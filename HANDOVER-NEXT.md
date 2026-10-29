# 交接文档（给下一个会话）：MC 26.2 → ART + SDL3

> 写于：本轮会话结束。详细历史见同目录 `HANDOVER.md`（第十、十一、十二节是本轮的），
> 本文档是**面向新会话的完整独立说明**，读完这份就能直接上手。

---

# ★ 本轮更新（2026-10-29 晚）：GLFW 层已完整打通

> 接手时的卡点是「`GLFW.<clinit>` dlopen `libglfw.so` 失败，且崩溃把原因盖住」。
> 本轮把这条链路彻底打通：**`GlfwProbeActivity` 现在 `PROBE OK`**。

## 实测结果（`GlfwProbeActivity`，已清理掉所有诊断代码后的纯路径）

```
GLFW.getLibrary() = LinuxLibrary pointer [...]
glfwInit() = true
glfwGetVersionString = 3.4.0 SDL3-shim (MC on ART)
glfwCreateWindow = 0x70a8bea060
makeContextCurrent + swapInterval + show 完成
framebufferSize = 1080x2378
monitors = 1   videoMode = 1080x2378@60
30 帧完成，用时 459ms (65 fps)      ← swap 真的上屏了
PROBE OK
```

## 三个根因（每个都单独致命，按被发现顺序）

### 根因 1：`dlerror()` 崩溃 —— `MultiReleaseTextDecoding` 用了双参数 `Unsafe`

**现象**：`GLFW.<clinit>` → `Library.loadNative` → `dlopen` 失败 → error 分支 →
`dlerror()` → `memUTF8Safe` → `decodeUTF8` → **SIGSEGV**，fault addr 恒为
`0xffffffffb37e9190`。**崩溃把 dlopen 的真实原因完全盖住了。**

**根因**：`org.lwjgl.system.MultiReleaseTextDecoding.decodeUTF8(long,int)` 里有 **4 处**
`UNSAFE.getByte(null, source + position++)`（字节码是 `aconst_null` +
`getByte:(Ljava/lang/Object;J)B`）。ART 对 `o == null` 的双参数形式把 offset 当字段偏移并做
**32 位符号扩展**（`0xb37e9190` → `0xffffffffb37e9190`）→ 必崩。

上一轮的 patch 只改了 `MemoryUtil`，**`MultiReleaseTextDecoding` 是盲点**。
佐证：同一包内的 `memLengthNT1`/`strlen64NT1` 一直用单参数 `UNSAFE.getByte(long)`，
从未崩过 —— 说明单参数绝对地址版在 ART 上可用、双参数不可用。

**修复**（新增 `lwjglbuild/patch/MultiReleaseTextDecoding.java`）：
- 4 处全部改单参数 `UNSAFE.getByte(source + position++)`
- 加 `isBogusAddress(long)` 防御：高 32 位全 1 = 符号扩展产物，返回 `<badptr:0x...>` 而非崩
- `MemoryUtil` 同步加 `isBogusAddress`；`memLengthNT1` 命中即返回 0；
  `memUTF8Safe(long)` 返回可辨识诊断串（**这样坏地址再也不会掩盖真实原因**）

### 根因 2：`ndlopen` 被优化成 tail call → linker namespace 错成 `com_android_art`

修完根因 1，真实错误终于露出来：

```
dlopen failed: library "libSDL3.so" not found:
  needed by .../libglfw.so in namespace com_android_art
```

但同一个库：`System.loadLibrary("glfw")` **能加载**；C 里直接 `dlopen` 也**能加载**
（`err=(NULL/no-error)`，`dlsym(SDL_Init)`/`dlsym(glfwInit)` 都有地址）。
**只有 LWJGL 的 `ndlopen` 失败。**

**根因**：生成的 C 是

```c
return (jlong)(uintptr_t)dlopen(filename, mode);
```

`-O2` 编译成了 **tail call**：

```asm
mov w1, w3
mov x0, x2
b   0x643a0 <dlopen@plt>     ← b（尾调用），不是 bl
```

bionic 的 `dlopen` 用 `__builtin_return_address(0)` 取调用者地址，据此定位调用者 soinfo 的
linker namespace，再用该 namespace 解析 DT_NEEDED。尾调用让返回地址落在
**ART 的 JNI trampoline（`libart.so`）**里 → namespace 变成 `com_android_art`；
而 `libSDL3.so` 是 `SDLActivity` 用 `System.loadLibrary` 加载到 app 的 `clns-N` 的 → 找不到。

**修复**：
- `org_lwjgl_system_linux_DynamicLinkLoader.c`：4 个函数都拆成
  「先存结果 → `ART_NO_TAILCALL()` 屏障 → 再 return」
- `build_lwjgl.sh` 加 `-fno-optimize-sibling-calls` 双保险
- 验证：反汇编变成 `bl` + 完整栈帧（`stp x29,x30` / `ldp` / `ret`）

> ⚠️ **通用坑**：liblwjgl.so 里任何 `return 库函数(...)` 形式的 JNI 入口都可能被尾调用。
> 对 namespace 敏感的主要是 `dlopen`（`dlsym`/`dlclose` 走 handle，不敏感），但值得记住。

### 根因 3：shim 缺 21 个 `GLFW$Functions` 必需符号

修完 namespace 后继续推进，抛：

```
java.lang.NullPointerException: A required function is missing: glfwInitAllocator
```

**根因**：`org.lwjgl.glfw.GLFW$Functions.<clinit>` 用
`APIUtil.apiGetFunctionAddress(FunctionProvider, String)`（**必需**语义，不是 Optional）
为每个字段取地址。它共需 **125 个**函数，而 shim 只导出 107 → 缺 21 个。

**提取差集的方法（可复用）**：

```bash
# 1) 必需函数清单
javap -p -c -cp . 'org.lwjgl.glfw.GLFW$Functions' \
  | grep -oE 'String glfw[A-Za-z0-9_]+' | sed 's/String //' | sort -u > needed.txt
# 2) shim 导出
llvm-nm -D --defined-only libglfw.so | grep -oE 'glfw[A-Za-z0-9_]+$' | sort -u > shim.txt
# 3) 差集（应为空）
comm -23 needed.txt shim.txt
```

缺的 21 个：

| 类别 | 函数 |
|---|---|
| joystick / gamepad（13） | `glfwJoystickPresent` `glfwGetJoystickAxes` `glfwGetJoystickButtons` `glfwGetJoystickHats` `glfwGetJoystickName` `glfwGetJoystickGUID` `glfwGetJoystickUserPointer` `glfwSetJoystickUserPointer` `glfwSetJoystickCallback` `glfwJoystickIsGamepad` `glfwGetGamepadName` `glfwGetGamepadState` `glfwUpdateGamepadMappings` |
| window（5） | `glfwGetWindowFrameSize` `glfwGetWindowTitle` `glfwIconifyWindow` `glfwSetWindowContentScaleCallback` `glfwSetWindowMaximizeCallback` |
| monitor（2） | `glfwGetGammaRamp` `glfwSetGammaRamp` |
| misc（1） | `glfwInitAllocator` |

**修复**（`glfwshim/glfw_sdl.c` 末尾追加）：joystick/gamepad 全打桩返回 0/NULL；
`glfwIconifyWindow` 转发 `SDL_MinimizeWindow`；`glfwGetWindowTitle` 返回稳定的 `""`（避免 MC NPE）；
两个 callback 用字段保存旧值。**重编后导出 128 个，`comm -23` 差集为空。**

## 附带坑：Surface 生命周期（接 MC 时必须处理）

`SDL_GL_CreateContext` 会**一直等** Surface；而 Android 上 App 一失焦，Surface 就销毁。
表现为 `glfwCreateWindow` **卡死且无日志**（若真失败，shim 会 `setError` 打 "GLFW error"；
没有这条就说明是卡在等 Surface）。

排查依据（每次启动都复现）：

```
18:03:08.5 surfaceCreated() / onWindowFocusChanged(): true
18:03:10.3 onWindowFocusChanged(): false    ← 约 1.8s 后失焦（用户切走 App）
18:03:10.4 onPause() → surfaceDestroyed()
```

对策（探针已做）：
- `getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)`
- 探针延迟 3000ms → **500ms**，抢在 Surface 存活窗口内跑完
- **接 MC 时要设计真正的前台保活/暂停恢复策略**（MC 需要长时间渲染）

## 本轮改动文件

```
lwjglbuild/patch/MultiReleaseTextDecoding.java         ★ 新增（4 处单参数 + 坏地址防御）
lwjglbuild/patch/MemoryUtil.java                       ★ 加 isBogusAddress + 两处防御
libs-patched/org.lwjgl_lwjgl_3.4.1_unsafe.jar          注入上两个类 → rebuild_libs.sh → libsdex
lwjglbuild/org_lwjgl_system_linux_DynamicLinkLoader.c  ★ 拆 tail call（4 个函数）
lwjglbuild/android_shim.c                              ★ 保留 probeDlopen 诊断（裸 dlopen 全套证据）
build_lwjgl.sh                                         ★ 加 -fno-optimize-sibling-calls
glfwshim/glfw_sdl.c                                    ★ 追加 21 个必需函数（107 → 128 导出）
mcprobe/src/.../GlfwProbeActivity.java                 ★ KEEP_SCREEN_ON + 500ms + 30 帧
```

## 已知小问题（不影响功能）

- `glfwGetMonitorName` 返回 `1`（SDL3 在该设备上 `SDL_GetDisplayName` 就给数字），MC 一般不依赖
- `glfwGetPlatform` 返回 X11（`0x00060005`），Android 上不准确；MC 不检查平台
- shim 多导出 3 个非必需符号：`glfwGetGLVersion`、`glfwSetContextCreationAPI`、`glfwWindowHintString_`（无害）

# ★★ 第三条进展：MobileGlues 打通 + GL 类路径修复

> 用户提供了 **`/sdcard/FCL`（Fold Craft Launcher 数据目录）** 作为已验证的参考实现 ——
> FCL 已经成功在 Android 上跑通了 MC 26.2 的渲染，其日志是重要对照基准。

## 一、FCL 的实测日志（基准）

```
Backend library: LWJGL version 3.4.1-snapshot+SNAPSHOT
[ANGLE] Warning: No environment variable VULKAN_PTR! Will load libvulkan.
EGLBridge: Binding to OpenGL ES
Using graphics backend OpenGL, using drivers: 4.0.0 MobileGlues 2.0.0
Using graphics device: Adreno (TM) 830 | ANGLE | Vulkan 1.3.284 | OpenGL ES 3.2 (Swung0x48, BZLZHH, Tungsten)
GLFW: Set size for window 490571018240, width=2258, height=1080
Reloading ResourceManager: vanilla
Created: 1024x1024x0 minecraft:textures/atlas/gui.png-atlas
```

**关键结论：FCL 走的是 OpenGL + MobileGlues 路线（不是 Vulkan）**，且内部再套 ANGLE。

## 二、FCL 的运行时库（可直接借用）

```
/data/app/*/com.fcl.plugin.mobileglues*/lib/arm64/libmobileglues.so   6.12 MB  ← 已验证版
/data/app/*/com.fcl.plugin.mobileglues*/lib/arm64/libmobileglues_info_getter.so
/data/app/*/com.tungsten.fcl*/lib/arm64/
    libEGL_angle.so  libGLESv2_angle.so        ← ANGLE 后端
    libOSMesa_8.so   libOSMesa_81.so            ← 软件 GL 兜底
    libgl4es_114.so  libng_gl4es.so  libglapi.so ← 老翻译层
    libSDL2.so libSDL3.so libopenal.so
    libandroidnsbypass.so liblinkerhook.so libbytehook.so  ← 它靠 hook 劫持库加载
    libpojavexec.so                           ← 基于 PojavLauncher 架构
```

**注意**：我们工程里原来那份 `libmobileglues.so` 是 **5.97 MB（较旧）**，
已换成 FCL 的 **6.12 MB** 版本（NEEDED 只有 `libandroid/liblog/libm/libdl/libc`）。

## 三、集成方案（已验证可用，不需要 hook、不需要改 SDL3 源码）

`libmobileglues.so` **同时导出 `eglGetProcAddress` 和完整桌面 GL 符号**，
所以一个库可以同时顶替 `libEGL.so` 和 `libGLESv2.so`。

