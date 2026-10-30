package com.example.mcart;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import org.libsdl.app.SDLActivity;
import org.lwjgl.glfw.GLFW;
import org.lwjgl.glfw.GLFWErrorCallback;

/**
 * 通过 LWJGL 的 GLFW 绑定，走真正的 libglfw.so（= GLFW→SDL3 shim）。
 * 这条路径和 MC 的 Window 初始化完全一致。
 */
public class GlfwProbeActivity extends SDLActivity {

    /** 游戏/资源目录定死在内部储存的 .minecraft（配合 MANAGE_EXTERNAL_STORAGE）。 */
    private static final String MC_HOME = "/sdcard/.minecraft";

    /**
     * 读 MC_HOME 下的一个「控制文件」（开关 / 参数），不存在或读失败返回 null。
     * 这样 A/B 实验（后处理开关、自动进哪个世界）不用重编 dex/apk。
     */
    private static String readCtl(String name) {
        try {
            java.nio.file.Path p = java.nio.file.Paths.get(MC_HOME, name);
            if (!java.nio.file.Files.isRegularFile(p)) return null;
            return new String(java.nio.file.Files.readAllBytes(p), "UTF-8").trim();
        } catch (Throwable t) {
            return null;
        }
    }

    /**
     * 构造 QuickPlay 数据：存在 /sdcard/.minecraft/quickplay.txt 就自动进单人世界，
     * 免得每次要手点「单人游戏 → 选世界 → 进入」。文件内容为空 = 进最近的世界，
     * 否则内容当世界目录名（如「新的世界」）。这是量化测 FPS 的必要条件：
     * 后处理链只在 loaded world 的渲染路径里跑。
     */
    private static net.minecraft.client.main.GameConfig.QuickPlayData buildQuickPlay() {
        String qp = readCtl("quickplay.txt");
        if (qp != null) {
            String worldId = qp.isEmpty() ? null : qp;
            Log.i("GLFWPROBE", "QuickPlay: 自动进单人世界 worldId="
                    + (worldId == null ? "(最近的世界)" : worldId));
            return new net.minecraft.client.main.GameConfig.QuickPlayData(
                    null,
                    new net.minecraft.client.main.GameConfig.QuickPlaySinglePlayerData(worldId));
        }
        return new net.minecraft.client.main.GameConfig.QuickPlayData(
                null,
                net.minecraft.client.main.GameConfig.QuickPlayVariant.DISABLED);
    }

    private volatile boolean mRunning = true;