SDL3 的 `SDL_egl.c` 会读两个 hint（已确认源码）：
- `SDL_HINT_OPENGL_LIBRARY`（392 行）
- `SDL_HINT_EGL_LIBRARY`（462 行）

两处加载失败都会**自动回退**到系统库，所以设置是安全的。

**实现（`glfwshim/glfw_sdl.c`）**：在 `glfwInit()` 里用 `dladdr` 定位自身目录，
把同目录的 `libmobileglues.so` 设进这两个 hint：

```c
static void android_set_mobileglues_hints(void) {
    Dl_info info;
    char path[512];
    if (!dladdr((void *)android_set_mobileglues_hints, &info) || !info.dli_fname) return;
    /* 取自身路径的目录 + "/libmobileglues.so" */
    SDL_SetHint(SDL_HINT_EGL_LIBRARY, path);
    SDL_SetHint(SDL_HINT_OPENGL_LIBRARY, path);
}
```

**实测结果（与 FCL 逐字对上）**：

```
glGetString addr = 0x70a7d8f510
GL_VERSION  = 4.0.0 MobileGlues 2.0.0
GL_RENDERER = Adreno (TM) 830 | OpenGL ES 3.2
GL_VENDOR   = Swung0x48, BZLZHH, Tungsten
```

差别：我们没套 ANGLE，直接走 Adreno 原生 GLES 3.2（少一层翻译）。
若遇特性缺失，可再把 `libEGL_angle.so`/`libGLESv2_angle.so` 加进来。

## 四、GL 类路径的三个坑（`org.lwjgl.opengl.GL`）—— 已全部修复并验证

> **验证结果**：
> ```
> GL.createCapabilities OK —— GL 类路径已打通
> GL11.GL_VERSION  = 4.0.0 MobileGlues 2.0.0
> GL11.GL_RENDERER = Adreno (TM) 830 | OpenGL ES 3.2
> PROBE OK
> ```
> 这是 MC 的 `GlBackend`/`GlDevice` 将来完全一样的调用路径。

探针里用 `glfwGetProcAddress` + `JNI.invokePP` 直调 `glGetString` 可以验证 GL，
但 **MC 用的是 LWJGL 的 `GL` 类**，那条路还有三道坎：

### 坑 1：`GL.<clinit>` 会自动探测 GLX/WGL/EGL

```
java.lang.IllegalStateException: There is no OpenGL context management API available.
  at org.lwjgl.opengl.GL.create(GL.java:116)
  at org.lwjgl.opengl.GL.<clinit>(GL.java:79)
```

`GL.<clinit>` 里有 `if (!Configuration.OPENGL_EXPLICIT_INIT.get(false)) create();`，
而 Android 上 GLX/WGL 都不存在。

**修**：`System.setProperty("org.lwjgl.opengl.explicitInit", "true")`。

> ⚠️ **必须放在 `onCreate` 最前面**：`org.lwjgl.system.Configuration` 的取值在**类初始化时
> 就缓存进实例字段 `state`**（`get()` 只返回 `this.state`），放在 `runProbe` 里已经太晚。

### 坑 2：`liblwjgl_opengl.so` 必须自己编

`GL.<clinit>` 第 40 行会 `Library.loadSystem(..., "lwjgl_opengl")`。

**编法**（185 个 C，排除 WGL/GLX —— 它们要 Windows/X11 头）：

```bash
CC -shared -fPIC -O2 -DLWJGL_LINUX -I. \
  -I$JAVA_HOME/include -I$JAVA_HOME/include/linux \
  -I../vendor/lwjgl3/modules/lwjgl/opengl/src/main/c \
  -o liblwjgl_opengl.so \
  $(ls ../vendor/lwjgl3/modules/lwjgl/opengl/src/generated/c/*.c | grep -vE 'WGL|GLX')
# => 546 KB
```

**关键机制**：生成 C 里的取函数方式不是每库私有 TLS，而是

```c
#define tlsGetFunction(index) (uintptr_t)((void **)(*__env)->reserved3)[index]
```

即 **JNIEnv 的 reserved3 槽位**，跨库共享 —— 所以 `liblwjgl_opengl.so` 可以独立编译，
不需要链接 `liblwjgl.so`。

### 坑 3：`ThreadLocalUtil.setFunctionMissingAddresses` 写只读内存

```
Fatal signal 11 (SIGSEGV), code 2 (SEGV_ACCERR), fault addr 0x71f3e20de8
  #00 art::Unsafe_putLongJJ        ← 注意：这是「单参数版」putLong(J,J)，不是符号扩展问题
  #06 MemoryUtil.memPutAddress
  #10 ThreadLocalUtil.setFunctionMissingAddresses
  #14 GL.create
```

`setFunctionMissingAddresses` 把 abort 表指针写进**全局 `JNINativeInterface` 的 `reserved3`**，
而 **ART 把该结构放在只读内存** → `SEGV_ACCERR`（写只读页）。

真正生效的槽位表是 `setCapabilities()` 里 `setupEnvData()` 建了 JNIEnv **可写副本**之后
才写进副本的 `reserved3`。

**修**（新增 `lwjglbuild/patch/ThreadLocalUtil.java`）：把 `setFunctionMissingAddresses`
改成 **no-op**。代价仅是没有 current context 时调 GL 不再 abort 而是可能段错误，
正常渲染路径（总是先 makeCurrent）不涉及。

> **注意区分**：`Unsafe_putLongJJ(_JNIEnv*, _jobject*, long, long)` 是**单参数版**
> `putLong(long address, long value)`，两个 long 分别是地址和值 —— **不是**根因 1 里
> 那个 32 位符号扩展问题。双参数版会是 `putLong(Object, long, long)`（三个参数）。

## 五、资产来源（已解决）

`/sdcard/FCL/.minecraft/` 有完整资源：

```
assets/indexes/32.json          ← 对应 26.2 的 assets 索引（32）
assets/objects/                 ← 实际资源对象
versions/26.2/26.2.jar          ← 39,193,383 字节（与我们 26.2-client.jar 同尺寸）
versions/26.2/options.txt       ← FCL 跑通时的设置，可参考
libraries/                      ← 完整依赖
```

真跑 MC 时需要把 `assets` 按索引解到 APK 的 assets 目录（或改成读外部目录）。

---

# ★★★ 第四条进展：MC 的 GlDevice 在 ART 上创建成功

> 这是接手以来最关键的一步：**官方 MC 26.2 的渲染设备在 Android ART 上初始化成功。**

## 实测输出

```
RenderSystem.initRenderThread OK
RenderSystem.initBackendSystem OK
MC GlBackend 实例化 OK, name=OpenGL
MC 窗口 = 0x70a8c7f750
MC GlDevice 创建成功: DeviceInfo[
  name=Adreno (TM) 830 | OpenGL ES 3.2,
  vendorName=Swung0x48, BZLZHH, Tungsten,
  driverInfo=4.0.0 MobileGlues 2.0.0,
  backendName=OpenGL,
  limits=DeviceLimits[maxAnisotropy=1, minUniformOffsetAlignment=32,
                      maxTextureSize=16384, maxColorAttachments=8, ...],
  features=DeviceFeatures[multiDrawDirectSeparate=true, persistentMapping=true, ...],
  underlyingExtensions=[GL_ARB_buffer_storage, GL_ARB_vertex_attrib_binding,
                        GL_ARB_direct_state_access],
  type=OTHER]
PROBE OK
```

## MC 渲染初始化的正确顺序（`Main.java:264` + `Minecraft.<init>`）

```java
RenderSystem.initRenderThread();                    // 主线程登记为渲染线程（只一次）
RenderSystem.initBackendSystem();                   // 初始化 GLFW / 后端 / 时间源
backend.setWindowHints();                           // 3.3 core hints
Window window = new Window(...);                     // 用 hints 建窗
backend.createDevice(window.handle(), ...);          // -> new GlDevice -> GL.createCapabilities()
```

**四步缺一不可，踩过的坑：**

| 缺失 | 现象 |
|---|---|
| 没 `initRenderThread()` | `IllegalStateException: Rendersystem called from wrong thread`（`GlStateManager._getString`） |
| 没 `initBackendSystem()` | 同上（渲染线程未登记） |
| 没配 EGL 路径 | `There is no OpenGL context management API available.` |

> `initRenderThread()` 只能调用一次（`renderThread != null` 会抛异常）。

## GL 初始化的正确配置（不需要 patch GL 类）

```java
// onCreate 最前面（Configuration 的值在类初始化时就缓存，晚了无效）
String dir = getApplicationInfo().nativeLibraryDir;
System.setProperty("org.lwjgl.opengl.contextAPI", "EGL");
System.setProperty("org.lwjgl.egl.libname", dir + "/libmobileglues.so");
System.setProperty("org.lwjgl.librarypath", dir);
```

`GL.<clinit>` 自动 `create()` → `loadEGL()` → dlopen MobileGlues → 用它的
`eglGetProcAddress`/`glXGetProcAddress` 取全部 GL 函数 → 写入 `JNIEnv->reserved3` 槽位
→ `liblwjgl_opengl.so` 的 JNI 通过 `tlsGetFunction(index)` 间接调用。

**这条路径和 MC 完全一致**，所以 MC 不需要任何 GL 类 patch。

## 本轮修掉的 shim 真 bug

1. **`glfwGetError` 是占位实现，永远返回 `0x10005`**

   ```c
   int glfwGetError(const char **description) {
       if (description) *description = NULL;
       return 0x00010005;   /* 永远报错 */
   }
   ```

   MC 的 `Window.checkGlfwError()` 一读就抛 `GLFW error before init: [0x10005]`，
   导致 `initBackendSystem()` 直接失败。

   **修**：实现真正的错误状态（`sErrorCode` / `sErrorDesc`），`setError` 记录、
   `glfwGetError` **读后即清**（标准 GLFW 语义），`glfwInit` 成功时清残留。

2. **SDL3 的 Android 后端只支持一个窗口**

   ```
   SDL_CreateWindow FAILED: Android only supports one window
   ```

   探针先建了自己的窗，MC 再建就失败。真跑 MC 时只建一个窗，所以不是问题，
   但**任何"先探针后 MC"的调试流程都要先 `glfwDestroyWindow` 释放**。

## 诊断 shim 的小技巧

`libglfw.so` 链接了 `-llog`，可以直接在 shim 里打日志定位问题：

```c
#include <android/log.h>
#define SHIMLOG(...) __android_log_print(ANDROID_LOG_INFO, "GLFWSHIM", __VA_ARGS__)
```

`SDL_GetError()` 是关键 —— 很多失败只有它说得清（比如上面那个单窗口限制）。

---

# ★★★★ 第五条进展：assets 通道打通（内容实读验证）

> 用户定的架构：**加「所有文件访问权限」，游戏/资源目录定死在 `/sdcard/.minecraft`。**

## 一、权限与路径

`AndroidManifest.xml` 加：

```xml
<uses-permission android:name="android.permission.MANAGE_EXTERNAL_STORAGE"/>
<uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE" android:maxSdkVersion="32"/>
<uses-permission android:name="android.permission.WRITE_EXTERNAL_STORAGE" android:maxSdkVersion="29"/>
<!-- application 上 -->
android:requestLegacyExternalStorage="true"
```

**debug 版用 root 直接授权即可，不需要用户点设置：**

```bash
su -c "appops set com.example.mcart MANAGE_EXTERNAL_STORAGE allow"
su -c "appops get com.example.mcart MANAGE_EXTERNAL_STORAGE"   # 确认 allow
```

授权后 app 能读 `/sdcard`（含 `/sdcard/FCL`、`/sdcard/.minecraft`）。
未授权时是 `Permission denied`（实测用 app uid `ls` 会失败）。

## 二、两块资源来源（都验证过）

### (1) 外部大资源：`/sdcard/.minecraft/assets`

**MC 原生就支持 objects 哈希结构，不需要解压成 `minecraft/...` 目录树：**

```java
// GameConfig.FolderData
public Path getExternalAssetSource() {
    return this.assetIndex == null ? this.assetDirectory.toPath()
         : IndexedAssetSource.createIndexFs(this.assetDirectory.toPath(), this.assetIndex);
}
```

`IndexedAssetSource.createIndexFs()` 读 `assets/indexes/<index>.json`，用
`LinkFileSystem.builder().put(logicalPath, objects/xx/hash)` 建**虚拟文件系统**。

**所以只需要：**
```
/sdcard/.minecraft/assets/
├── indexes/32.json      ← 从 FCL 复制
└── objects/xx/hash      ← 从 FCL 复制（471 MB / 5057 文件）
```

复制脚本 `copy_assets.py`（只挑 index 引用的文件，不是整个 1.4 GB）：

```bash
su -c "python3 copy_assets.py 32 /sdcard/.minecraft/assets"
# DONE copied=5057 skipped=0 missing=0 total_entries=5057
```

**注意**：`/sdcard` 是 FUSE，**硬链接和符号链接都不可用**（root 也不行，`Operation not permitted`），
只能真复制。

### (2) APK 内 jar 资源（shader / models / blockstates 等）

**关键发现**：26.2 的 shader **不在** 外部 assets 里（`32.json` 里 shader 相关条目为 **0**），
而在 `26.2-client.jar` 自己的 `assets/` 里：

```
26.2-client.jar: assets/ 共 11175 个文件，仅 5.52 MB
  含 assets/.mcassetsroot 标记 + assets/minecraft/shaders/core/*.vsh|*.fsh（84 个）
```

MC 用 `VanillaPackResourcesBuilder.pushJarResources()` 通过 `Class.getResource` 找 `.mcassetsroot`
定位 jar 内资源根。**dex 化之后 jar 资源不在 APK 里，所以必须把 jar 的 assets 单独打进去：**

```bash
cd mcprobe/assets && unzip -o -q ../../26.2-client.jar 'assets/*'
# => mcprobe/assets/assets/minecraft/... + .mcassetsroot
# pack.sh 会把 assets/assets 复制成 APK 的 assets/
```

**验证结果（实测）**：

```
外部 assets 目录 exists=true
LinkFileSystem 建立: /
外部 lang/zh_cn.json exists=true → 实读 = 562116 字节        （与源文件一致）
外部 panorama_0.png exists=true  → 实读 = 547239 字节, PNG魔数=OK
APK .mcassetsroot = jar:file:.../base.apk!/assets/.mcassetsroot
```

## 三、踩到的坑

### 坑 1：`Files.size()` 在 LinkFSPath 上返回 0

`Files.exists()` 为 true 但 `Files.size()` 返回 0，而实际文件有 562116 字节。
**不要用 `Files.size()` 判断 assets 是否正常**，用 `Files.readAllBytes()` 实读内容验证。

### 坑 2（✅ 已解决）：`LinkFSPath.resolve(String)` 抛 AbstractMethodError

```
java.lang.AbstractMethodError: abstract method "java.nio.file.Path
  java.nio.file.Path.resolve(java.lang.String)"
  on receiver java.lang.Class<net.minecraft.server.packs.linkfs.LinkFSPath>
```

- Android 与 JDK 的 `Path.resolve(String)` **都是 default 方法**（已用 javap 对比确认），所以不是接口差异
- `LinkFSPath` 源码只实现了 `resolve(Path)`，依赖 default 的 `resolve(String)`
- d8 之后这个 default 方法解析失败
- **探针里的绕法**：用 `fsRoot.resolve(lfs.getPath("..."))`（走 `resolve(Path)`）
- **但 MC 自己的代码大量使用 `resolve(String)`**，所以必须补丁
- **已修复**：在 `make_patches.py` 里加第 8 处补丁，给 `LinkFSPath` 显式实现
  `resolve(String)` / `resolveSibling(Path)` / `resolveSibling(String)` /
  `startsWith(String)` / `endsWith(String)`（`Path` 的 5 个 default 方法）

**补丁流程**（完整、可复用）：

```bash
python3 make_patches.py                      # 生成 patch/src

export JAVA_HOME=/data/data/com.termux/files/usr/lib/jvm/java-21-openjdk
AJ=/data/data/com.termux/files/usr/opt/android-sdk/platforms/android-36/android.jar
CP="mc65.jar:$AJ:$(ls libs26/*.jar | tr '\n' ':')"
rm -rf patch/out && mkdir -p patch/out
javac -nowarn -cp "$CP" -d patch/out $(find patch/src -name '*.java')
# => 27 个 class

cd patch/out && zip -q -r ../../mcpatched.jar net/minecraft

./rebuild_mc.sh                              # d8 -> mcdex2（几分钟）
```

**验证**：`javap -p -cp mcpatched.jar net.minecraft.server.packs.linkfs.LinkFSPath`
应能看到 `resolve(String)` / `resolveSibling(...)` / `startsWith(String)` / `endsWith(String)`。

**实测结果**：`resolve(String)` 直接可用，`lang/zh_cn.json` 实读 562116 字节、
`panorama_0.png` 实读 547239 字节（PNG 魔数正确）。

### 坑 3：纹理路径要用真实存在的

`minecraft/textures/block/stone.png` 在 26.2 的 index 里**不存在**（版本改了命名/结构）。
验证时先查 index 里实际有什么：

```bash
python3 -c "
import json
d=json.load(open('/sdcard/.minecraft/assets/indexes/32.json'))
print([k for k in d['objects'] if k.startswith('minecraft/textures/')][:5])
"
# => minecraft/textures/gui/title/background/panorama_0.png 等
```

## 四、当前完整状态

| 层 | 状态 |
|---|---|
| MC Java 逻辑层（Bootstrap） | ✅ |
| MemoryUtil / Unsafe | ✅ |
| LWJGL native 加载 | ✅ |
| GLFW shim（128 导出） | ✅ |
| GL 翻译层 MobileGlues 2.0.0 | ✅ `GL_VERSION = 4.0.0` |
| `org.lwjgl.opengl.GL` | ✅ MC 原生 EGL 路径 |
| **MC `GlBackend`/`GlDevice`** | ✅ `DeviceInfo` 完整 |
| **assets（外部 + APK 内）** | ✅ 内容实读验证 |
| `LinkFSPath.resolve(String)` | ✅ 已 patch（第 8 处补丁） |
| 完整 `Minecraft` 实例 / 主循环 | ❌ 待做 |

---

# ★★★★★ 第六条进展：Minecraft 实例创建成功

## 实测

```
Bootstrap.bootStrap OK in 8538 ms (bg)
GameConfig 构造 OK
Util.startTimerHackThread OK
★★★ Minecraft 实例创建成功！
PROBE OK
```

进程存活，音频也起来了：

```
openal: [ALSOFT] (II) Initialized backend "oboe"
openal: [ALSOFT] (II) Added "oboe" for playback / capture
```

**这是官方 MC 26.2 的 `net.minecraft.client.Minecraft` 构造完成** —— 窗口、GL 设备、
资源管理、音频、Realms、F3 调试屏等全部初始化成功。

## 启动顺序（最终验证可用的）

```java
// 1) 后台线程（主线程会 ANR）
Class.forName("net.minecraft.SharedConstants").getMethod("tryDetectVersion").invoke(null);
Class.forName("net.minecraft.server.Bootstrap").getMethod("bootStrap").invoke(null);

// 2) 回到主线程
GLFW.glfwInit();                       // shim 里顺带设 MobileGlues 的 EGL hint
GLFW.glfwCreateWindow(...);
RenderSystem.initRenderThread();       // 只能调一次
RenderSystem.initBackendSystem();
GlBackend backend = new GlBackend();
backend.setWindowHints();
long win = GLFW.glfwCreateWindow(...);
GpuDevice dev = backend.createDevice(win, shaderSource, debugOptions, loader);

// 3) 完整实例
GameConfig cfg = new GameConfig(...);
Util.startTimerHackThread();
Minecraft mc = new Minecraft(cfg);      // ★ 成功
```

## 逐条攻克的 Android / ART 差异（按遇到顺序）

| # | 问题 | 修法 |
|---|---|---|
| 1 | `Bootstrap.bootStrap()` 在主线程 → **ANR**（`Input dispatching timed out` 5s） | 移到后台线程，跑完再 post 回主线程 |
| 2 | manifest `android:vmSafeMode="true"` **禁用 JIT** → Bootstrap 38s | 改 `false`，Bootstrap 降到 **5~8s** |
| 3 | `java.lang.management.ManagementFactory`（JMX）不存在 | patch `DebugEntryMemory` / `DebugEntryDetailedMemory` 改用 `Runtime` |
| 4 | `Files.readString/writeString`（Java 11 API）不存在 | patch `RealmsPersistence` 用 `readAllBytes`/`write` |
| 5 | `LinkFSPath` 的 `Path` **default 方法**全部 `AbstractMethodError` | 补丁显式实现 `resolve(String)` / `resolveSibling(*)` / `startsWith(String)` / `endsWith(String)` / **`iterator()`** |
| 6 | `SecurityException: missing INTERNET permission` | manifest 加 `INTERNET` + `ACCESS_NETWORK_STATE` |
| 7 | `UnsatisfiedLinkError: libopenal.so` | 从 FCL 复制 `libopenal.so`（依赖只有系统库） |

## ★ 一个必须记住的教训：zip 注入要包含 `com/`

我一开始用

```bash
cd patch/out && zip -q -r ../../mcpatched.jar net/minecraft
```

结果 `com/mojang/**`（如 `RealmsPersistence`）的补丁**根本没进 jar**，
改了源码却毫无效果，白排查一轮。**正确做法**：

```bash
cd patch/out && zip -q -r ../../mcpatched.jar com net
```

验证方式：`javap -p -c -cp mcpatched.jar <类>` 直接看字节码里是 `readString` 还是 `readAllBytes`。

## 当前 `make_patches.py` 共 11 处补丁

```
 1 Util（jar FileSystemProvider / 泛型）
 2 StringRepresentable（擦除）
 3 FileAccessProvider（ScopedValue -> ThreadLocal）
 4 ModifyContent（ScopedValue -> ThreadLocal）
 5 Language.parseTranslations（资源兜底）
 6 Language.loadFromJson（null 流）
 7 Language.getVisualOrder（sink 重写）
 8 LinkFSPath（Path default 方法 + iterator）
 9 DebugEntryMemory（ManagementFactory -> Runtime）
10 DebugEntryDetailedMemory（同上）
11 RealmsPersistence（Files.readString/writeString）
```

## 下一个缺口

```
nativeloader: Load liblwjgl_stb.so ... dlopen failed: library "liblwjgl_stb.so" not found
```
`stb` 用于图像/字体解码，**需要在 `build_lwjgl.sh` 里把 LWJGL 的 stb 模块也编进来**
（`modules/lwjgl/stb/src/generated/c/*.c`）。

---

# ★★★★★★ 第七条进展：Minecraft 实例稳定运行（无崩溃）

## 实测（最终状态）

```
Bootstrap.bootStrap OK in ~5-9 s (bg)
GameConfig 构造 OK
Util.startTimerHackThread OK
★★★ Minecraft 实例创建成功！
PROBE OK
进程存活，无崩溃，MC 侧无错误日志
```

音频也起来了：`openal: [ALSOFT] (II) Initialized backend "oboe"`

## 本轮新增：`liblwjgl_stb.so`（图像/字体解码）

MC 构造后会异步加载纹理，需要 LWJGL 的 stb 模块。**它踩了三个坑**：

### 坑 1：缺 `lwjgl_malloc.h` 的 include 路径

```bash
-I../vendor/lwjgl3/modules/lwjgl/core/src/main/c
```

### 坑 2：`org_lwjgl_stb_STBVorbis.c` 自己 `#include "stb_vorbis.c"`

不要单独再编 `stb_vorbis.c`，否则 `duplicate symbol: stb_vorbis_*`。

### 坑 3（★ 最难）：`org_lwjgl_malloc` 的 **GOT 条目未解析** → 解 PNG 必崩

现象：

```
Fatal signal 11 (SIGSEGV), fault addr 0x0000000000000000
  #01 pc ... liblwjgl_stb.so (stbi__parse_png_file+856)
  #02 pc ... liblwjgl_stb.so (stbi__load_main+452)
  #03 pc ... liblwjgl_stb.so (stbi__load_and_postprocess_8bit+56)
```

反汇编 `stbi__parse_png_file+856`：

```asm
ldr x8, [x8, #0xd68]    ; 取 GOT 条目（外部符号地址）
ldr x8, [x8]            ; 解引用取函数指针  ← 这里 x8 是 0 -> SEGV at 0x0
blr x8
```

`0xd68` 对应 `.got` 里 `org_lwjgl_realloc` 的 GLOB_DAT 条目。LWJGL 的 stb 用
「可覆盖内存管理函数」：`STBI_MALLOC -> org_lwjgl_malloc(sz)`，这些是**函数指针全局变量**，
正常发行版由 `Java_<Lib>_setupMalloc` 在 Java 侧填入。