    // ── 悬浮帧率（长按 400ms 后拖动；位置存 SharedPreferences "art-overlay"）──
    private android.widget.TextView mFpsView;
    private volatile net.minecraft.client.Minecraft mMcRef;
    private final android.os.Handler mUiHandler = new android.os.Handler(android.os.Looper.getMainLooper());
    private float mDownRawX, mDownRawY;
    private int mDownLeft, mDownTop;
    private boolean mDragging;
    private android.os.Handler mDragHandler;
    private McConfig mCfg = new McConfig();
    private boolean mCfgLoaded = false;
    /** 内部渲染分辨率缩放（0.5~1.0），必须在建窗前传给 shim */
    private static native void nativeSetRenderScale(float scale);
    private long mWindow = 0;

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3" };
    }

    @Override
    protected void main() {
        Log.i("GLFWPROBE", "main() -> 阻塞保持 Activity");
        while (mRunning) {
            try { Thread.sleep(500); } catch (InterruptedException e) { break; }
        }
    }

    @Override
    protected void onCreate(Bundle state) {
        // ── 让 LWJGL 的 GL 走 EGL 初始化路径（= MC 自己的路径，不需要 patch GL 类）──
        // GL.<clinit> 会自动 create() 去找一个「OpenGL 上下文管理 API」：GLX / WGL / EGL。
        // Android 上没有 GLX/WGL，所以我们把 MobileGlues 指给 EGL 分支：
        //   loadEGL() -> dlopen(libmobileglues.so) -> 用它的 eglGetProcAddress/glXGetProcAddress 取 GL 函数
        // 必须放在 onCreate 最前：Configuration 的取值在类初始化时就缓存进实例字段。
        String nativeDir = getApplicationInfo().nativeLibraryDir;
        System.setProperty("org.lwjgl.opengl.contextAPI", "EGL");
        System.setProperty("org.lwjgl.egl.libname", nativeDir + "/libmobileglues.so");
        // 让 LWJGL 用绝对路径加载 native 库（dlopen 裸名字在 Android namespace 下不可靠）
        System.setProperty("org.lwjgl.librarypath",
                getApplicationInfo().nativeLibraryDir);
        // 用 System.loadLibrary 加载核心库，让 JNI_OnLoad 跑起来设置 jvm
        // （纯 dlopen 不会触发 JNI_OnLoad，Upcalls.getEnv 会拿到空 JavaVM 而崩）
        System.loadLibrary("lwjgl");
        // libglfw.so 里带了一个 JNI 入口用于开关软键盘（返回键）
        System.loadLibrary("glfw");

        // ── 自定义后处理链（光影-lite）开关 ──
        // GameRenderer 补丁（patch 17）读系统属性 art.posteffect，默认 true。
        // 写 /sdcard/.minecraft/posteffect.txt 内容为 off / false / 0 即关闭，
        // 这样 A/B 对比 FPS 和画面时不用重编 MC 的 d8。
        String pe = readCtl("posteffect.txt");
        if (pe != null) {
            boolean on = !(pe.equalsIgnoreCase("off")
                        || pe.equalsIgnoreCase("false")
                        || pe.equals("0"));
            System.setProperty("art.posteffect", on ? "true" : "false");
            Log.i("GLFWPROBE", "后处理链 art.posteffect=" + on + "（来自 posteffect.txt=\"" + pe + "\"）");
        } else {
            Log.i("GLFWPROBE", "后处理链 art.posteffect=默认(true)（无 posteffect.txt）");
        }

        // 「MC 设置」写的外部配置：悬浮帧率开关等（文件不存在就用默认值）
        try {
            mCfg = McConfig.load();
            mCfgLoaded = true;
            Log.i("GLFWPROBE", "McConfig 已加载（fpsOverlay=" + mCfg.showFpsOverlay + "）");
        } catch (Throwable t) {
            Log.w("GLFWPROBE", "McConfig 读取失败，用默认值", t);
        }
        // 内部渲染分辨率：必须在 glfwCreateWindow 之前设置（建窗时就按缩小后的 buffer 开）
        try {
            nativeSetRenderScale(mCfg.renderScale);
            Log.i("GLFWPROBE", "渲染分辨率缩放 = " + mCfg.renderScale);
        } catch (Throwable t) {
            Log.e("GLFWPROBE", "nativeSetRenderScale 失败", t);
        }

        super.onCreate(state);
        attachFpsOverlay();
        attachVctlOverlay();
        // 屏幕常亮，并尽量缩短等待：Android 上 App 一旦失焦 Surface 就会销毁，
        // 而 SDL_GL_CreateContext 会一直等 Surface，导致 glfwCreateWindow 卡死。
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        // 沉浸式全屏：去掉标题栏/状态栏/导航栏
        // （之前没有全屏主题，Android 会在顶部显示一条标题栏 —— 标题来自 MC 的
        //  glfwSetWindowTitle -> SDL_SetWindowTitle -> Activity label，
        //  显示成 "Minecraft* Development Version"，并且会挤掉内容区、使创建世界界面
        //  下半部分点不到。）
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().setDecorFitsSystemWindows(false);
        hideSystemBars();
        // 真实窗口尺寸（不要硬编码：SDK 33+ 用 WindowMetrics，回退到 DisplayMetrics）
        android.util.DisplayMetrics dm = new android.util.DisplayMetrics();
        getResources().getDisplayMetrics();
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            android.graphics.Rect b = getWindowManager().getCurrentWindowMetrics().getBounds();
            mWinW = b.width(); mWinH = b.height();
        } else {
            dm = getResources().getDisplayMetrics();
            mWinW = dm.widthPixels; mWinH = dm.heightPixels;
        }
        Log.i("GLFWPROBE", "窗口真实尺寸 = " + mWinW + "x" + mWinH);
        applySurfaceFixedSize();

        // 堆上限报告：Android 的堆由 ART 决定，不是 -Xmx
        //   largeHeap=false -> heapgrowthlimit；largeHeap=true -> heapsize
        Runtime rt0 = Runtime.getRuntime();
        android.app.ActivityManager am =
                (android.app.ActivityManager) getSystemService(android.content.Context.ACTIVITY_SERVICE);
        android.app.ActivityManager.MemoryInfo mi = new android.app.ActivityManager.MemoryInfo();
        am.getMemoryInfo(mi);
        Log.i("GLFWPROBE", "堆信息: maxMemory=" + (rt0.maxMemory() / 1048576) + "MB"
                + " memoryClass=" + am.getMemoryClass() + "MB"
                + " largeMemoryClass=" + am.getLargeMemoryClass() + "MB"
                + " 设备可用RAM=" + (mi.availMem / 1048576) + "MB"
                + " 低内存阈值=" + (mi.threshold / 1048576) + "MB");

        // Bootstrap.bootStrap() 要跑几秒（注册物品/方块/注册表）。它不能放在主线程：
        // Android 主线程阻塞超过 5 秒会 ANR 并被系统杀掉
        // （实测 "Input dispatching timed out ... not responding. Waited 5000ms"）。
        // 所以放到后台线程跑完，再回主线程做窗口/GL/Minecraft 初始化。
        new Thread(() -> {
            try {
                Class.forName("net.minecraft.SharedConstants", true,
                        getClassLoader()).getMethod("tryDetectVersion").invoke(null);
                Log.i("GLFWPROBE", "SharedConstants.tryDetectVersion OK (bg)");

                long t = System.currentTimeMillis();
                Class.forName("net.minecraft.server.Bootstrap", false,
                        getClassLoader()).getMethod("bootStrap").invoke(null);
                Log.i("GLFWPROBE", "Bootstrap.bootStrap OK in "
                        + (System.currentTimeMillis() - t) + " ms (bg)");

                // Main.main() 在 bootStrap() 之后还会调这两个，探针绕过了 Main.main，必须自己补：
                //   ClientBootstrap.bootstrap() —— 注册客户端侧 late-bound 编解码器
                //     （ItemModels / SpecialModelRenderers / ItemTintSources / SpriteSources 等）。
                //     缺了它，所有 atlas 都会 "Failed to parse atlas definition ...:
                //     Unknown element id: minecraft:single" —— 纹理图集全空 => 紫黑缺失纹理。
                t = System.currentTimeMillis();
                net.minecraft.client.ClientBootstrap.bootstrap();
                Log.i("GLFWPROBE", "ClientBootstrap.bootstrap OK in "
                        + (System.currentTimeMillis() - t) + " ms (bg)");

                net.minecraft.server.Bootstrap.validate();
                Log.i("GLFWPROBE", "Bootstrap.validate OK (bg)");
            } catch (Throwable t) {
                Log.e("GLFWPROBE", "Bootstrap 失败（后台线程）", t);
            }
            // 关键：整个 GL/MC 主循环必须跑在独立线程上，不能占住 Java 主线程。
            // mc.run() 是死循环，如果它跑在主线程（Looper 回调里），主线程永远不返回 Looper.loop()，
            // Android 的输入事件（触摸/按键）就永远得不到分发 ——
            // 5 秒后系统判定 "Input dispatching timed out" 触发 ANR 并杀掉进程
            // （实测两次：MotionEvent DOWN / KeyEvent scanCode=114）。
            // 放到独立线程后：主线程空闲 -> SDLSurface 收输入 -> 塞进 SDL 事件队列
            // -> 游戏线程的 glfwPollEvents 取走，全程不阻塞输入分发。
            new Handler(Looper.getMainLooper()).postDelayed(
                    () -> new Thread(this::runProbe, "mc-game").start(), 150);
        }, "mc-bootstrap").start();
    }

    private static native void nativeSetSoftKeyboard(boolean show);

    private boolean mKeyboardShown = false;

    /** 返回键 = 开关系统软键盘（不退出 Activity，也不再交给 SDL 当按键）。
     *  只有走 SDL_StartTextInput 才会让 SDL 产出 SDL_EVENT_TEXT_INPUT，
     *  MC 的文本框（世界种子/聊天/服务器地址）才能收到字符。 */
    private void toggleSoftKeyboard() {
        mKeyboardShown = !mKeyboardShown;
        Log.i("GLFWPROBE", "返回键 -> 软键盘 " + (mKeyboardShown ? "开" : "关"));
        try {
            nativeSetSoftKeyboard(mKeyboardShown);
        } catch (Throwable t) {
            Log.e("GLFWPROBE", "软键盘切换失败", t);
        }
    }

    @Override
    public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        if (event.getKeyCode() == android.view.KeyEvent.KEYCODE_BACK) {
            if (event.getAction() == android.view.KeyEvent.ACTION_UP && !event.isCanceled()) {
                toggleSoftKeyboard();
            }
            return true;   // 整个吃掉：不给 SDL 当按键，也不触发 finish
        }
        return super.dispatchKeyEvent(event);
    }

    private void startPerfSampler(final net.minecraft.client.Minecraft mc) {
        Thread t = new Thread(() -> {
            while (mRunning) {
                try {
                    Thread.sleep(10000);
                } catch (InterruptedException e) {
                    return;
                }
                try {
                    Runtime rt = Runtime.getRuntime();
                    long used = (rt.totalMemory() - rt.freeMemory()) / 1048576;
                    Log.i("PERF", String.format(
                            java.util.Locale.ROOT,
                            "fps=%d  heap=%d/%dMB  rd=%d  sd=%d  gfx=%s  mip=%d  particles=%s",
                            mc.getFps(), used, rt.maxMemory() / 1048576,
                            mc.options.renderDistance().get(),
                            mc.options.simulationDistance().get(),
                            mc.options.graphicsPreset().get().getSerializedName(),
                            mc.options.mipmapLevels().get(),
                            mc.options.particles().get().name()));
                } catch (Throwable ignored) {
                }
            }
        }, "perf-sampler");
        t.setDaemon(true);
        t.start();
    }

    private int mWinW = 1080, mWinH = 2378;

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        android.view.Window w = getWindow();
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            android.view.WindowInsetsController c = w.getInsetsController();
            if (c != null) {
                c.hide(android.view.WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(
                        android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            w.getDecorView().setSystemUiVisibility(
                    android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                  | android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
                  | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                  | android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                  | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                  | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
        // 兜底：某些 ROM（Oppo/OnePlus）在 WindowInsetsController.hide 之后
        // 仍会把状态栏画出来，这里再压一次旧式 flag
        w.getDecorView().setSystemUiVisibility(
                android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
              | android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
              | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
              | android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE
              | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
              | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
    }

    private void runProbe() {
        // ── assets 通道验证（两块来源）──
        //  (1) 外部大资源：/sdcard/.minecraft/assets 用 MC 原生的 objects 哈希 + index 结构，
        //      由 IndexedAssetSource.createIndexFs() 建 LinkFileSystem 虚拟映射，无需解压。
        //  (2) jar 内资源（shader/models/等 ~5.5MB）：已随 APK 打进 assets/。
        try {
            java.nio.file.Path assetsDir = java.nio.file.Paths.get(MC_HOME, "assets");
            Log.i("GLFWPROBE", "外部 assets 目录 exists=" + java.nio.file.Files.isDirectory(assetsDir));

            java.nio.file.Path fsRoot =
                    net.minecraft.client.resources.IndexedAssetSource.createIndexFs(assetsDir, "32");
            Log.i("GLFWPROBE", "LinkFileSystem 建立: " + fsRoot);

            // 验证 LinkFSPath 补丁：现在可以直接用 resolve(String)（之前会 AbstractMethodError）
            java.nio.file.FileSystem lfs = fsRoot.getFileSystem();
            java.nio.file.Path lang = fsRoot.resolve("minecraft/lang/zh_cn.json");
            boolean hasLang = java.nio.file.Files.exists(lang);
            Log.i("GLFWPROBE", "外部 lang/zh_cn.json exists=" + hasLang);
            if (hasLang) {
                // 用 readAllBytes 真正读内容（Files.size 在 LinkFSPath 上不可靠）
                byte[] data = java.nio.file.Files.readAllBytes(lang);
                Log.i("GLFWPROBE", "外部 lang/zh_cn.json 实读 = " + data.length + " 字节");
            }

            // 抽查一张真实存在的纹理，验证 objects -> 逻辑路径 的映射和内容读取
            java.nio.file.Path tex = fsRoot.resolve(
                    "minecraft/textures/gui/title/background/panorama_0.png");
            boolean hasTex = java.nio.file.Files.exists(tex);
            Log.i("GLFWPROBE", "外部 panorama_0.png exists=" + hasTex);
            if (hasTex) {
                byte[] png = java.nio.file.Files.readAllBytes(tex);
                Log.i("GLFWPROBE", "外部 panorama_0.png 实读 = " + png.length + " 字节, PNG魔数="
                        + ((png.length > 3 && (png[0] & 0xFF) == 0x89 && png[1] == 'P') ? "OK" : "BAD"));
            }

            ClassLoader cl = getClass().getClassLoader();
            Log.i("GLFWPROBE", "APK .mcassetsroot = " + cl.getResource("assets/.mcassetsroot"));
            Log.i("GLFWPROBE", "APK shaders/core/block.vsh = "
                    + cl.getResource("assets/minecraft/shaders/core/block.vsh"));
        } catch (Throwable t) {
            Log.e("GLFWPROBE", "assets 通道失败", t);
        }

        try {
            GLFWErrorCallback err = GLFWErrorCallback.create(
                    (code, desc) -> Log.e("GLFWPROBE", "GLFW error " + code + ": " + desc));
            err.set();
            Log.i("GLFWPROBE", "GLFW.getLibrary() = " + GLFW.getLibrary());

            boolean ok = GLFW.glfwInit();
            Log.i("GLFWPROBE", "glfwInit() = " + ok);
            if (!ok) return;

            Log.i("GLFWPROBE", "glfwGetVersionString = " + GLFW.glfwGetVersionString());
            Log.i("GLFWPROBE", "glfwGetPlatform = " + GLFW.glfwGetPlatform()
                    + "  rawMouseMotionSupported=" + GLFW.glfwRawMouseMotionSupported());

            GLFW.glfwDefaultWindowHints();
            GLFW.glfwWindowHint(GLFW.GLFW_CONTEXT_VERSION_MAJOR, 3);
            GLFW.glfwWindowHint(GLFW.GLFW_CONTEXT_VERSION_MINOR, 2);
            GLFW.glfwWindowHint(GLFW.GLFW_VISIBLE, GLFW.GLFW_TRUE);

            mWindow = GLFW.glfwCreateWindow(1280, 720, "MC on ART (GLFW shim)", 0, 0);
            Log.i("GLFWPROBE", "glfwCreateWindow = 0x" + Long.toHexString(mWindow));
            if (mWindow == 0) return;

            GLFW.glfwMakeContextCurrent(mWindow);
            GLFW.glfwSwapInterval(1);
            GLFW.glfwShowWindow(mWindow);
            Log.i("GLFWPROBE", "makeContextCurrent + swapInterval + show 完成");

            // 按 MC 的真实路径验证 GL 类：函数地址全部来自 GLFW（→ SDL → MobileGlues），
            // 由 liblwjgl_opengl.so 的 JNI 通过 JNIEnv->reserved3 的槽位表间接调用。
            //
            // 关键：GL.<clinit> 默认会自动 create() 去探测 GLX/WGL/EGL 上下文管理 API，
            // Android 上三者都没有会抛 "There is no OpenGL context management API available."
            // 设 explicitInit=true 让 clinit 跳过自动探测，由我们自己注入 FunctionProvider。
            try {
                // 依赖 GL.<clinit> 的自动 create()（走上面配置的 EGL 路径）——
                // 这正是 MC 的 GlDevice 构造里直接调 createCapabilities() 的前提。
                org.lwjgl.opengl.GL.createCapabilities();
                Log.i("GLFWPROBE", "GL.createCapabilities OK —— GL 类路径已打通");
                Log.i("GLFWPROBE", "GL11.GL_VERSION  = " + org.lwjgl.opengl.GL11.glGetString(org.lwjgl.opengl.GL11.GL_VERSION));
                Log.i("GLFWPROBE", "GL11.GL_RENDERER = " + org.lwjgl.opengl.GL11.glGetString(org.lwjgl.opengl.GL11.GL_RENDERER));
            } catch (Throwable t) {
                Log.e("GLFWPROBE", "GL 类路径失败", t);
            }

            // 验证 GL 层：直接用 glfwGetProcAddress 拿 glGetString 再 JNI 调，
            // 绕开 org.lwjgl.opengl.GL —— 它的 <clinit> 要加载 liblwjgl_opengl.so（MC 才需要，探针不需要）
            try {
                long fnGetString = GLFW.glfwGetProcAddress("glGetString");
                Log.i("GLFWPROBE", "glGetString addr = 0x" + Long.toHexString(fnGetString));
                if (fnGetString != 0L) {
                    // GLenum: GL_VERSION=0x1F02, GL_RENDERER=0x1F01, GL_VENDOR=0x1F00
                    // 注意：LWJGL 的 JNI.invokeXxx 把「函数地址」放在最后一个参数
                    long pVersion  = org.lwjgl.system.JNI.invokePP(0x1F02L, fnGetString);
                    long pRenderer = org.lwjgl.system.JNI.invokePP(0x1F01L, fnGetString);
                    long pVendor   = org.lwjgl.system.JNI.invokePP(0x1F00L, fnGetString);
                    Log.i("GLFWPROBE", "GL_VERSION  = " + org.lwjgl.system.MemoryUtil.memUTF8Safe(pVersion));
                    Log.i("GLFWPROBE", "GL_RENDERER = " + org.lwjgl.system.MemoryUtil.memUTF8Safe(pRenderer));
                    Log.i("GLFWPROBE", "GL_VENDOR   = " + org.lwjgl.system.MemoryUtil.memUTF8Safe(pVendor));
                }
            } catch (Throwable t) {
                Log.e("GLFWPROBE", "GL 查询失败", t);
            }

            int[] w = new int[1], h = new int[1];
            GLFW.glfwGetFramebufferSize(mWindow, w, h);
            Log.i("GLFWPROBE", "framebufferSize = " + w[0] + "x" + h[0]);
            Log.i("GLFWPROBE", "glfwGetTime = " + GLFW.glfwGetTime());

            org.lwjgl.PointerBuffer monitors = GLFW.glfwGetMonitors();
            Log.i("GLFWPROBE", "monitors = " + (monitors == null ? "null" : monitors.remaining())
                    + " primary=0x" + Long.toHexString(GLFW.glfwGetPrimaryMonitor()));
            if (monitors != null && monitors.remaining() > 0) {
                long mon = monitors.get(0);
                Log.i("GLFWPROBE", "monitorName = " + GLFW.glfwGetMonitorName(mon));
                org.lwjgl.glfw.GLFWVidMode vm = GLFW.glfwGetVideoMode(mon);
                if (vm != null) {
                    Log.i("GLFWPROBE", "videoMode = " + vm.width() + "x" + vm.height()
                            + "@" + vm.refreshRate());
                }
            }

            // 跑 120 帧，验证 pollEvents + swapBuffers 循环
            long t0 = System.nanoTime();
            final int FRAMES = 30;
            for (int i = 0; i < FRAMES; i++) {
                GLFW.glfwPollEvents();
                GLFW.glfwSwapBuffers(mWindow);
            }
            long ms = (System.nanoTime() - t0) / 1000000;
            Log.i("GLFWPROBE", FRAMES + " 帧完成，用时 " + ms + "ms ("
                    + (ms > 0 ? (FRAMES * 1000L / ms) : 0) + " fps)");
            Log.i("GLFWPROBE", "windowShouldClose = " + GLFW.glfwWindowShouldClose(mWindow));

            // ── MC 渲染层验证：直接用 MC 自己的 GlBackend / GlDevice ──
            // 这是 Minecraft.<init> 里 backend.createDevice(...) 的完全同一条路径。
            // 注意：SDL3 的 Android 后端只允许一个窗口（"Android only supports one window"），
            // 所以先销毁探针自己的窗口再让 MC 建窗 —— MC 本身也只会建一个。
            GLFW.glfwMakeContextCurrent(0L);
            GLFW.glfwDestroyWindow(mWindow);
            mWindow = 0;
            Log.i("GLFWPROBE", "探针窗口已销毁，交给 MC 建窗");
            try {
                // MC 的 RenderSystem 要求所有渲染调用发生在它认定的「渲染线程」上。
                // Minecraft.<init> 里的第一件事就是 initBackendSystem()（把当前线程记为渲染线程），
                // 不做这一步，GlStateManager._getString 会抛 "Rendersystem called from wrong thread"。
                // Main.java:264 先 initRenderThread() 把主线程登记为渲染线程，
                // 然后才 initBackendSystem()。少了前者，GlStateManager 的
                // assertOnRenderThread() 会一直抛 "Rendersystem called from wrong thread"。
                com.mojang.blaze3d.systems.RenderSystem.initRenderThread();
                Log.i("GLFWPROBE", "RenderSystem.initRenderThread OK");
                com.mojang.blaze3d.systems.RenderSystem.initBackendSystem();
                Log.i("GLFWPROBE", "RenderSystem.initBackendSystem OK");

                com.mojang.blaze3d.opengl.GlBackend backend =
                        new com.mojang.blaze3d.opengl.GlBackend();
                Log.i("GLFWPROBE", "MC GlBackend 实例化 OK, name=" + backend.getName());

                backend.setWindowHints();  // MC 的 3.3 core hints
                long mcWin = GLFW.glfwCreateWindow(854, 480, "MC GlDevice", 0L, 0L);
                Log.i("GLFWPROBE", "MC 窗口 = 0x" + Long.toHexString(mcWin));
                if (mcWin != 0L) {
                    com.mojang.blaze3d.shaders.GpuDebugOptions opts =
                            new com.mojang.blaze3d.shaders.GpuDebugOptions(0, false, false, false);
                    com.mojang.blaze3d.systems.GpuDevice dev = backend.createDevice(
                            mcWin,
                            (id, type) -> null,   // ShaderSource：暂不提供任何 shader
                            opts,
                            () -> Log.i("GLFWPROBE", "criticalShaderLoader 被调用"));
                    Log.i("GLFWPROBE", "MC GlDevice 创建成功: " + dev.getDeviceInfo());
                    GLFW.glfwDestroyWindow(mcWin);
                    Log.i("GLFWPROBE", "MC 窗口已销毁");
                }
            } catch (Throwable t) {
                Log.e("GLFWPROBE", "MC 渲染层失败", t);
            }

            // ── 最后一步：构造完整的 Minecraft 实例 ──
            // 这一���会真正触发资源加载 + shader 编译（GlslCompiler -> MobileGlues 的 GLSL 3.3 core）。
            try {
                java.io.File gameDir  = new java.io.File(MC_HOME);
                java.io.File assetsDir = new java.io.File(MC_HOME + "/assets");
                java.io.File packDir  = new java.io.File(MC_HOME + "/resourcepacks");
                gameDir.mkdirs();
                packDir.mkdirs();

                net.minecraft.client.User user = new net.minecraft.client.User(
                        "Player",
                        java.util.UUID.nameUUIDFromBytes("Player".getBytes("UTF-8")),
                        "",
                        java.util.Optional.<String>empty(),
                        java.util.Optional.<String>empty());

                com.mojang.blaze3d.platform.DisplayData display =
                        new com.mojang.blaze3d.platform.DisplayData(
                                mWinW, mWinH,
                                java.util.OptionalInt.empty(),
                                java.util.OptionalInt.empty(),
                                false);

                net.minecraft.client.main.GameConfig.FolderData folder =
                        new net.minecraft.client.main.GameConfig.FolderData(
                                gameDir, packDir, assetsDir, "32");

                net.minecraft.client.main.GameConfig.GameData gd =
                        new net.minecraft.client.main.GameConfig.GameData(
                                false,          // demo
                                "26.2",         // launchVersion
                                "release",      // versionType
                                false,          // disableMultiplayer
                                false,          // disableChat
                                false,          // captureTracyImages
                                false,          // vulkanValidation
                                false,          // renderDebugLabels
                                null,           // forcedGraphicsApi
                                false);         // offlineDeveloperMode

                net.minecraft.client.main.GameConfig cfg =
                        new net.minecraft.client.main.GameConfig(
                                new net.minecraft.client.main.GameConfig.UserData(
                                        user, java.net.Proxy.NO_PROXY),
                                display, folder, gd,
                                buildQuickPlay());
                Log.i("GLFWPROBE", "GameConfig 构造 OK");

                net.minecraft.util.Util.startTimerHackThread();
                Log.i("GLFWPROBE", "Util.startTimerHackThread OK");

                net.minecraft.client.Minecraft mc = new net.minecraft.client.Minecraft(cfg);
                Log.i("GLFWPROBE", "★★★ Minecraft 实例创建成功！");

                // 进入主循环（阻塞，直到游戏关闭）。
                // run() = while(running){ RenderSystem.pollEvents(); runTick(); }，
                // 靠 swapBuffers 的 vsync 阻塞控制帧率，不是忙等。
                // 性能采样：每 10 秒把 FPS / 堆 / 渲染距离打到 logcat，
                // 免得靠肉眼看左上角数字（也方便我远程量化）。
                mMcRef = mc;
                startFpsOverlayUpdater();
                startPerfSampler(mc);

                Log.i("GLFWPROBE", "进入 mc.run() 主循环 ...");
                mc.run();
                Log.i("GLFWPROBE", "主循环已退出");
            } catch (Throwable t) {
                Log.e("GLFWPROBE", "Minecraft 构造失败", t);
            }

            Log.i("GLFWPROBE", "PROBE OK");
        } catch (Throwable t) {
            Log.e("GLFWPROBE", "探针失败", t);
        }
    }

    // SDL/系统会在恢复、失焦回来、窗口变化时把系统栏重新显示出来，
    // 所以每个时机都要重新隐藏一次（否则顶部会冒出一条状态栏把游戏内容挤下去）。
    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
        applySurfaceFixedSize();   // surface 重建后要重新固定 buffer 尺寸
        attachFpsOverlay();    // 幂等：SDL 布局被重建时重新挂上
        attachVctlOverlay();   // 幂等：SDL 的 mLayout 被重建/我们的 View 被摘掉时重新挂上
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    // ── 虚拟触屏按键浮层（只绘制，不吃触摸；命中判定在 libglfw.so 的 VCTL 里）──
    private VctlOverlay mVctlView;

    /**
     * 虚拟触屏按键的可见浮层（onCreate / onResume 都调，幂等）。
     *
     * 挂载点必须是 SDL 自己那个装 mSurface 的 RelativeLayout（SDLActivity.mLayout，
     * 通过 getContentView() 拿到）：不要用 addContentView()，那是往 android.R.id.content
     * 上挂，SDLActivity 一旦再 setContentView(mLayout) 就会把我们的 View 顶掉。
     *
     * overlay 只负责画、不吃触摸（onTouchEvent 返回 false、不 clickable），
     * RelativeLayout 会继续把事件派给下面的 mSurface，命中判定仍由 libglfw.so 的 VCTL 逻辑负责。
     */
    private void attachVctlOverlay() {
        try {
            android.view.ViewGroup layout = (android.view.ViewGroup) getContentView();
            if (layout == null) {
                Log.w("GLFWPROBE", "VCTL 浮层：SDL 布局还没建好，稍后 onResume 再试");
                return;
            }
            if (mVctlView == null) {
                mVctlView = new VctlOverlay(this);
                Log.i("GLFWPROBE", "VCTL 浮层：新建 View");
            }
            if (mVctlView.getParent() != null) {
                return;                                  // 已经挂着，幂等返回
            }
            android.view.ViewGroup.LayoutParams lp = new android.view.ViewGroup.LayoutParams(
                    android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                    android.view.ViewGroup.LayoutParams.MATCH_PARENT);
            layout.addView(mVctlView, lp);               // 兄弟节点，后加者在上层
            mVctlView.bringToFront();
            Log.i("GLFWPROBE", "VCTL 浮层已挂到 " + layout.getClass().getSimpleName()
                    + "（仅绘制，不吃触摸）");
        } catch (Throwable t) {
            Log.e("GLFWPROBE", "VCTL 浮层挂载失败（游戏照常运行，只是看不见按钮）", t);
        }
    }

    // ── 内部渲染分辨率（SurfaceView.setFixedSize + 合成器拉伸）────────────
    /**
     * 把 SDL 的 SurfaceView buffer 固定成缩小尺寸（视图仍是全屏，合成器拉伸）。
     *
     * 为什么不用 SDL_SetWindowSize：Android 上 SDL 窗口尺寸恒等于 SurfaceView 的
     * 视图尺寸，SDL_SetWindowSize 对真实 buffer 无效（实测 buffer 仍是 2378x1080），
     * 而 SDL 创建 EGL surface 时还会用 setBuffersGeometry(0,0) 重置 geometry。
     * SurfaceHolder.setFixedSize 才是「buffer 固定小尺寸、视图铺满」的正确入口。
     *
     * 时序：必须在 SDL_GL_CreateContext 之前生效（本方法 onCreate 就调），
     * 且 shim 侧把 glfwGetFramebufferSize 返回值换成缩小后的尺寸，两者大小一致。
     */
    private void applySurfaceFixedSize() {
        if (mCfg.renderScale >= 0.995f) return;
        try {
            android.view.ViewGroup layout = (android.view.ViewGroup) getContentView();
            if (layout == null) { Log.w("GLFWPROBE", "Surface 固定尺寸：SDL 布局还没建好"); return; }
            int sw = Math.max(160, Math.round(mWinW * mCfg.renderScale));
            int sh = Math.max(120, Math.round(mWinH * mCfg.renderScale));
            for (int i = 0; i < layout.getChildCount(); i++) {
                android.view.View v = layout.getChildAt(i);
                if (v instanceof android.view.SurfaceView) {
                    ((android.view.SurfaceView) v).getHolder().setFixedSize(sw, sh);
                    VctlOverlay.setRenderScale(mCfg.renderScale);   // 浮层按 1/scale 画
                    Log.i("GLFWPROBE", "Surface fixedSize = " + sw + "x" + sh
                            + "（renderScale=" + mCfg.renderScale + "，视图仍全屏，合成器拉伸）");
                    return;
                }
            }
            Log.w("GLFWPROBE", "Surface 固定尺寸：没找到 SurfaceView（子 View 数=" + layout.getChildCount() + "）");
        } catch (Throwable t) {
            Log.e("GLFWPROBE", "Surface 固定尺寸设置失败", t);
        }
    }

    // ── 悬浮帧率（可拖动）────────────────────────────────────────────────
    private int dp(int v) {
        return Math.round(getResources().getDisplayMetrics().density * v);
    }

    /** 把悬浮层限制在屏幕内（长按拖动时用） */
    private void clampOverlay(android.widget.RelativeLayout.LayoutParams l) {
        android.view.ViewGroup parent = (android.view.ViewGroup) mFpsView.getParent();
        if (parent == null) return;
        int maxLeft = Math.max(0, parent.getWidth() - mFpsView.getWidth());
        int maxTop  = Math.max(0, parent.getHeight() - mFpsView.getHeight());
        l.leftMargin = Math.max(0, Math.min(maxLeft, l.leftMargin));
        l.topMargin  = Math.max(0, Math.min(maxTop, l.topMargin));
    }

    /** 挂 FPS 悬浮层（onCreate / onResume 都调，幂等） */
    private void attachFpsOverlay() {
        if (!mCfg.showFpsOverlay) { Log.i("GLFWPROBE", "FPS 悬浮层：配置里关闭"); return; }
        try {
            if (mFpsView != null && mFpsView.getParent() != null) return;   // 已挂着
            android.view.ViewGroup layout = (android.view.ViewGroup) getContentView();
            if (layout == null) { Log.w("GLFWPROBE", "FPS 悬浮层：拿不到 SDL 布局"); return; }
            if (!(layout instanceof android.widget.RelativeLayout)) {
                Log.w("GLFWPROBE", "FPS 悬浮层：根布局不是 RelativeLayout（"
                        + layout.getClass().getSimpleName() + "），跳过");
                return;
            }

            android.widget.TextView tv = new android.widget.TextView(this);
            tv.setText("-- fps");
            tv.setTextColor(0xFFE8EAED);
            tv.setTextSize(13f);
            tv.setShadowLayer(3f, 0f, 0f, 0xFF000000);
            tv.setPadding(dp(10), dp(3), dp(10), dp(3));
            tv.setBackgroundColor(0x66000000);
            tv.setGravity(android.view.Gravity.CENTER);

            android.widget.RelativeLayout.LayoutParams lp = new android.widget.RelativeLayout.LayoutParams(
                    android.view.ViewGroup.LayoutParams.WRAP_CONTENT,
                    android.view.ViewGroup.LayoutParams.WRAP_CONTENT);
            android.content.SharedPreferences sp = getSharedPreferences("art-overlay", MODE_PRIVATE);
            lp.leftMargin = sp.getInt("left", dp(72));   // 往里缩，避开 R 角
            lp.topMargin  = sp.getInt("top",  dp(8));
            layout.addView(tv, lp);

            mFpsView = tv;

            // ── 长按后拖动 ──
            mDragHandler = new android.os.Handler(android.os.Looper.getMainLooper());
            final Runnable longPress = () -> {
                mDragging = true;
                tv.setAlpha(0.75f);
                tv.performHapticFeedback(android.view.HapticFeedbackConstants.LONG_PRESS);
            };

            tv.setOnTouchListener((v, ev) -> {
                android.widget.RelativeLayout.LayoutParams l =
                        (android.widget.RelativeLayout.LayoutParams) v.getLayoutParams();
                switch (ev.getActionMasked()) {
                    case android.view.MotionEvent.ACTION_DOWN:
                        mDownRawX = ev.getRawX();
                        mDownRawY = ev.getRawY();
                        mDownLeft = l.leftMargin;
                        mDownTop = l.topMargin;
                        mDragging = false;
                        mDragHandler.postDelayed(longPress, 400);
                        return true;
                    case android.view.MotionEvent.ACTION_MOVE:
                        if (!mDragging) { return true; }
                        l.leftMargin = mDownLeft + (int) (ev.getRawX() - mDownRawX);
                        l.topMargin  = mDownTop  + (int) (ev.getRawY() - mDownRawY);
                        clampOverlay(l);
                        v.setLayoutParams(l);
                        return true;
                    case android.view.MotionEvent.ACTION_UP:
                    case android.view.MotionEvent.ACTION_CANCEL:
                        mDragHandler.removeCallbacks(longPress);
                        tv.setAlpha(1.0f);
                        if (mDragging) {
                            mDragging = false;
                            getSharedPreferences("art-overlay", MODE_PRIVATE).edit()
                                    .putInt("left", l.leftMargin)
                                    .putInt("top", l.topMargin)
                                    .apply();
                            Log.i("GLFWPROBE", "FPS 悬浮层位置已记住: " + l.leftMargin + "," + l.topMargin);
                        }
                        return true;
                }
                return true;
            });

            Log.i("GLFWPROBE", "FPS 悬浮层已挂上（左 " + lp.leftMargin + " 上 " + lp.topMargin
                    + "，长按可拖动）");
        } catch (Throwable t) {
            Log.e("GLFWPROBE", "FPS 悬浮层挂载失败", t);
        }
    }

    /** 每 500ms 刷新一次文本（MC 的 getFps() 是最近 1 秒的帧数） */
    private void startFpsOverlayUpdater() {
        if (mFpsView == null) return;
        Thread t = new Thread(() -> {
            while (mRunning) {
                try {
                    Thread.sleep(500);
                } catch (InterruptedException e) {
                    return;
                }
                net.minecraft.client.Minecraft mc = mMcRef;
                String txt;
                if (mc == null) {
                    txt = "-- fps";
                } else {
                    txt = mc.getFps() + " fps";
                    if (mCfg.renderScale < 0.995f) {
                        txt = txt + "  " + Math.round(mCfg.renderScale * 100) + "%";
                    }
                }
                final String text = txt;
                final android.widget.TextView tv = mFpsView;
                if (tv != null) {
                    mUiHandler.post(() -> {
                        try { tv.setText(text); } catch (Throwable ignored) {}
                    });
                }
            }
        }, "fps-overlay");
        t.setDaemon(true);
        t.start();
        Log.i("GLFWPROBE", "FPS 悬浮层刷新线程已启动");
    }

    @Override
    protected void onDestroy() {
        mRunning = false;
        if (mWindow != 0) {
            GLFW.glfwDestroyWindow(mWindow);
            mWindow = 0;
            GLFW.glfwTerminate();
        }
        super.onDestroy();
    }
}