**试过但不够的修法**：写 `__attribute__((constructor))` 给 6 个指针都赋
`malloc/calloc/realloc/free/aligned_alloc` —— 日志确认赋值成功
（`setup: malloc=0x... realloc=0x...` 全部非 NULL），**但照样崩**，
因为崩的是 GOT 条目解析，不是指针值。

**最终修法**：`sed` 复制一份 `org_lwjgl_stb_STBImage.c` 改掉三个宏，彻底不碰那套指针：

```bash
sed -e 's|#define STBI_MALLOC(sz)    org_lwjgl_malloc(sz)|#define STBI_MALLOC(sz)    malloc(sz)|' \
    -e 's|#define STBI_REALLOC(p,sz) org_lwjgl_realloc(p,sz)|#define STBI_REALLOC(p,sz) realloc(p,sz)|' \
    -e 's|#define STBI_FREE(p)       org_lwjgl_free(p)|#define STBI_FREE(p)       free(p)|' \
    $STB/src/generated/c/org_lwjgl_stb_STBImage.c > stb_image_android.c
```

编译（注意 `-lm -llog`，`pow` 来自 libm，日志要 `-llog`）：

```bash
CC -shared -fPIC -O2 -DLWJGL_LINUX -I. -I$CORE \
   -I$JAVA_HOME/include -I$JAVA_HOME/include/linux -I$STB/src/main/c \
   -o liblwjgl_stb.so stb_image_android.c \
   $(ls $STB/src/generated/c/*.c | grep -v "STBImage.c") -lm -llog
```

结果：**无崩溃，进程存活**。
（`stb_malloc_init.c` 那个 constructor 已经不需要了，可以保留也可以删。）

## 当前完整 native 库清单（mcprobe/lib-arm64/）

| 文件 | 来源 | 作用 |
|---|---|---|
| `libglfw.so` | 自写 GLFW→SDL3 shim（128 导出） | GLFW API |
| `liblwjgl.so` | 自编（含 ThreadLocalUtil） | JNI 派发 / libc / libffi / Upcalls |
| `liblwjgl_opengl.so` | 自编（185 C） | GL 类 |
| `liblwjgl_stb.so` | 自编（stb，改系统 malloc） | 图像/字体解码 |
| `libmobileglues.so` | FCL 6.12 MB | GL 4.0 翻译层 |
| `libSDL3.so` | 自编 3.5.0 | 窗口/事件 |
| `libffi.so` | Termux 3.8.0 | 回调 |
| `libopenal.so` | FCL | 音频（oboe 后端） |

## 完整环境配置（onCreate 最前面）

```java
String nd = getApplicationInfo().nativeLibraryDir;
System.setProperty("org.lwjgl.opengl.contextAPI", "EGL");
System.setProperty("org.lwjgl.egl.libname", nd + "/libmobileglues.so");
System.setProperty("org.lwjgl.librarypath", nd);
System.loadLibrary("lwjgl");
```

## 剩下的唯一一步

`new Minecraft(cfg)` 只做**构造**，还没进入 `run()` 主循环。真正出画面需要：

```java
mc.run();   // 或者按 MC 的 tick/render 循环驱动
```

预计会第一次触发**真实 shader 编译**（`GlslCompiler` → MobileGlues 的 GLSL 3.3 core），
这是整条链上最后一个未验证点。

---

## 下一步

1. **接 MC 的 `Window`/`GlBackend`**：把探针的验证路径换成 MC 的真实初始化路径
2. **桌面 GL 3.3 core 的缺口**：MC 请求 `CLIENT_API=OPENGL_API` + 3.3 core
   （`GlBackend.java` 里 `glfwWindowHint(139265,196609)` / `(139266,3)` / `(139267,3)`），
   而 Android 只有 GLES → 用 **MobileGlues**
   （`RotatingartLauncher/app/src/main/jniLibs/arm64-v8a/libmobileglues.so`，依赖仅
   `libandroid/liblog/libm/libdl/libc`，可直接打包）；同目录还有 `libOSMesa.so`（软 GL 兜底）
3. 资源加载 / 真正出画面

---

## 0. 一句话现状

**MC 的 Java 逻辑层早已在 ART 上跑通；LWJGL 内存层、libffi、SDL3、GLFW shim 全部就绪。
本轮把最后的卡点（dlopen 加载 libglfw.so 失败）彻底解决 —— `GlfwProbeActivity` 现在
`PROBE OK`：GLFW 初始化 / 建窗 / GLES3.2 上下文 / swap 30 帧 / 显示器信息全绿，
且走的是 MC 将来完全一样的纯 LWJGL 路径（不预加载 glfw、不借 native 探针）。
下一步是接 MC 的 `Window`/`GlBackend` 与桌面 GL。**

> 本轮完整复盘见下方「★ 本轮更新」章节，**接手请先读它**。

---

## 1. 目标

把官方 Minecraft 26.2 的 `client.jar` 直接跑在 Android ART 上（不经过桌面 JVM），渲染层用 SDL3。
不是重写 MC（77 万行），是**移植**：

```
26.2-client.jar ──d8──► dex(12 个)
                      + liblwjgl.so（自编，Android 版，含 libffi/Upcalls/DynamicLinkLoader）
                      + libffi.so   （Termux 3.8.0，只依赖 libc）
                      + libSDL3.so  （自编 3.5.0，官方 androidbuildlibs.sh）
                      + libglfw.so  （自写：GLFW API → SDL3 的 shim）
                      → APK → ART
```

---

## 2. 环境事实（直接用，别再摸索）

| 项 | 值 |
|---|---|
| 工作目录 | `/data/data/com.termux/files/home/mc-work`（530MB+） |
| 设备 | OnePlus PLQ110 / Android 16 / SDK 36 / arm64-v8a，**有 root（KernelSU，`su -c` 可用）** |
| JDK | `/data/data/com.termux/files/usr/lib/jvm/java-21-openjdk` |
| SDK | `/data/data/com.termux/files/usr/opt/android-sdk` |
| `android.jar` | **用 `platforms/android-36/android.jar`**（SDL3 的 `Display.getHighestHdrSdrRatio()` 需要 API36；35 编不过） |
| aapt2/apksigner | `build-tools/35.0.0`（**只有这个是 arm64 版**，其余是 x86_64 跑不了） |
| NDK | `.../ndk/29.0.14206865`（clang 最高 android35，用 `aarch64-linux-android28-clang`） |
| d8 | `/data/data/com.termux/files/usr/share/java/d8.jar`（9.2.4） |
| APK | `mcprobe/mc-on-art.apk`（29MB），包名 `com.example.mcart`，debuggable + vmSafeMode |

**文件系统限制**：`/data/data` 上 **`link()` 被 SELinux 拒绝**，所以
"原子写入"类工具（写临时文件再 hardlink 替换）会失败。**改文件一律用 `cat > f <<'EOF'` 或 python 写。**

**看崩溃栈**：
```bash
su -c "logcat -d -b crash -t 300" | grep -E "signal 11|#[0-9]+ pc" | grep -v libart
# ART 崩在 JNI 里时 #0 是 art::Unsafe_putXxx 之类，往上翻解释器帧能看到完整 Java 调用链
su -c "ls -t /data/tombstones/tombstone_[0-9]* | head -1"   # tombstone 里 x0..x29 很有用
```

---

## 3. 目录结构（本轮之后）

```
mc-work/
├── 26.2-src-e/                MC 反编译源码（51MB，MC 侧补丁的唯一来源）
├── 26.2-client.jar            官方 jar
├── mc65.jar / mcpatched.jar   降版本副本 / 打补丁的 jar
├── libs26/ libs-patched/      133 个依赖库（原始 / 补丁后）
├── mcdex2/ libsdex/           现成 dex（3 + 8 个）
│
├── lwjglbuild/                ★ liblwjgl.so 构建目录
│   ├── patch/MemoryUtil.java          ★ ART 适配版（4 处关键修复）
│   ├── patch/MemoryUtilTunables.java  ★ 整体替换版
│   ├── ffi-include/                   Termux 的 ffi.h + ffitarget.h（3.8.0）
│   ├── org_lwjgl_system_*.c           从 LWJGL 源码取的生成 C（见 build_lwjgl.sh）
│   ├── org_lwjgl_system_linux_DynamicLinkLoader.c
│   ├── org_lwjgl_system_Upcalls.c / common_tools.c / SharedLibraryUtil.c
│   ├── android_shim.c                 自写：nGetDirectBufferAddress
│   └── liblwjgl.so                    产物（555KB）
├── build_lwjgl.sh             ★ 重编 liblwjgl.so
├── rebuild_libs.sh            libs-patched → d8 → libsdex（4~5 分钟）
│
├── glfwshim/
│   ├── glfw_sdl.c             ★ GLFW→SDL3 shim（916 行，107 个导出符号）
│   └── sdl_probe.c            SDL3 可用性验证程序
│
├── mcprobe/                   ★ 探针 APK 工程
│   ├── src/org/libsdl/app/*.java       直接从 SDL 源码复制的 12 个胶水类（无需改）
│   ├── src/com/example/mcart/          探针 Activity（见下）
│   ├── lib-arm64/                      liblwjgl.so / libffi.so / libSDL3.so / libglfw.so
│   ├── build_dex.sh           ★ src → dex
│   ├── pack.sh                ★ aapt2 + zip + apksigner
│   └── mc-on-art.apk
│
├── vendor/lwjgl3/             LWJGL 3.4.1 浅克隆（取生成 C + Java 源码）
├── vendor/SDL/                SDL 3.5.0 浅克隆（含构建产物）
└── HANDOVER.md / HANDOVER-NEXT.md（本文）/ PATCHES.md / PORTING.md
```

**探针 Activity 一览**（manifest 里都注册了）：

| Activity | 用途 | 结果 |
|---|---|---|
| `MainActivity` | MC Bootstrap 全量初始化 | ✅ 早前就通过 |
| `DiagActivity` | LWJGL 内存层分步诊断 | ✅ 全绿 |
| `Diag2/3/4Activity` | Unsafe 能力探测 / buffer 语义 / libffi | ✅ |
| `SdlProbeActivity` | 纯 SDL3：init+建窗+GLES3.2+swap | ✅ |
| `GlfwProbeActivity` | **走 LWJGL 的 GLFW 绑定**调 shim | ❌ 当前卡点 |

---

## 4. 已完成并验证的四块

### 4.1 LWJGL 内存层 ✅（上轮的卡点，本轮彻底解决）

原崩溃 `MemoryUtil.write8 写 0xa85e2727f0` 实际是**五个 ART 语义差异叠加**：

| # | 问题 | 修复 |
|---|---|---|
| 1 | `memAddress0` 有 **8 个重载**，上轮只改了通用 `memAddress0(Buffer)`；而 `memAddress(ByteBuffer)` 调的是 `memAddress0(ByteBuffer)` → JNI 补丁**根本没生效**。`ADDRESS=-1` 让 `Unsafe.getLong(buf,-1)` 读到对象头 → 返回固定的垃圾 `0xa85e262800` | 7 个具体重载 + 通用版全改 `nGetDirectBufferAddress(buffer)` |
| 2 | ART 的 `Unsafe.putXxx(Object o, long off, v)` 在 `o==null` 时把 off 当**字段偏移**并做 **32 位符号扩展**（`0xacbc8550`→`0xffffffffacbc8550`）→ 必崩 | 36 处全改成**单参数绝对地址版** `Unsafe.putXxx(ptr, v)`（ART 上实测可用，>2GB 也能用） |
| 3 | 默认分配器走 jemalloc，在 `MemoryUtil` 自己的 clinit 里递归初始化（jemalloc 的 clinit 又要 MemoryStack→nmemAlloc） | 新增内置 `AndroidAllocator`，直接调 `LibCStdlib.nmalloc/ncalloc/nrealloc/nfree/naligned_alloc` |
| 4 | `NewDirectByteBuffer(addr,n).asIntBuffer()` 继承 **BIG_ENDIAN**，与 `memGetInt` 的本机小端不符 → 表现成"地址对、值不对"，极难定位 | 所有 `wrapBufferShort/Char/Int/Long/Float/Double` 和泛型 `slice/duplicate` 都加 `.order(NATIVE_ORDER)` |
| 5 | `MemoryUtilTunables.<clinit>` 读 ART 没有的 `Unsafe.ARRAY_BYTE_BASE_OFFSET` → `NoSuchFieldError`，`memSet/memCopy` 全废 | 整体替换成 `patch/MemoryUtilTunables.java`：地址区间走 `LibCString.nmemset/nmemcpy`，数组↔native 走 NIO view buffer |

**验证证据**（`files/diag.txt`）：
```
1b memAddress == nGetDirectBufferAddress      ← 一致
1c memPutInt/memGetInt = 0x11223344
3e UTF8("lwjgl") -> lwjgl                    ← 原崩溃点
5c memGetInt=123456   5d IntBuffer.get(0)=123456   ← 字节序一致
ALLOCATOR = MemoryUtil$AndroidAllocator
```
外加 40 轮 `memAllocInt` 地址↔视图双向检查 **0 不一致**；并验证 `NewDirectByteBuffer` 包的内存
**GC 不会 free**（5 轮 full GC + 再 malloc 64 块，内容不变）。

### 4.2 libffi ✅

- 用 Termux 的 `/data/data/com.termux/files/usr/lib/libffi.so`（SONAME=`libffi.so`，**只依赖 libc.so**）
- LWJGL 3.4.1 头文件期望 libffi **3.5.0**，Termux 装的是 **3.8.0**。
  做法：**用 Termux 的 3.8.0 头文件**编 LWJGL 的 3 个 `org_lwjgl_system_libffi_*.c`，
  并动态链接 3.8.0 的库 —— 这样 C 侧与运行库自洽，Java 侧自动适配。
- 实测：`FFI_VERSION_STRING=3.5.0`(Java 常量)、`FFICIF.SIZEOF=32`、
  `ABI=0/NARGS=4/ARG_TYPES=8/RTYPE=16/BYTES=24/FLAGS=28`、`ffi_prep_cif -> FFI_OK`。

### 4.3 SDL3 ✅（最大进展）

用官方脚本自编，不用 Termux 那套（那套还要 `libandroid-shmem` + `libiconv`）：

```bash
cd vendor/SDL
export ANDROID_NDK_HOME=/data/data/com.termux/files/usr/opt/android-sdk/ndk/29.0.14206865
export PATH=$ANDROID_NDK_HOME:$PATH
./build-scripts/androidbuildlibs.sh APP_PLATFORM=android-28 -j4
# => build/android/lib/arm64-v8a/libSDL3.so (2.1MB, SONAME=libSDL3.so)
```

依赖全是系统库：`libdl/libGLESv1_CM/libGLESv2/libOpenSLES/liblog/libandroid/libc/libm/libstdc++`

**实测在 ART 上完整工作**：
```
SDL_Init(VIDEO)      = 1            ← SDL3 返回 bool，1=成功（这是最后一个坑）
video driver         = android
displays             = 1, 1080x2378
SDL_CreateWindow     = 0xb4000070b7c42a00
SDL_GL_CreateContext = 0xb4000070b7d00f00
GL_VERSION           = OpenGL ES 3.2 V@0800.74 (Adreno 830)
swap ok                             ← glClear + swap 真的画上屏了
```

**三个必须知道的坑**（都在 `SdlProbeActivity` 里解决了，新代码照抄即可）：

1. `SDLActivity.getLibraries()` 默认返回 `{"SDL3","main"}`，`libmain.so` 不存在 →
   `onCreate` 提前 return → 弹 "dlopen failed: libmain.so" → **`SDL.setupJNI()` 永不执行**
   → `SDL_Init` 虽返回成功但 video driver 为 null、CreateWindow 返回 NULL。
   **修**：`@Override getLibraries() { return new String[]{"SDL3"}; }`
2. `SDLMain.run()` 在 `SDLActivity.main()` 返回后会 `finish()` 掉 Activity
   （日志 `activity finished by caller: ... SDLMain.run:2477`）→ Surface 销毁 → 后续 SDL_Init 失败。
   **修**：`@Override main() { while(running) Thread.sleep(500); }`
3. **SDL3 全 API 返回 bool**（不是 SDL2 的 0/-1）。`SDL_Init`/`SDL_GetDisplayBounds` 都是 true=成功。

### 4.4 libglfw.so （GLFW→SDL3 shim）✅ 已写完

`glfwshim/glfw_sdl.c`，916 行。**已编译通过，导出 107 个 `glfw*` 符号，
覆盖 MC 实际用到的全部 59 个（用 `comm` 对比过，一个不缺）。**

**关键机制**：LWJGL 3.4 的 GLFW 绑定**不是 JNI native 方法**，而是
`APIUtil.apiGetFunctionAddress(GLFW.getLibrary(), "glfwInit")` + `JNI.invokeX` 派发，
所以 shim 导出的是**原始 C 符号名**（`glfwInit`），不是 `Java_org_lwjgl_glfw_GLFW_glfwInit`。

实现了：生命周期、窗口、GL 上下文、事件分发（`SDL_PollEvent` → 17 种 GLFW 回调）、
显示器、剪贴板、输入查询、**GLFW keycode → SDL scancode 映射表**
（GLFW 是 USB HID 风格 `GLFW_KEY_A=65/ESCAPE=256`，SDL 完全不同）。
Mojang 定制 IME 扩展（`glfwSetPreeditCallback` 等）SDL3 无对应功能，打桩返回 0/NULL。

编译命令：
```bash
NDK=/data/data/com.termux/files/usr/opt/android-sdk/ndk/29.0.14206865
$NDK/toolchains/llvm/prebuilt/linux-aarch64/bin/aarch64-linux-android28-clang \
  -shared -fPIC -O2 -o glfwshim/libglfw.so glfwshim/glfw_sdl.c \
  -Ivendor/SDL/include -Lvendor/SDL/build/android/lib/arm64-v8a -lSDL3 -llog
```

---

## 5. ★ 当前卡点：`GLFW.<clinit>` 加载 libglfw.so 失败

### 5.1 现象

`GlfwProbeActivity` 跑到 `GLFWErrorCallback.create(...)`（触发 `GLFW.<clinit>`）就 SIGSEGV：

```
#06 MultiReleaseTextDecoding.decodeUTF8
#10 MemoryUtil.memUTF8Safe
#14 DynamicLinkLoader.dlerror          ← 在 error 分支里！
#18 LinuxLibrary.loadLibrary
#22 LinuxLibrary.<init>
#26 APIUtil.apiCreateLibrary
#30 Library.loadNative
#34 Library.loadNativeFromLibraryPath
#38 Library.loadNative
#46 GLFW.<clinit>
#56 GLFWErrorCallback.set
#60 GlfwProbeActivity.runProbe
```
`fault addr 恒为 0xffffffffb37e9190`（又是 32 位符号扩展的味道）。

### 5.2 分析

**`dlopen` 本身失败了**，于是走 `LinuxLibrary.loadLibrary` 的 error 分支去拿 `dlerror()`，
而 `dlerror()` 返回的指针被当成有效地址传给 `memUTF8Safe` → 读它 → SIGSEGV。

**崩溃把真正的失败原因完全盖住了。** 这是当前唯一的障碍。

### 5.3 下一步（按优先级，第 1 步做完就能看到真因）

**第 1 步：让 `dlerror()` 不再崩**

最省事的两种改法（挑一个）：

- **A（推荐）**：写一个 patch 版 `org.lwjgl.system.linux.DynamicLinkLoader`，
  把 `dlerror()` 改成不调 native（直接返回 `""` 或 Java 侧拼的字符串），
  路径 `lwjglbuild/patch/DynamicLinkLoader.java`，编译后**和 MemoryUtil 一样注入**
  `libs-patched/org.lwjgl_lwjgl_3.4.1_unsafe.jar`（用 `zip -q -r` 覆盖 `org/lwjgl/system/linux/DynamicLinkLoader*.class`）。
- **B（更小）**：在 `patch/MemoryUtil.java` 的 `memUTF8Safe(long)` 前加地址合法性检查
  （`address >>> 48 != 0 || address < 0x1000` 就返回 null）。

改完 `./rebuild_libs.sh`（4~5 分钟）→ `cd mcprobe && ./pack.sh` → 装机 → 重跑 `GlfwProbeActivity`。

**第 2 步：根据真实错误决定修法**

大概率是这两类之一：

- **依赖解析失败**：`libglfw.so` NEEDED `libSDL3.so`。虽然 SDL3 已被 `SDLActivity` 用
  `System.loadLibrary` 加载，但**通过 `dlopen` 绝对路径加载的库**，Android linker 对依赖
  的 namespace 解析有额外限制。
  **备选修法**：把 shim 改成**不 NEEDED SDL3** —— 用 `dlopen("libSDL3.so")` + `dlsym` 在运行时取
  所有 SDL 函数指针（写个 `SDL_FUNCS(X)` 宏表，一次性解决）。
- **路径/命名问题**：如果 `org.lwjgl.librarypath` 指向 `nativeLibraryDir` 后
  `dlopen("<dir>/libglfw.so")` 仍失败，就反过来：**先 `System.loadLibrary("glfw")` 拿到句柄，
  再自己 `apiCreateLibrary`** 绕过 LWJGL 的路径查找逻辑。

**第 3 步：跑通后接 MC**

`GlfwProbeActivity` 全绿后，就可以把 MC 的 `Window`/`GlBackend` 初始化接上来。
注意 **MC 请求的是桌面 OpenGL 3.3 core**（`GlBackend.java` 里
`glfwWindowHint(139265,196609)` = CLIENT_API/OPENGL_API，`(139266,3)`/`(139267,3)` = 3.3），
而 Android 只有 GLES —— 需要 **MobileGlues**（现成的：
`/data/data/com.termux/files/home/RotatingartLauncher/app/src/main/jniLibs/arm64-v8a/libmobileglues.so`，
依赖只有 `libandroid/liblog/libm/libdl/libc`，**可直接打包**）。
同目录还有 `libOSMesa.so`（软件 GL 兜底）、`libGLESv2_angle.so`。

---

## 6. 常用命令速查

```bash
cd /data/data/com.termux/files/home/mc-work
export JAVA_HOME=/data/data/com.termux/files/usr/lib/jvm/java-21-openjdk
export PATH=$JAVA_HOME/bin:$PATH

# 重编 liblwjgl.so（改 C 或换 .c 列表后）
./build_lwjgl.sh && cp lwjglbuild/liblwjgl.so mcprobe/lib-arm64/

# 重编补丁版 Java 类并注入 jar
javac -nowarn -cp libs26/org.lwjgl_lwjgl_3.4.1_unsafe.jar:libs26/org.jspecify_jspecify_1.0.0.jar \
      -d lwjglbuild/patch/out lwjglbuild/patch/MemoryUtil.java
cd lwjglbuild/patch/out && zip -q -r /data/data/com.termux/files/home/mc-work/libs-patched/org.lwjgl_lwjgl_3.4.1_unsafe.jar org/lwjgl/system

# 重建依赖 dex（4~5 分钟，后台跑）
./rebuild_libs.sh

# 重编 libglfw.so
NDK=/data/data/com.termux/files/usr/opt/android-sdk/ndk/29.0.14206865
$NDK/toolchains/llvm/prebuilt/linux-aarch64/bin/aarch64-linux-android28-clang \
  -shared -fPIC -O2 -o glfwshim/libglfw.so glfwshim/glfw_sdl.c \
  -I vendor/SDL/include -L vendor/SDL/build/android/lib/arm64-v8a -lSDL3 -llog
cp glfwshim/libglfw.so mcprobe/lib-arm64/

# 打包 + 装机 + 跑
cd mcprobe && ./build_dex.sh && ./pack.sh
su -c "cp mc-on-art.apk /data/local/tmp/mc.apk && pm install -r /data/local/tmp/mc.apk"
su -c "am force-stop com.example.mcart; logcat -c"
su -c "am start -W -n com.example.mcart/.GlfwProbeActivity"
sleep 12; su -c "logcat -d | grep -E 'GLFWPROBE|V/SDL'"

# 探针结果文件（Diag*Activity 写的）
su -c "cat /data/data/com.example.mcart/files/diag.txt"
```

---

## 7. 已经踩过、别再踩的坑

1. **不要在 `/data/data` 上指望原子写入**（hardlink 被 SELinux 拒）。
2. **`android.jar` 必须用 android-36**（SDL3 的 Java 胶水用到了 API36 的 `Display.getHighestHdrSdrRatio`）。
3. **`javac` 不要用 `-source/-target 8 -bootclasspath android.jar`** —— lambda 会因为找不到
   `LambdaMetafactory` 报错。直接 `javac -cp android.jar:...`（生成 v65，d8 9.2.4 吃得下）。
4. **multirelease 的 Java 11 实现已经从 `org.lwjgl_lwjgl_3.4.1_unsafe.jar` 里摘掉了**
   （`META-INF/versions/11/org/...` 4 个类），因为它们在 ART 上有问题。别再加回去。
5. **`System.loadLibrary("lwjgl")` 必须先调**：纯 `dlopen` 不会触发 `JNI_OnLoad`，
   于是 `common_tools.c` 里的 `jvm` 为 NULL，`Upcalls.getEnv()` 直接崩。
6. **不要自己预先 `System.loadLibrary("glfw")`** —— 会让 LWJGL 自己的加载器状态不一致
   （报 "Failed to locate library: libglfw.so" 但其实已经加载了）。让 LWJGL 自己加载。
7. **ART 的 `Unsafe`**：单参数绝对地址版可用；`(Object,long)` 版在 `o==null` 时不可用（32 位符号扩展）。
   `jdk.internal.misc.Unsafe` 被 hidden-api 挡住，反射拿不到。
8. **`ByteBuffer.allocateDirect` 的 `GetDirectBufferAddress` 行为**：`memAddress` 拿到的是对的，
   但注意 buffer 默认大端，凡是要当 native 内存用都得 `.order(nativeOrder())`。
9. **MC 的反编译源码在 `26.2-src-e/`，是 51MB 的宝贵资产，不要删**（`make_patches.py` 依赖它）。
10. **SDL3 的 Java 胶水类不能改**（`org/libsdl/app/*.java` 12 个），
    `build-scripts/check_android_jni.py` 验证过它们与 native 的 JNI 签名一致。

---

## 8. 如果 dlopen 死活搞不定（备选路线）

**不用 LWJGL 的 GLFW 绑定**：Android 上直接用 `android_native_app_glue` 或
JNI + `ANativeWindow_fromSurface` + EGL 自己起 GLES 上下文，然后把 MC 的 `Window` 类
（`com.mojang.blaze3d.platform.Window`）直接用 `make_patches.py` 打补丁换成自实现。
代价：要改 MC 的 Window/GlBackend，但避开了 LWJGL 加载器这一整层不确定性。

这条路的可行性判断依据：MC 对 GLFW 的用法很窄（建窗、GL 上下文、输入回调、显示器信息），
没有用到 GLFW 的高级特性。

---

# 第九轮（关键突破：真的进主菜单了）

这一轮把「紫黑缺失纹理 + ANR 杀进程 + 进世界崩」全部打通。**首次到达主菜单并接受点击输入。**

## 9.1 jar 内 classpath 资源全部丢失 —— 三个源头

d8 只把 `.class` 编进 dex，**jar 里的所有非 class 资源在 APK 里都不存在**。踩了三处：

### (a) MC 的客户端资源（models / blockstates / textures / shaders / data）
症状：
```
java.io.FileNotFoundException: minecraft:textures/colormap/foliage.png
  FoliageColorReloadListener.prepare
```
根因链：
- `VanillaPackResourcesBuilder.ROOT_DIR_BY_TYPE` 用 `Class.getResource("/assets/.mcassetsroot")`
  探测，APK 里确实有这个 URL（`jar:file:.../base.apk!/assets/.mcassetsroot`）。
- 但 `FileSystemUtil.safeGetPath()` 走 `Paths.get(jar:...)` → `ProviderNotFoundException: Provider "jar" not found`
  → `ROOT_DIR_BY_TYPE` 为空 → `pushJarResources()` 变成空操作。
- 于是 vanilla pack 的 `pathsForType[CLIENT_RESOURCES]` 里**只有外部 index（objects）那一条**，
  index 32 里没有任何 colormap 条目 → 整个 jar 侧资源不可达。

**修法（patch 12）**：`VanillaPackResourcesBuilder.pushJarResources()` 改成先看
`/sdcard/.minecraft/assets-jar`，存在则 `pushUniversalPath(jarRoot)`（jar 根布局：`assets/`、`data/`、`pack.png`）。

**准备数据**（新脚本 `extract_jarres.py`，把 jar 的非 class 资源解出来）：
```bash
cd /data/data/com.termux/files/home/mc-work
su -c "rm -rf /sdcard/.minecraft/assets-jar"
python3 extract_jarres.py mcpatched.jar /sdcard/.minecraft/assets-jar
# => 19497 个文件 / 14.22 MB，含 assets/.mcassetsroot、textures/colormap/*.png、
#    shaders/core/*.vsh|fsh（61 个）、models/、blockstates/、font/*.json、data/minecraft/**
```
注意：**不要用 `mcprobe/assets/assets/` 那份**（旧的手工拷贝，缺 `data/`、`pack.png`、`.mcassetsroot`）。
root shell 里没有 python3 → 用 termux 自己的 `python3` 直接写 /sdcard（可以写）。

### (b) ICU4J 数据文件
症状：进加载界面后 `com.ibm.icu.text.Bidi.<init>` → `ExceptionInInitializerError` →
`ICUBinary.readHeader` NPE（`UBiDiProps` 读不到 `ubidi.icu`）。
ICU4J 用 `Class.getResourceAsStream("com/ibm/icu/impl/data/icudata/<name>")` 找数据。

**修法**：把 `libs26/com.ibm.icu_icu4j_78.3.jar` 的 `com/ibm/icu/impl/data/**`（4119 文件 26.77 MB）
原样解到 **APK 根**（`stage/com/...`）。已写进 `mcprobe/pack.sh`（条件生成 `mcprobe/icures/`）。
- `.res` 4073 个 21.48 MB（locale bundle）+ 70 个 `.icu/.nrm/.brk/.dict` 约 4.5 MB。
- 只缺 `ubidi.icu` 也能过 Bidi，但 `nfc.nrm`/`ucadata.icu` 等后续还会用到，干脆全带上。

### (c) log4j / slf4j provider 注册
症状：`SLF4J(W): No SLF4J providers were found` + `Log4j API could not find a logging provider`
→ **MC 日志整条链路 NOP**，`/sdcard/.minecraft/logs/` 空，排查全靠猜。

**修法**：新脚本 `gen_logres.py` 从所有 jar 收集并**合并** `META-INF/services/*`
（同名文件必须合并，APK 里只能有一份），外加
`META-INF/org/apache/logging/log4j/core/config/plugins/Log4j2Plugins.dat`。
`pack.sh` 会 `cp -r ../logres/META-INF stage/`。
→ 之后 MC 日志出现在 logcat 的 `I/System.out` 标签下（无文件 appender，不写 logs/）。

### (d) 附带发现：`log4j2.xml` 不在 jar 里
`mcpatched.jar` 根目录没有任何 xml，MC 用代码配置 log4j；所以只能从 logcat 看日志。

## 9.2 紫黑纹理的真正原因：`ClientBootstrap.bootstrap()` 没被调用

日志（log4j 修好后才看得到）：
```
Worker-Main-3 ERROR Failed to parse atlas definition minecraft:atlases/blocks.json in pack vanilla
java.lang.IllegalStateException: Unknown element id: minecraft:single;
                                   Unknown element id: minecraft:directory
  at SpriteSourceList.load(SpriteSourceList.java:73)
```
`SpriteSources.ID_MAPPER` 是 `LateBoundIdMapper`，靠 `SpriteSources.bootstrap()` 注册
（`single` / `directory` / `filter` / `unstitch` / `paletted_permutations`）。
谁调它？**`net.minecraft.client.main.Main.main()` 第 160 行 `ClientBootstrap.bootstrap();`**

探针绕过了 `Main.main`，直接 `new Minecraft(cfg)`，所以从没注册 → 所有 atlas 解析失败 →
所有图集为空 → 全部 sprite 退化成 missing texture（**紫黑棋盘**）。

**修法（探针）**：bootStrap 之后补两步：
```java
net.minecraft.client.ClientBootstrap.bootstrap();   // ItemModels/SpecialModelRenderers/
                                                    // ItemTintSources/SelectItemModelProperties/
                                                    // ConditionalItemModelProperties/
                                                    // RangeSelectItemModelProperties/
                                                    // SpriteSources/DialogScreens/
                                                    // InputControlHandlers/DialogBodyHandlers
net.minecraft.server.Bootstrap.validate();
```
实测 9 ms 完成，之后 atlas 错误数 = 0。

> 教训：**`Main.main()` 里凡是 `bootStrap()` 之后的初始化步骤，探针都必须自己复现。**
> 目前已知：`tryDetectVersion` / `bootStrap` / `ClientBootstrap.bootstrap` / `validate`。

## 9.3 ANR 根因：mc.run() 占了 Java 主线程

症状：任何触摸/按键 5 秒后
```
ANR in com.example.mcart: Input dispatching timed out ... Waited 5000ms for MotionEvent(...)
Killing ... reason=6 (ANR)  -> 进程被系统杀死
```
也遇到过 `KeyEvent scanCode=114`（音量键）触发同款。

根因：`mc.run()` 是死循环，跑在 `Handler.postDelayed` 的主线程回调里，
主线程永远不返回 `Looper.loop()` → Android 输入事件（ViewRootImpl → InputEventReceiver）
永远拿不到分发 → 超时 ANR。

**修法（探针）**：整个 `runProbe()` 挪到独立线程：
```java
new Handler(Looper.getMainLooper()).postDelayed(
        () -> new Thread(this::runProbe, "mc-game").start(), 150);
```
主线程空出来 → SDLSurface 收输入 → 塞进 SDL 事件队列 → 游戏线程 `glfwPollEvents` 取走。
实测点击事件能正常到达 MC（`GLFWMouseButtonCallbackI.callback` → `MouseHandler.onButton`）。

**顺带**：`RenderSystem.initRenderThread()` 因此登记的是 `mc-game` 线程，所有 GL 调用都在这条线程，自洽。

## 9.4 进世界崩：一批 JDK 新 API Android 没有

Android 16（SDK 36）其实挺新，`Path.of`、`ScopedValue` 都有；但下面这几个没有。

| 症状 | 位置 | 修法 |
|---|---|---|
| `NoSuchMethodError: Class.getModule()` | `JvmProfiler.INSTANCE`（Java 9+，评估 `jdk.jfr` 模块）| patch 13：直接 `INSTANCE = new NoOpProfiler()`，并删 `import jdk.jfr.FlightRecorder` |
| `NoSuchMethodError: Files.readString/writeString`（Java 11）| `AccountProfileKeyPairManager` / `QuickPlayLog` / `SnbtDatafixer` / `SummaryReporter` / `FileFixerUpper` | patch 14：换成 `readAllBytes` + `new String(bytes, UTF_8)` / `Files.write(p, s.getBytes(UTF_8), opts...)` |
| `NoSuchMethodError: Path.resolve(String, String[])`（JDK 22+）| `Identifier.resolveAgainst:153`、`NativeLibrariesBootstrap:166` | patch 15/16：拆成两次 `resolve(String)` |

`JvmProfiler` 的触发点是**创建/进入世界**：`Minecraft.doWorldLoad → IntegratedServer → MinecraftServer
→ ReloadableServerResources → Commands → JvmProfiler.<clinit>`。
所以「主菜单能进、一点创建世界就崩」就是这个。

`Path.resolve(String, String...)` 的触发点更早：`MinecraftServer.<init> → SavedDataStorage.getDataFile
→ Identifier.resolveAgainst`。**这是当前最新的阻塞点（patch 15 应该已经解决，待实测）。**

反编译源码里还有两处编译不过的残留（顺手修进 patch 14/16）：
- `FileFixerUpper`：`NbtOps` 是 `DynamicOps<Tag>` 不能当 `DynamicOps<CompoundTag>`；lambda 里 `ex`/`exx` 实际变量名是 `e`
- `NativeLibrariesBootstrap.CapturingPrintStream`：`super(...)` 之前有局部变量赋值（Java 不允许）

## 9.5 其它已知但暂时无害的日志噪音

| 日志 | 原因 | 处理 |
|---|---|---|
| `Couldn't resolve path to vanilla assets / ProviderNotFoundException: Provider "jar" not found` | `ROOT_DIR_BY_TYPE` 静态初始化（patch 12 之前的旧路径）| 无害，patch 12 会兜住 |
| `File /data/.mcassetsroot does not exist in classpath` | `PackType.SERVER_DATA` 的探测 | 无害，assets-jar 里有 `data/` |
| `Error while loading the narrator` / `NoClassDefFoundError: com.sun.jna.NativeLibrary` | 缺 `libjnidispatch.so`（JNA native，dex 打包丢失）| TTS 可选；要修就把 jna 的 `com/sun/jna/android-aarch64/libjnidispatch.so` 塞进 APK |
| `Failed to fetch user properties` 401 / `Realms service error (405)` | 离线账号，没有正版会话 | 无害（离线模式）|
| `Negative index in crash report handler (14/20)` | `SystemReport.preload` | 无害 |

## 9.6 内存：进后台必被 lowmemorykiller 杀

```
lowmemorykiller: Kill 'com.example.mcart' (11807) ... oom_score_adj 910 to free 175952kB rss;
                 reason: device is in direct reclaim and low on memory
handleAppExit ... reason=3 (LOW_MEMORY)
```
MC on ART 常驻 RSS 400~500 MB。**测的时候别切后台**（切后台 → adj 升高 → 直接被回收）。
不是崩溃。

## 9.7 本轮的构建命令（按顺序）

```bash
cd /data/user/0/com.termux/files/home/mc-work

# ① 改过 patch 源码（make_patches.py 或 patch/src）后
python3 make_patches.py
export JAVA_HOME=/data/data/com.termux/files/usr/lib/jvm/java-21-openjdk
export PATH=$JAVA_HOME/bin:$PATH
AJ=/data/data/com.termux/files/usr/opt/android-sdk/platforms/android-36/android.jar
CP="mc65.jar:$AJ:$(ls libs26/*.jar | tr '\n' ':')"
rm -rf patch/out && mkdir -p patch/out
javac -nowarn -cp "$CP" -d patch/out $(find patch/src -name '*.java')   # 应 53 个 class
cd patch/out && zip -q -r ../../mcpatched.jar com net && cd ../..

# ② 改过探针 java 后
mcprobe/build_dex.sh

# ③ d8（约 3~4 分钟）
./rebuild_mc.sh

# ④ 打包 + 装（pack.sh 现在会带上 icu 数据 + log4j services）
mcprobe/pack.sh
su -c "pm install -r -d $PWD/mcprobe/mc-on-art.apk"
su -c "appops set com.example.mcart MANAGE_EXTERNAL_STORAGE allow"
su -c "logcat -c"
su -c "am start -n com.example.mcart/.GlfwProbeActivity"
```

看日志的正确姿势（整机 logcat 太大，必须按 pid 过滤，且要限量）：
```bash
P=$(su -c "pidof com.example.mcart")
timeout 45 su -c "logcat -d -v brief --pid=$P" > /data/data/com.termux/files/home/p.log
grep -E "GLFWPROBE" p.log            # 探针进度
grep -E "^I/System\.out" p.log       # MC 自己的日志（log4j -> stdout -> logcat）
```
`logcat -d` 全量 dump 会超过 90 秒被工具打断，务必用 `--pid` + `timeout`。

## 9.8 补丁清单（当前 16 处）

| # | 文件 | 作用 |
|---|---|---|
| 1 | `Util` | jar FileSystemProvider / 两处反编译泛型 |
| 2 | `StringRepresentable` | 父类构造擦除不一致 |
| 3 | `FileAccessProvider` | ScopedValue（文件修复用）|
| 4 | `ModifyContent` | 同上 |
| 5-7 | `Language` ×3 | 反编译 |
| 8 | `LinkFSPath` | `Path` 的 5 个 default 方法（`resolve(String)` 等）|
| 9-10 | `DebugEntryMemory` / `DebugEntryDetailedMemory` | `ManagementFactory` 不存在 → `Runtime` |
| 11 | `RealmsPersistence` | `Files.readString/writeString` |
| **12** | **`VanillaPackResourcesBuilder`** | **jar 资源改读 `/sdcard/.minecraft/assets-jar`** |
| **13** | **`JvmProfiler`** | **禁 JFR（`Class.getModule()`）** |
| **14** | **5 个文件** | **`Files.readString/writeString`** |
| **15** | **`Identifier`** | **`Path.resolve(String,String...)`** |
| **16** | **`NativeLibrariesBootstrap`** | **同上 + 构造函数上 super 前赋值** |

## 9.9 新增脚本

- `extract_jarres.py <jar> <dest> [prefix...]` —— 解 jar 里非 class 资源（排除 `.class/.dex/META-INF`）
- `gen_logres.py [out]` —— 收集合并所有 jar 的 `META-INF/services/*` + log4j 插件缓存
  （内部 `os.chdir` 到脚本所在目录，可从任意 cwd 调用）

---

# 第十轮（可以玩了：鼠标右键 / 返回键键盘 / 帧率 / 声音）

第九轮之后能进世界正常玩。这轮补的是「可发布」还缺的几块。

## 10.1 鼠标右键没反应 —— 是键号映射错位，不是没绑

用拓展坞的真实鼠标时，右键在 MC 里没反应。根因是 SDL 与 GLFW 的鼠标键编号不同，
而 shim 里只是简单减一：

| | 左 | 中 | 右 | X1 | X2 |
|---|---|---|---|---|---|
| SDL | 1 | 2 | **3** | 4 | 5 |
| GLFW | 0 | **2** | **1** | 3 | 4 |

`int btn = e->button.button - 1;` 于是 SDL 右键(3) 落到 GLFW 2（中键），
中键落到右键上 —— 表现为「右键没用」。

**修法**：`glfwshim/glfw_sdl.c` 加 `sdl_button_to_glfw()` 显式映射（1→0, 2→2, 3→1, 4→3, 5→4）。

顺手加了触摸模式的双指右键（拔掉拓展坞时用）：
- 用 `SDL_EVENT_FINGER_DOWN/UP/CANCELED` 数指头
- 1 指 = 左键，**2 指 = 右键**
- 切到右键时**先把左键抬起**再按右键，避免「边挖边放」
- 右键手势期间忽略 SDL 合成的鼠标按键与移动事件（否则第二根手指会把视角甩飞）

## 10.2 手机返回键呼出系统键盘

要两件事同时做，少一个都不行：
1. `InputMethodManager.showSoftInput()` —— 只是「显示键盘」
2. **`SDL_StartTextInput(window)`** —— SDL 内部有 `text_input_active` 状态，
   不调它的话键盘能弹出来但**不会产生 `SDL_EVENT_TEXT_INPUT`**，
   MC 的文本框（世界种子 / 聊天 / 服务器地址）收不到任何字符

SDL 的 Java 层 `SDLActivity.showTextInput()` 是 public static，但只做第 1 步。

**实现**：
- `glfw_sdl.c` 新增 JNI 入口
  `Java_com_example_mcart_GlfwProbeActivity_nativeSetSoftKeyboard(JNIEnv*, jobject, jboolean)`，
  内部 `SDL_StartTextInput(sWin.sdl)` / `SDL_StopTextInput(sWin.sdl)`
  （SDL3 的签名带 window 参数；它自己会 post `ShowTextInputTask` 到主线程，跨线程安全）
- `GlfwProbeActivity.dispatchKeyEvent()` 吃掉 `KEYCODE_BACK`：
  ```java
  if (event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
      if (event.getAction() == KeyEvent.ACTION_UP && !event.isCanceled()) toggleSoftKeyboard();
      return true;   // 不给 SDL 当按键，也不触发 finish
  }
  ```
  用 `dispatchKeyEvent` 而不是 `onBackPressed`：否则 SDL 先把 BACK 转给 MC，Activity 又要 finish。
- `onCreate` 里要 `System.loadLibrary("glfw")`（JNI 方法需要 Java 侧显式加载；
  LWJGL 是运行时按绝对路径 dlopen 的，两者指向同一文件，句柄相同）

## 10.3 帧率显示（不用改代码）

MC 26.2 的 F3 面板是模块化的，条目配置存在 `debug-profile.json`。
写 `/sdcard/.minecraft/debug-profile.json`：

```json
{"custom": {"minecraft:fps": "alwaysOn"}}
```

`alwaysOn` = 不进 F3 也常驻。左上角出现 `60 fps T: 60 (fifo)`。
条目 ID 见 `DebugScreenEntries`（`minecraft:memory` / `minecraft:tps` / `minecraft:chunk_generation_stats` …）。
`DebugScreenEntryList` 的 `SerializedOptions` 只有两个字段：`profile` 或 `custom`
（给了 `profile` 就忽略 `custom`）。

## 10.4 全屏 / 横屏 / 状态栏

- 顶部出现「Minecraft* Development Version」标题栏 → Activity 加
  `android:theme="@android:style/Theme.NoTitleBar.Fullscreen"`，
  并在 `onCreate` 里 `getWindow().setDecorFitsSystemWindows(false)` +
  `WindowInsetsController.hide(systemBars())`（旧 API 走 `setSystemUiVisibility`）
- **系统栏要在三个时机重复隐藏**：`onCreate` / `onResume` / `onWindowFocusChanged(true)`，
  只做 `onCreate` 会被 SDL/系统刷回来
- manifest 声明 `android:appCategory="game"` + `android:isGame="true"`
  （用户要求：就让系统知道这是游戏，别搞别的 hack）
- 横屏要**两边都设**：
  - manifest `android:screenOrientation="sensorLandscape"`
  - shim 里 `SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight")`
    否则 SDL3 的 `SDLActivity.setOrientationBis()` 在「窗口可缩放 + 无 hint」时
    会请求 `FULL_USER`，跟着系统自动旋转来回翻转，surface 尺寸在 2378x1080/1080x2378 之间跳
  - 另外系统侧锁了 `settings put system accelerometer_rotation 0` + `user_rotation 1`
- 窗口尺寸不要硬编码：`DisplayData` 改成 `getWindowManager().getCurrentWindowMetrics().getBounds()`
  取真实像素（之前写死 1080x2378，横屏后就和 surface 不一致）

## 10.5 fallback 启动入口

manifest 把 `MAIN/LAUNCHER` 从 `MainActivity` 挪到 `GlfwProbeActivity`，
`label` 改成 `Minecraft`，`MainActivity` 只保留 `exported`。
验证：`cmd package resolve-activity --brief com.example.mcart` →
`com.example.mcart/.GlfwProbeActivity`。
注意 `monkey -c LAUNCHER` 启动过一次没起来（并非入口错），`am start` 正常。

## 10.6 ★ 声音：换 OpenAL —— 后端报成功但实际静音

**症状**：完全没声音。

**误导性现象**：FCL 那版 `libopenal.so`（OpenAL Soft 1.25.2，Oboe 后端）日志一路绿灯：
```
[ALSOFT] (II) Initialized backend "oboe"
[ALSOFT] (II) Created device 0xb400007091ef1140, "Oboe Default"
[ALSOFT] (II) Post-start: Stereo, Float32, 48000hz, 480 / 1536 buffer
[ALSOFT] (II) Created context 0xb4000070a107c500
[ALSOFT] (II) Increasing allocated voices to 256
```
看起来全都成功，**但一点声都没有**。

**诊断方法（关键）**：看 AudioFlinger 的实际轨道状态，而不是 OpenAL 日志：
```bash
su -c "dumpsys media.audio_flinger" | grep -E "<pid>/"
```
FCL 版的 track 长这样 —— 增益 `-inf`、就绪帧 `0`：
```
AT::add  F2  656  no  31035/10507  ...  -inf  0  0  0  0  false 00000000 1536  0  f  0  0 ...
```
换成 LWJGL 官方版后是 —— 增益 `0 dB`、`FrmRdy 2658`、`Active=yes`：
```
684  yes  7129/10507  0x000 ... 44100 3 1 0  0 0 0 0 0  false 000378E4  2658  2658  A ...
```
同时输出线程 `Standby: no`。

**修法**：换成 `/data/user/0/com.termux/files/home/RotatingartLauncher/app/src/main/jniLibs/arm64-v8a/libopenal32.so`
（LWJGL 官方 Android OpenAL，**OpenSL ES 后端**），按 LWJGL 默认库名拷成
`mcprobe/lib-arm64/libopenal.so`。

| | FCL 版 | LWJGL 版 |
|---|---|---|
| 后端 | Oboe / AAudio | **OpenSL ES** |
| NEEDED | libdl libm liblog libc | + `libOpenSLES.so` `libstdc++.so` |
| SONAME | libopenal.so | libopenal32.so（dlopen 按文件名走，不影响）|
| 结果 | 建成功但静音 | 正常 |

> 教训：**音频「初始化成功」不等于「有声音」。** 必须核对 AudioFlinger 里轨道的
> `Active` / 就绪帧数 / 增益 dB。`-inf` 增益就是静音。

## 10.7 liblwjgl_stb.so 里还有 3 个文件漏修（闪退）

进世界后 2~3 秒闪退，`reason=5 (APP CRASH(NATIVE))`，tombstone：
```
signal 11 (SIGSEGV), fault addr 0x0   pc 0x0      <- 跳到空指针
#01 stbir__alloc_internal_mem_and_build_samplers+1700
#03 stbir_resize_extended
#05 Java_org_lwjgl_stb_STBImageResize_nstbir_1resize_1uint8_1linear
#15 net.minecraft.client.renderer.GameRenderer.lambda$takeAutoScreenshot$1
```
MC 进世界后自动截图 → `NativeImage.resizeSubRectTo` → `stb_image_resize` →
`STBIR_MALLOC` 宏 → `org_lwjgl_malloc` **函数指针**（dex-only 打包时永远是 NULL）。

之前只修了 `STBImage`，**漏了另外 3 个**：

| 源文件 | 宏 | 影响 |
|---|---|---|
| `STBImage` | `STBI_MALLOC/REALLOC/FREE` | 读 PNG |
| `STBImageResize` | `STBIR_MALLOC/FREE` | **自动截图（本次闪退）** |
| `STBImageWrite` | `STBIW_MALLOC/REALLOC/FREE` | **手动存截图** |
| `STBTruetype` | `STBTT_malloc/free` | 字体 |

**新增脚本**：
- `make_stb_android.py` —— 生成 4 个 `*_android.c`（宏换成系统 `malloc/free/realloc`）
- `build_stb.sh` —— 编译 `liblwjgl_stb.so`

**注意** `build_stb.sh` 里的几个坑：
- `-I vendor/lwjgl3/modules/lwjgl/stb/src/main/c`
- `-I vendor/lwjgl3/modules/lwjgl/core/src/main/c` **和** `.../core/src/main/c/linux`（`LinuxConfig.h` 在这里）
- **必须 `-lm`**：不加会 `dlopen failed: cannot locate symbol "pow"`
- 不要单独编 `stb_vorbis.c`（`org_lwjgl_stb_STBVorbis.c` 已经 include 它）
- 生成的 4 个文件放在 `lwjglbuild/`（不是 `liblwjglbuild/`）

验证：JNI 导出符号 **224 个，与旧库零差异**；`llvm-nm -D -u` 里无残留 `org_lwjgl_` 符号。

## 10.8 内存：largeHeap + KernelSU 模块（未生效，需重启）

实测：
- 设备总 RAM 14.75 GB
- `dalvik.vm.heapsize` = 512m、`dalvik.vm.heapgrowthlimit` = 256m
- manifest 加 `android:largeHeap="true"` 后 `maxMemory` 仍 **512MB**
- `setprop dalvik.vm.heapsize 3072m` 后 `memoryClass` 立刻变 3072MB，
  但 `Runtime.maxMemory()` 还是 512MB —— **ART 在 zygote 启动时算好、fork 时继承**

要 3GB 必须重启。已做 KernelSU 模块开机自动设置：

```
/data/adb/modules/mc_heap_3g/
├── module.prop
└── post-fs-data.sh     # /data/adb/ksu/bin/resetprop dalvik.vm.heapsize 3072m
```

`heapgrowthlimit` 保持系统默认 256m —— 只抬高 `heapsize`，这样只有 `largeHeap`
应用（我们这个）能用 3GB，其他应用照旧受增长上限约束。
**尚未重启验证。**

## 10.9 当前完整刷机/构建流程（第十轮）

```bash
cd /data/user/0/com.termux/files/home/mc-work

# 改过 MC 补丁
python3 make_patches.py && <javac> && cd patch/out && zip -q -r ../../mcpatched.jar com net && cd ../..
./rebuild_mc.sh                 # d8，3~4 分钟

# 改过 lwjgl/stb 原生库
./build_lwjgl.sh                # liblwjgl.so
./build_stb.sh                  # liblwjgl_stb.so（记得 -lm）

# 改过 glfw shim（含鼠标/键盘/软键盘/横屏 hint）
NDK=/data/data/com.termux/files/usr/opt/android-sdk/ndk/29.0.14206865
CC=$NDK/toolchains/llvm/prebuilt/linux-aarch64/bin/aarch64-linux-android28-clang
JH=/data/data/com.termux/files/usr/lib/jvm/java-21-openjdk
cd glfwshim && $CC -shared -fPIC -O2 -o libglfw.so glfw_sdl.c \
  -I/data/data/com.termux/files/home/BongoCatMobile/third_party/sdl3/include \
  -I$JH/include -I$JH/include/linux \
  -L../mcprobe/lib-arm64 -lSDL3 -llog -ldl -lm
cd .. && cp glfwshim/libglfw.so mcprobe/lib-arm64/

# 打包安装
mcprobe/pack.sh
su -c "pm install -r -d $PWD/mcprobe/mc-on-art.apk"
su -c "appops set com.example.mcart MANAGE_EXTERNAL_STORAGE allow"
su -c "am start -n com.example.mcart/.GlfwProbeActivity"
```

shim 编译后务必核对导出符号数（应恒为 128 个 `glfw*`）。

## 10.10 已知遗留

- 3GB 堆：等重启验证
- 输入法被子势收起后，Java 侧 `mKeyboardShown` 状态会不同步（按返回键要先「关」一次）。
  要彻底解决需监听 insets 变化或 `onNativeScreenKeyboardShown` 回调
- 无 on-screen 按钮（跳跃/潜行/背包/右键按钮）；现在靠拓展坞键鼠或双指右键
- 未做音频焦点处理（`AudioHardening background playback would be muted` 说明切后台会被静音）

---

# 第十一轮 ★★ 声音真正的根因：Android 没有 javax.sound.sampled

## 11.1 现象与误导

症状：**完全没声音**，但所有「看起来该报错」的地方都没报错：
- OpenAL 后端初始化成功（Oboe / OpenSL 都试过）
- `SoundEngine - Sound engine started`（打开 log4j DEBUG 后才看到）
- `Playing sound minecraft:music/menu/... for event minecraft:music.menu` 大量刷
- AudioFlinger 里 track `Active=yes`、`FrmRdy 2658`、`Underruns 0`、增益满

**我走的弯路（记录以免重犯）**：
1. 以为 FCL 的 OpenAL（Oboe 后端）有问题 → 换成 LWJGL 官方 `libopenal32.so`（OpenSL ES 后端）
2. 以为音频路由/音量问题 → 查出 `STREAM_MUSIC` 满音量、设备 speaker
3. 以为 MC 没发起播放 → 加了 `log4j2.xml` 才看到它一直在播
4. 以为资源文件坏了 → 写 `al_ogg.c`（stb_vorbis 解码 + OpenAL 播放）验证真实 ogg **完全正常**
5. 差点去换 SDL3 的 AAudio 后端 —— **方向完全错**，后端一直没问题

## 11.2 根因

MC 26.2 的音频系统用 `javax.sound.sampled.AudioFormat` 描述格式：

```java
// JOrbisAudioStream 构造
this.audioFormat = new AudioFormat(this.info.rate, 16, this.info.channels, true, false);
// OpenAlUtil.audioFormatToOpenAl
if (encoding.equals(Encoding.PCM_UNSIGNED) || encoding.equals(Encoding.PCM_SIGNED)) { ... }
// Channel.calculateBufferSize
(int)(seconds * format.getSampleSizeInBits() / 8.0F * format.getChannels() * format.getSampleRate())
```

而 **Android 运行时完全不带 `javax.sound`**：
```
/apex/com.android.art/javalib/core-oj.jar       javax/sound 条目 0
/apex/com.android.art/javalib/core-libart.jar   javax/sound 条目 0
/system/framework/framework.jar                 javax/sound 条目 0
```
`mcpatched.jar` 里也只有**引用**、没有**定义**（dex 的 class_defs 里 0 个 `javax/sound/*`）。

所以解码/上传链路在 `new AudioFormat(...)` 处就断了：
- MC 照样打出 "Playing sound"（那是 `SoundEngine.play()` 里发出的，早于解码）
- 音频流打开后异常退出 → 不会 `close()` → `FallbackResourceManager` 报
  `Leaked resource: 'minecraft:sounds/....ogg'`（**这个 WARN 是决定性线索**，修好后消失）
- OpenAL 收到的是空 buffer → track 活着但输出静音

## 11.3 修法

需要补的 API 面极小，用 dexdump 从 MC dex 里精确提取，只有 6 个成员：

```
AudioFormat.<init>(FIIZZ)V        // (sampleRate, sampleSizeInBits, channels, signed, bigEndian)
AudioFormat.getChannels()I
AudioFormat.getSampleRate()F
AudioFormat.getSampleSizeInBits()I
AudioFormat.getEncoding()Ljavax/sound/sampled/AudioFormat$Encoding;
AudioFormat$Encoding.equals(Object)Z
```

新增 `mcprobe/src/javax/sound/sampled/AudioFormat.java`（自实现，含 `Encoding`
的 `PCM_SIGNED`/`PCM_UNSIGNED`，语义对齐 JDK 版）。

**编译坑**：`javax.sound.sampled` 属于 JDK 的 `java.desktop` 模块，javac 直接编会报
`package exists in another module: java.desktop`。`build_dex.sh` 改成两步：

```bash
# 只编这个类，用 --patch-module 绕开模块归属检查
javac -nowarn --patch-module java.desktop=src -d build/classes \
      src/javax/sound/sampled/AudioFormat.java
# 其余照旧（排除 src/javax）
javac -nowarn -cp "$AJ:$CP:build/classes" -d build/classes \
      $(find src -name '*.java' ! -path 'src/javax/*')
```

验证：
```bash
dexdump -f mcprobe/dex/classes.dex | grep 'Class descriptor.*javax/sound'
# 应看到 AudioFormat 和 AudioFormat$Encoding 两个定义
```

修好后：`Leaked resource` 归零，`Playing sound minecraft:step/grass4 for event
minecraft:block.grass.step` 正常刷，**耳朵里真的有声了**。

## 11.4 诊断工具（都留在仓库里，以后能复用）

| 工具 | 用途 |
|---|---|
| `mcprobe/log4j2.xml` | 放进 APK 根，把 `net.minecraft` 开到 DEBUG。**不加它 MC 只输出 ERROR**，INFO 全被默认配置吞掉，排查等于盲猜 |
| `al_test.c` | 最小 OpenAL 播放测试（440Hz 正弦），验证 libopenal+扬声器链路 |
| `al_ogg.c` | 真实 .ogg → stb_vorbis 解码 → OpenAL 播放，并打印 peak/avg 判断是否静音 |
| shim 里的 `ALSOFT_CONF` / `ALSOFT_LOGLEVEL` setenv | 试着打开 OpenAL 自身日志（这个 LWJGL 构建其实不往 logcat 输出，没成功但仍保留） |

运行方式（以应用 uid，避免 root 的音频权限差异）：
```bash
su -c "run-as com.example.mcart /data/user/0/com.example.mcart/files/al_ogg <ogg路径>"
```
注意 `libopenal32.so` 的 SONAME 是 `libopenal32.so`，链接后运行时要么按这个名字放库，
要么前面用 `-Wl,-rpath,<dir>`。

## 11.5 看音频「有没有真的响」的正确方法

**不要看 `framesWritten`** —— 它只在 `threadLoop_standby` 日志行里出现，
值不变只代表「没发生过 standby」，不代表没数据。

看 track 表里我们进程那一行：
```bash
su -c "dumpsys media.audio_flinger" | grep -E "<pid>/"
```
关注三列：`Active=yes` / `FrmRdy`（就绪帧数，非 0 才在出数据）/ `Underruns`（欠载）。
增益列出现 `-inf` 就是静音。

**注意**：单机模式窗口失焦会自动暂停、Android 也会后台静音应用
（`AudioHardening background playback would be muted`）。测量时务必确认
`dumpsys window | grep mCurrentFocus` 是 `com.example.mcart`，否则测到的全是假的。

## 11.6 遗留

- 3GB 堆仍等重启验证（KernelSU 模块 `mc_heap_3g`）
