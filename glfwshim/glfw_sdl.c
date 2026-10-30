/*
 * libglfw.so —— GLFW 3.4 API 到 SDL3 的 shim（MC on ART 移植专用）
 *
 * LWJGL 3.4 通过 apiGetFunctionAddress 找原始 C 符号（glfwInit 等）并派发，
 * 所以这里导出的是原始 GLFW 符号名，不是 JNI 名字。
 */
#include <SDL3/SDL.h>

#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <android/log.h>
#include <jni.h>

#define SHIMLOG(...) __android_log_print(ANDROID_LOG_INFO, "GLFWSHIM", __VA_ARGS__)
#include <math.h>
#include <stdio.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* GLFW 常量                                                            */
/* ------------------------------------------------------------------ */
#define GLFW_TRUE 1
#define GLFW_FALSE 0
#define GLFW_PRESS 1
#define GLFW_RELEASE 0
#define GLFW_REPEAT 2

#define GLFW_CLIENT_API 0x00022001
#define GLFW_CONTEXT_VERSION_MAJOR 0x00022002
#define GLFW_CONTEXT_VERSION_MINOR 0x00022003
#define GLFW_CONTEXT_REVISION 0x00022004
#define GLFW_CONTEXT_ROBUSTNESS 0x00022005
#define GLFW_CONTEXT_RELEASE_BEHAVIOR 0x00022006
#define GLFW_CONTEXT_NO_ERROR 0x00022007
#define GLFW_OPENGL_PROFILE 0x00022008
#define GLFW_CONTEXT_DEBUG 0x00022009
#define GLFW_CONTEXT_FORWARD_COMPAT 0x0002200A
#define GLFW_OPENGL_API 0x00030001
#define GLFW_OPENGL_ES_API 0x00030002
#define GLFW_NO_API 0

#define GLFW_RED_BITS 0x00021001
#define GLFW_GREEN_BITS 0x00021002
#define GLFW_BLUE_BITS 0x00021003
#define GLFW_ALPHA_BITS 0x00021004
#define GLFW_DEPTH_BITS 0x00021005
#define GLFW_STENCIL_BITS 0x00021006
#define GLFW_SAMPLES 0x0002100D
#define GLFW_SRGB_CAPABLE 0x0002100E
#define GLFW_DOUBLEBUFFER 0x00021010

#define GLFW_RESIZABLE 0x00020003
#define GLFW_VISIBLE 0x00020004
#define GLFW_DECORATED 0x00020005
#define GLFW_MAXIMIZED 0x00020008
#define GLFW_FOCUS_ON_SHOW 0x0002000C

#define GLFW_CURSOR 0x00033001
#define GLFW_CURSOR_NORMAL 0x00034001
#define GLFW_CURSOR_HIDDEN 0x00034002
#define GLFW_CURSOR_DISABLED 0x00034003
#define GLFW_RAW_MOUSE_MOTION 0x00033005

#define GLFW_KEY_LAST 348
#define GLFW_MOUSE_BUTTON_LAST 7

/* ------------------------------------------------------------------ */
/* GLFW key -> SDL_Scancode 映射（GLFW 用 USB HID 风格 keycode）          */
/* ------------------------------------------------------------------ */
static int glfwkey_to_scancode(int key)
{
    if (key >= 65 && key <= 90) {                       /* A..Z */
        SDL_Scancode s[] = {
            SDL_SCANCODE_A, SDL_SCANCODE_B, SDL_SCANCODE_C, SDL_SCANCODE_D, SDL_SCANCODE_E,
            SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_H, SDL_SCANCODE_I, SDL_SCANCODE_J,
            SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_M, SDL_SCANCODE_N, SDL_SCANCODE_O,
            SDL_SCANCODE_P, SDL_SCANCODE_Q, SDL_SCANCODE_R, SDL_SCANCODE_S, SDL_SCANCODE_T,
            SDL_SCANCODE_U, SDL_SCANCODE_V, SDL_SCANCODE_W, SDL_SCANCODE_X, SDL_SCANCODE_Y,
            SDL_SCANCODE_Z };
        return s[key - 65];
    }
    if (key >= 48 && key <= 57) {                       /* 0..9 */
        SDL_Scancode s[] = {
            SDL_SCANCODE_0, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
            SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9 };
        return s[key - 48];
    }
    if (key >= 290 && key <= 314) return SDL_SCANCODE_F1 + (key - 290);   /* F1..F25 */
    if (key >= 320 && key <= 329) return SDL_SCANCODE_KP_0 + (key - 320);
    switch (key) {
        case 32:  return SDL_SCANCODE_SPACE;
        case 39:  return SDL_SCANCODE_APOSTROPHE;
        case 44:  return SDL_SCANCODE_COMMA;
        case 45:  return SDL_SCANCODE_MINUS;
        case 46:  return SDL_SCANCODE_PERIOD;
        case 47:  return SDL_SCANCODE_SLASH;
        case 59:  return SDL_SCANCODE_SEMICOLON;
        case 61:  return SDL_SCANCODE_EQUALS;
        case 91:  return SDL_SCANCODE_LEFTBRACKET;
        case 92:  return SDL_SCANCODE_BACKSLASH;
        case 93:  return SDL_SCANCODE_RIGHTBRACKET;
        case 96:  return SDL_SCANCODE_GRAVE;
        case 256: return SDL_SCANCODE_ESCAPE;
        case 257: return SDL_SCANCODE_RETURN;
        case 258: return SDL_SCANCODE_TAB;
        case 259: return SDL_SCANCODE_BACKSPACE;
        case 260: return SDL_SCANCODE_INSERT;
        case 261: return SDL_SCANCODE_DELETE;
        case 262: return SDL_SCANCODE_RIGHT;
        case 263: return SDL_SCANCODE_LEFT;
        case 264: return SDL_SCANCODE_DOWN;
        case 265: return SDL_SCANCODE_UP;
        case 266: return SDL_SCANCODE_PAGEUP;
        case 267: return SDL_SCANCODE_PAGEDOWN;
        case 268: return SDL_SCANCODE_HOME;
        case 269: return SDL_SCANCODE_END;
        case 280: return SDL_SCANCODE_CAPSLOCK;
        case 281: return SDL_SCANCODE_SCROLLLOCK;
        case 282: return SDL_SCANCODE_NUMLOCKCLEAR;
        case 283: return SDL_SCANCODE_PRINTSCREEN;
        case 284: return SDL_SCANCODE_PAUSE;
        case 330: return SDL_SCANCODE_KP_DECIMAL;
        case 331: return SDL_SCANCODE_KP_DIVIDE;
        case 332: return SDL_SCANCODE_KP_MULTIPLY;
        case 333: return SDL_SCANCODE_KP_MINUS;
        case 334: return SDL_SCANCODE_KP_PLUS;
        case 335: return SDL_SCANCODE_KP_ENTER;
        case 336: return SDL_SCANCODE_KP_EQUALS;
        case 340: return SDL_SCANCODE_LSHIFT;
        case 341: return SDL_SCANCODE_LCTRL;
        case 342: return SDL_SCANCODE_LALT;
        case 343: return SDL_SCANCODE_LGUI;
        case 344: return SDL_SCANCODE_RSHIFT;
        case 345: return SDL_SCANCODE_RCTRL;
        case 346: return SDL_SCANCODE_RALT;
        case 347: return SDL_SCANCODE_RGUI;
        case 348: return SDL_SCANCODE_MENU;
        default:  return SDL_SCANCODE_UNKNOWN;
    }
}

static int scancode_to_glfwkey(SDL_Scancode sc)
{
    for (int k = 32; k <= GLFW_KEY_LAST; k++) {
        if (glfwkey_to_scancode(k) == (int)sc) return k;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* 内部状态                                                             */
/* ------------------------------------------------------------------ */
typedef struct GLFWwindow GLFWwindow;
typedef struct GLFWmonitor GLFWmonitor;
typedef struct GLFWcursor GLFWcursor;

typedef void (*GLFWerrorfun)(int, const char *);
typedef void (*GLFWkeyfun)(GLFWwindow *, int, int, int, int);
typedef void (*GLFWcharfun)(GLFWwindow *, unsigned int);
typedef void (*GLFWcharmodsfun)(GLFWwindow *, unsigned int, int);
typedef void (*GLFWmousebuttonfun)(GLFWwindow *, int, int, int);
typedef void (*GLFWcursorposfun)(GLFWwindow *, double, double);
typedef void (*GLFWscrollfun)(GLFWwindow *, double, double);
typedef void (*GLFWframebuffersizefun)(GLFWwindow *, int, int);
typedef void (*GLFWwindowsizefun)(GLFWwindow *, int, int);
typedef void (*GLFWwindowposfun)(GLFWwindow *, int, int);
typedef void (*GLFWwindowfocusfun)(GLFWwindow *, int);
typedef void (*GLFWwindowclosefun)(GLFWwindow *);
typedef void (*GLFWwindowiconifyfun)(GLFWwindow *, int);
typedef void (*GLFWwindowrefreshfun)(GLFWwindow *);
typedef void (*GLFWcursorenterfun)(GLFWwindow *, int);
typedef void (*GLFWdropfun)(GLFWwindow *, int, const char **);
typedef void (*GLFWmonitorfun)(GLFWmonitor *, int);
typedef void (*GLFWpreeditfun)(GLFWwindow *, const char *, int);
typedef void (*GLFWimestatusfun)(GLFWwindow *, int);

typedef struct {
    int width, height;
    int redBits, greenBits, blueBits;
    int refreshRate;
} GLFWvidmode;

struct GLFWwindow {
    SDL_Window   *sdl;
    SDL_GLContext gl;
    int           shouldClose;
    int           width, height;
    int           posx, posy;
    int           focused, iconified, visible;
};

struct GLFWmonitor {
    SDL_DisplayID id;
};

struct GLFWcursor {
    SDL_Cursor *sdl;
};

static struct GLFWwindow sWin;
static struct GLFWmonitor sMon;
static GLFWvidmode sMode;
static int sInitialized = 0;
static int sTimeBase  = 0;
static uint64_t sStartNs = 0;

static unsigned char sKeyState[GLFW_KEY_LAST + 1];
static unsigned char sMouseState[GLFW_MOUSE_BUTTON_LAST + 1];

/* 触摸 -> 鼠标键映射
 *  Android 上 SDL 用触摸合成的鼠标事件只有左键，右键（放方块/使用物品）没有输入源。
 *  约定：一指 = 左键（挖掘/攻击/拖拽转视角），二指 = 右键（使用/放置）。
 *  切到右键时先把左键抬起，避免同时按住两个键导致「边挖边放」。 */
static int sTouchCount = 0;
static int sRightHeld  = 0;

/* ── 光标状态 ──
 * GLFW 的 cursorpos 是「虚拟位置」：在 GLFW_CURSOR_DISABLED 下无界（可以一直增大），
 * MC 的 MouseHandler 正是靠相邻两次 cursorpos 的差值算视角转动的。
 * SDL 在相对鼠标模式下会对绝对坐标做窗口内钳制（ConstrainMousePosition），
 * 真实增量只在 xrel/yrel 里 —— 所以必须自己累积，否则鼠标到窗口边缘就转不动。 */
static int    sCursorMode     = GLFW_CURSOR_NORMAL;
static int    sRawMotion      = 0;
static double sCursorX        = 0.0;
static double sCursorY        = 0.0;
static int    sCursorPosValid = 0;

/* SDL 鼠标键编号 (1=左 2=中 3=右 4=X1 5=X2) 与 GLFW (0=左 1=右 2=中 3..7=X) 不同，
 * 不能简单减一：SDL 右键 3 减一会落到 GLFW 2（中键），
 * 表现为「真实鼠标右键在 MC 里没反应」。 */
static int sdl_button_to_glfw(int sdlBtn)
{
    switch (sdlBtn) {
        case 1: return 0;   /* SDL_BUTTON_LEFT   -> GLFW_MOUSE_BUTTON_LEFT   */
        case 2: return 2;   /* SDL_BUTTON_MIDDLE -> GLFW_MOUSE_BUTTON_MIDDLE */
        case 3: return 1;   /* SDL_BUTTON_RIGHT  -> GLFW_MOUSE_BUTTON_RIGHT  */
        case 4: return 3;   /* SDL_BUTTON_X1     -> GLFW_MOUSE_BUTTON_4      */
        case 5: return 4;   /* SDL_BUTTON_X2     -> GLFW_MOUSE_BUTTON_5      */
        default: return -1;
    }
}

static GLFWerrorfun        cbError;
static GLFWkeyfun          cbKey;
static GLFWcharfun         cbChar;
static GLFWcharmodsfun     cbCharMods;
static GLFWmousebuttonfun  cbMouseButton;
static GLFWcursorposfun    cbCursorPos;
static GLFWscrollfun       cbScroll;
static GLFWframebuffersizefun cbFbSize;
static GLFWwindowsizefun   cbWinSize;
static GLFWwindowposfun    cbWinPos;
static GLFWwindowfocusfun  cbWinFocus;
static GLFWwindowclosefun  cbWinClose;
static GLFWwindowiconifyfun cbWinIconify;
static GLFWwindowrefreshfun cbWinRefresh;
static GLFWcursorenterfun  cbCursorEnter;
static GLFWdropfun         cbDrop;
static GLFWmonitorfun      cbMonitor;
static GLFWpreeditfun      cbPreedit;
static GLFWimestatusfun    cbImeStatus;

/* window hints（只关心几个真正影响 SDL 的） */
static int hintVisible     = GLFW_TRUE;
static int hintResizable   = GLFW_TRUE;
static int hintDecorated   = GLFW_TRUE;
static int hintSamples     = 0;
static int hintCtxMajor    = 0;
static int hintCtxMinor    = 0;
static int hintDepthBits   = 24;
static int hintStencilBits = 8;
static int hintDoubleBuf   = GLFW_TRUE;

/* 内部渲染分辨率缩放（0.5~1.0）。不需要 patch MC 的渲染流程：
 * 直接按缩放后的尺寸建窗，SDL 把 Surface 的 buffer 建成这个尺寸，
 * 合成器负责拉满屏（硬件拉伸）；MC 拿到的 framebufferSize/视口/鼠标坐标全部自洽。
 * 必须在 glfwCreateWindow 之前设置（见 nativeSetRenderScale）。 */
static float sRenderScale = 1.0f;

/* ------------------------------------------------------------------ */
/* 错误处理                                                             */
/* ------------------------------------------------------------------ */
/*
 * GLFW 的错误状态：glfwGetError 必须「读后即清」（标准 GLFW 语义）。
 * 早期这里是个占位实现，永远返回 GLFW_PLATFORM_ERROR，
 * 结果 MC 的 Window.checkGlfwError() 一读就抛
 *   "GLFW error before init: [0x10005]"
 * 导致 RenderSystem.initBackendSystem() 直接失败。
 */
static int  sErrorCode = 0;
static char sErrorDesc[512];

static void setError(int code, const char *desc)
{
    sErrorCode = code;
    if (desc) {
        size_t n = strlen(desc);
        if (n >= sizeof(sErrorDesc)) n = sizeof(sErrorDesc) - 1;
        memcpy(sErrorDesc, desc, n);
        sErrorDesc[n] = 0;
    } else {
        sErrorDesc[0] = 0;
    }
    if (cbError) cbError(code, desc);
}

/* ------------------------------------------------------------------ */
/* 生命周期                                                             */
/* ------------------------------------------------------------------ */
/*
 * Android: 让 SDL 去加载「与本库同目录」的 libmobileglues.so，同时当作 EGL 和 GL 实现。
 *
 * MobileGlues 导出完整的 eglGetProcAddress + 桌面 GL 符号（并把它们翻译到 GLES），
 * 所以一个库可以同时顶 libEGL.so 和 libGLESv2.so 两个位置。
 * SDL3 的 SDL_egl.c 会读 SDL_HINT_EGL_LIBRARY(462 行) 与 SDL_HINT_OPENGL_LIBRARY(392 行)，
 * 且两处加载失败都会自动回退到系统库，所以这里设置是安全的。
 *
 * 用 dladdr 定位自身路径，从而自动找到同目录的 libmobileglues.so，
 * 不需要把路径硬编码进 APK 或依赖 Java 侧传参。
 */
static void android_set_mobileglues_hints(void)
{
    Dl_info info;
    char path[512];
    char *slash;
    size_t n, tl;

    if (!dladdr((void *)android_set_mobileglues_hints, &info)) return;
    if (!info.dli_fname) return;

    n = strlen(info.dli_fname);
    if (n >= sizeof(path)) return;
    memcpy(path, info.dli_fname, n + 1);

    slash = strrchr(path, '/');
    if (!slash) return;

    tl = strlen("libmobileglues.so");
    if ((size_t)(slash - path) + 1 + tl + 1 > sizeof(path)) return;
    memcpy(slash + 1, "libmobileglues.so", tl + 1);

    SDL_SetHint(SDL_HINT_EGL_LIBRARY, path);
    SDL_SetHint(SDL_HINT_OPENGL_LIBRARY, path);
}

/* vctl 前置声明（glfwInit / glfwCreateWindow 里会调用） */
static void vctl_load_config(void);
static void vctl_release_all(void);
static void vctl_flush_pending(void);
static int  vctl_hotbar_rect(int idx, float *ox0, float *oy0, float *ox1, float *oy1);

int glfwInit(void)
{
    if (sInitialized) return GLFW_TRUE;

    /* 必须在任何 EGL/GL 库加载之前设置（EGL 是在建 GL 窗口时才加载的） */
    android_set_mobileglues_hints();

    /* OpenAL 诊断开关：必须在 LWJGL dlopen libopenal.so 之前设置。
     * OpenAL Soft 读 ALSOFT_CONF 指向的配置文件，log-level 打开后才能真正看到
     * 设备/格式/欠载情况（默认不往 logcat 输出任何东西）。 */
    setenv("ALSOFT_CONF", "/sdcard/.minecraft/alsoft.conf", 0);
    setenv("ALSOFT_LOGLEVEL", "4", 0);

    /* 强制横屏：SDL3 的 SDLActivity.setOrientation() 会读这个 hint，
     * 两个方向都允许 -> 请求 SCREEN_ORIENTATION_USER_LANDSCAPE。
     * 不设的话它按「窗口可缩放」走 FULL_USER，会跟着系统自动旋转来回翻转，
     * 导致 surface 尺寸在 2378x1080 / 1080x2378 之间反复跳变。 */
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        setError(0x00010005 /* GLFW_PLATFORM_ERROR */, SDL_GetError());
        return GLFW_FALSE;
    }
    sInitialized = 1;
    sStartNs = SDL_GetTicksNS();

    /* 读虚拟按键配置（无文件则用 FCL 默认布局） */
    vctl_load_config();

    /* 初始化成功，清掉可能的残留错误 */
    sErrorCode = 0;
    sErrorDesc[0] = 0;
    return GLFW_TRUE;
}

void glfwTerminate(void)
{
    if (!sInitialized) return;
    if (sWin.gl) { SDL_GL_DestroyContext(sWin.gl); sWin.gl = NULL; }
    if (sWin.sdl) { SDL_DestroyWindow(sWin.sdl); sWin.sdl = NULL; }
    SDL_Quit();
    sInitialized = 0;
}

void glfwGetVersion(int *major, int *minor, int *rev)
{
    if (major) *major = 3;
    if (minor) *minor = 4;
    if (rev)   *rev   = 0;
}

const char *glfwGetVersionString(void) { return "3.4.0 SDL3-shim (MC on ART)"; }

int glfwGetError(const char **description)
{
    int code = sErrorCode;
    if (description) {
        *description = code != 0 ? sErrorDesc : NULL;
    }
    /* 读后即清，与官方 GLFW 一致 */
    sErrorCode = 0;
    sErrorDesc[0] = 0;
    return code;
}

int glfwGetPlatform(void) { return 0x00060005; /* GLFW_PLATFORM_X11-ish */ }
int glfwPlatformSupported(int platform) { (void)platform; return GLFW_TRUE; }

void glfwInitHint(int hint, int value) { (void)hint; (void)value; }

/* ------------------------------------------------------------------ */
/* 窗口 hint                                                            */
/* ------------------------------------------------------------------ */
void glfwDefaultWindowHints(void)
{
    hintVisible = GLFW_TRUE;
    hintResizable = GLFW_TRUE;
    hintDecorated = GLFW_TRUE;
    hintSamples = 0;
    hintCtxMajor = 0;
    hintCtxMinor = 0;
    hintDepthBits = 24;
    hintStencilBits = 8;
    hintDoubleBuf = GLFW_TRUE;
}

void glfwWindowHint(int hint, int value)
{
    switch (hint) {
        case GLFW_VISIBLE:      hintVisible   = value; break;
        case GLFW_RESIZABLE:    hintResizable = value; break;
        case GLFW_DECORATED:    hintDecorated = value; break;
        case GLFW_SAMPLES:      hintSamples   = value; break;
        case GLFW_CONTEXT_VERSION_MAJOR: hintCtxMajor = value; break;
        case GLFW_CONTEXT_VERSION_MINOR: hintCtxMinor = value; break;
        case GLFW_DEPTH_BITS:   hintDepthBits = value; break;
        case GLFW_STENCIL_BITS: hintStencilBits = value; break;
        case GLFW_DOUBLEBUFFER: hintDoubleBuf = value; break;
        default: break;   /* MC 的 GLFW 分支有自定义 hint，一律忽略 */
    }
}

void glfwWindowHintString(int hint, const char *value) { (void)hint; (void)value; }

/* ------------------------------------------------------------------ */
/* 窗口                                                                 */
/* ------------------------------------------------------------------ */
GLFWwindow *glfwCreateWindow(int width, int height, const char *title,
                             GLFWmonitor *monitor, GLFWwindow *share)
{
    (void)monitor; (void)share;
    if (!sInitialized && !glfwInit()) return NULL;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    if (hintCtxMajor >= 3) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    } else {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    }
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, hintDoubleBuf ? 1 : 0);
    if (hintDepthBits > 0)
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, hintDepthBits);
    if (hintStencilBits > 0)
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, hintStencilBits);
    if (hintSamples > 0) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, hintSamples);
    }

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL;
    if (hintResizable) flags |= SDL_WINDOW_RESIZABLE;
    flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;

    /* 内部渲染分辨率：直接按缩放后的尺寸建窗。
       这样 SDL 会把 Surface 的 buffer 也建成这个尺寸（合成器负责拉满屏），
       MC 拿到的 framebufferSize / 视口 / 鼠标坐标全部自洽 —— 不会出现画面被裁切。
       注意不要在 EGL 上下文创建之后再去 setBuffersGeometry：那会让 MC 的视口
       比 buffer 大，表现为「画面有一角跑到屏幕外」。 */
    if (sRenderScale < 0.995f) {
        int sw = (int)(width * sRenderScale + 0.5f);
        int sh = (int)(height * sRenderScale + 0.5f);
        if (sw < 320) sw = 320;
        if (sh < 240) sh = 240;
        SHIMLOG("renderScale=%.2f: 建窗尺寸 %dx%d -> %dx%d", sRenderScale, width, height, sw, sh);
        width = sw;
        height = sh;
    }

    SHIMLOG("createWindow: %dx%d flags=0x%x needsES=%d ctxMajor=%d", width, height, (unsigned)flags, 1, hintCtxMajor);
    SDL_Window *w = SDL_CreateWindow(title ? title : "Minecraft",
                                     width, height, flags);
    if (!w) {
        SHIMLOG("SDL_CreateWindow FAILED: %s", SDL_GetError());
        setError(0x00010005, SDL_GetError());
        return NULL;
    }

    sWin.sdl = w;
    sWin.gl  = SDL_GL_CreateContext(w);
    if (!sWin.gl) {
        SHIMLOG("SDL_GL_CreateContext FAILED: %s", SDL_GetError());
        setError(0x00010005, SDL_GetError());
        SDL_DestroyWindow(w);
        sWin.sdl = NULL;
        return NULL;
    }
    SDL_GL_MakeCurrent(w, sWin.gl);

    SDL_GetWindowSizeInPixels(w, &sWin.width, &sWin.height);
    SDL_GetWindowPosition(w, &sWin.posx, &sWin.posy);
    sWin.shouldClose = 0;
    sWin.focused = 1;
    sWin.iconified = 0;
    sWin.visible = hintVisible;

    /* 建窗成功后再兜底读一次配置 */
    vctl_load_config();

    memset(sKeyState, 0, sizeof(sKeyState));
    memset(sMouseState, 0, sizeof(sMouseState));

    return &sWin;
}

void glfwDestroyWindow(GLFWwindow *window)
{
    if (!window) return;
    if (window->gl)  { SDL_GL_DestroyContext(window->gl); window->gl = NULL; }
    if (window->sdl) { SDL_DestroyWindow(window->sdl);    window->sdl = NULL; }
}

int  glfwWindowShouldClose(GLFWwindow *window) { return window ? window->shouldClose : 0; }
void glfwSetWindowShouldClose(GLFWwindow *window, int value)
{
    if (window) window->shouldClose = value;
}

void glfwShowWindow(GLFWwindow *window) { if (window && window->sdl) { SDL_ShowWindow(window->sdl); window->visible = 1; } }
void glfwHideWindow(GLFWwindow *window) { if (window && window->sdl) { SDL_HideWindow(window->sdl); window->visible = 0; } }

void glfwSetWindowTitle(GLFWwindow *window, const char *title)
{
    if (window && window->sdl) SDL_SetWindowTitle(window->sdl, title);
}

void glfwSetWindowIcon(GLFWwindow *window, int count, const void *images)
{
    (void)window; (void)count; (void)images;
}

void glfwSetWindowSizeLimits(GLFWwindow *window, int minw, int minh, int maxw, int maxh)
{
    if (!window || !window->sdl) return;
    SDL_SetWindowMinimumSize(window->sdl,
                             minw > 0 ? minw : 0, minh > 0 ? minh : 0);
    SDL_SetWindowMaximumSize(window->sdl,
                             maxw > 0 ? maxw : 0, maxh > 0 ? maxh : 0);
}

void glfwSetWindowAspectRatio(GLFWwindow *w, int n, int d) { (void)w; (void)n; (void)d; }

void glfwGetWindowPos(GLFWwindow *window, int *xpos, int *ypos)
{
    if (window && window->sdl) SDL_GetWindowPosition(window->sdl, &window->posx, &window->posy);
    if (xpos) *xpos = window ? window->posx : 0;
    if (ypos) *ypos = window ? window->posy : 0;
}

void glfwSetWindowPos(GLFWwindow *window, int xpos, int ypos)
{
    if (window && window->sdl) { SDL_SetWindowPosition(window->sdl, xpos, ypos); window->posx = xpos; window->posy = ypos; }
}

void glfwGetWindowSize(GLFWwindow *window, int *width, int *height)
{
    if (width)  *width  = window ? window->width  : 0;
    if (height) *height = window ? window->height : 0;
}

void glfwSetWindowSize(GLFWwindow *window, int width, int height)
{
    if (window && window->sdl) {
        SDL_SetWindowSize(window->sdl, width, height);
        SDL_GetWindowSizeInPixels(window->sdl, &window->width, &window->height);
    }
}

void glfwGetFramebufferSize(GLFWwindow *window, int *width, int *height)
{
    if (window && window->sdl) SDL_GetWindowSizeInPixels(window->sdl, &window->width, &window->height);
    if (width)  *width  = window ? window->width  : 0;
    if (height) *height = window ? window->height : 0;
}

void glfwGetWindowContentScale(GLFWwindow *window, float *xs, float *ys)
{
    (void)window;
    if (xs) *xs = 1.0f;
    if (ys) *ys = 1.0f;
}

float glfwGetWindowOpacity(GLFWwindow *w) { (void)w; return 1.0f; }
void  glfwSetWindowOpacity(GLFWwindow *w, float o) { (void)w; (void)o; }
void  glfwFocusWindow(GLFWwindow *w) { if (w && w->sdl) SDL_RaiseWindow(w->sdl); }
void  glfwMaximizeWindow(GLFWwindow *w) { if (w && w->sdl) SDL_MaximizeWindow(w->sdl); }
void  glfwRestoreWindow(GLFWwindow *w) { if (w && w->sdl) SDL_RestoreWindow(w->sdl); }
void  glfwRequestWindowAttention(GLFWwindow *w) { (void)w; }

void glfwSetWindowMonitor(GLFWwindow *window, GLFWmonitor *monitor,
                          int xpos, int ypos, int width, int height, int refreshRate)
{
    (void)monitor; (void)refreshRate;
    if (!window || !window->sdl) return;
    if (width > 0 && height > 0) SDL_SetWindowSize(window->sdl, width, height);
    if (xpos || ypos) SDL_SetWindowPosition(window->sdl, xpos, ypos);
    SDL_GetWindowSizeInPixels(window->sdl, &window->width, &window->height);
}

GLFWmonitor *glfwGetWindowMonitor(GLFWwindow *window) { (void)window; return NULL; }

int glfwGetWindowAttrib(GLFWwindow *window, int attrib)
{
    if (!window) return 0;
    switch (attrib) {
        case GLFW_VISIBLE:      return window->visible;
        case GLFW_RESIZABLE:    return hintResizable;
        case GLFW_DECORATED:    return hintDecorated;
        default:                return 0;
    }
}

void glfwSetWindowAttrib(GLFWwindow *window, int attrib, int value) { (void)window; (void)attrib; (void)value; }
void glfwSetWindowUserPointer(GLFWwindow *window, void *pointer) { (void)window; (void)pointer; }
void *glfwGetWindowUserPointer(GLFWwindow *window) { (void)window; return NULL; }

/* ------------------------------------------------------------------ */
/* GL 上下文                                                            */
/* ------------------------------------------------------------------ */
void glfwMakeContextCurrent(GLFWwindow *window)
{
    if (window && window->sdl && window->gl)
        SDL_GL_MakeCurrent(window->sdl, window->gl);
}

GLFWwindow *glfwGetCurrentContext(void) { return sWin.sdl ? &sWin : NULL; }

void glfwSwapBuffers(GLFWwindow *window)
{
    if (window && window->sdl) SDL_GL_SwapWindow(window->sdl);
}

void glfwSwapInterval(int interval) { SDL_GL_SetSwapInterval(interval); }

int glfwExtensionSupported(const char *extension) { (void)extension; return GLFW_FALSE; }

typedef void (*GLFWglproc)(void);
GLFWglproc glfwGetProcAddress(const char *procname)
{
    return (GLFWglproc)SDL_GL_GetProcAddress(procname);
}

void glfwSetContextCreationAPI(int api) { (void)api; }
void glfwGetGLVersion(int *major, int *minor, int *rev) { if (major) *major = 3; if (minor) *minor = 2; if (rev) *rev = 0; }
void glfwWindowHintString_  (int h, const char *v) { (void)h; (void)v; }

/* ================================================================== */
/* 虚拟触屏按键 + 虚拟鼠标（VCTL）                                      */
/* ------------------------------------------------------------------ */
/*  布局与坐标规范对齐 FCL（Fold Craft Launcher）：                       */
/*    · 坐标是「千分比」(per-mille, 0~1000)，不是 0~1；                   */
/*    · 按钮宽高也是千分比，但基准是「屏幕高度」(reference: SCREEN_HEIGHT)；*/
/*    · 位置按 FCL 的公式换算成像素：                                     */
/*        w_px = H * w‰/1000                                            */
/*        x_px = (W - w_px) * x‰/1000   （即 x=0 贴左边、x=1000 贴右边）  */
/*        y_px = (H - h_px) * y‰/1000                                    */
/*    · 视角区 = 整屏减去所有按钮/摇杆矩形（FCL 默认布局里没有“视角按钮”），*/
/*      所以除按键之外的每一寸屏幕都能拖动转视角。                       */
/*                                                                    */
/*  三层输入：                                                         */
/*    1) 视角层：按钮/摇杆以外的任意位置，单指拖动 = 相机转动             */
/*    2) 按键层 + 摇杆：多点触控独立跟踪，边走边看互不打断                */
/*    3) 菜单层：cursorMode == GLFW_CURSOR_NORMAL 时拖动 = 绝对光标，      */
/*               轻点 = 左键单击（没鼠标也能点选项）                      */
/*                                                                    */
/*  偏离 FCL 的一处：FCL 的 TouchController 在 ACTION_UP 时清空所有指针；  */
/*  这里按 fingerID 独立管理，一根手指抬起只释放它自己按住的键。          */
/*                                                                    */
/*  注入点：直接调 GLFW 回调（cbKey / cbMouseButton / cbScroll），         */
/*  全部发生在 SDL 事件泵线程（渲染线程），不开新线程。                   */
/* ================================================================== */

#define VCTL_LOG(...) __android_log_print(ANDROID_LOG_INFO, "VCTL", __VA_ARGS__)

/* 配置文件：改完重启游戏生效，不需要重编 native。 */
#define VCTL_CFG_PRIMARY  "/sdcard/.minecraft/vcontrols.txt"
#define VCTL_CFG_FALLBACK "/storage/emulated/0/.minecraft/vcontrols.txt"

#define VCTL_MAX_BTN   20
#define VCTL_MAX_TOUCH 12

/* 一根手指落到屏幕上的身份 */
enum {
    VCTL_T_NONE  = 0,   /* 落在按钮/摇杆/视角区之外（本实现里几乎不会出现） */
    VCTL_T_LOOK  = 1,   /* 视角 / 光标拖动 */
    VCTL_T_BTN   = 2,   /* 按着某个虚拟键 */
    VCTL_T_STICK = 3,   /* 按着左下摇杆（move_mode=stick 时）*/
    VCTL_T_DPAD  = 4,   /* 按着十字键的某一个格子（move_mode=dpad 时）*/
    VCTL_T_HOTBAR= 5    /* 按着/滑过热键栏的某个槽（hotbar=1 时）*/
};

/* 摇杆方向位 */
#define VCTL_DIR_UP    1
#define VCTL_DIR_DOWN  2
#define VCTL_DIR_LEFT  4
#define VCTL_DIR_RIGHT 8

typedef struct {
    const char  *name;      /* 配置里的名字：btn_<NAME>=x,y,w,h */
    const char  *label;     /* 屏显文字（文档/未来浮层用） */
    int          glfwKey;   /* >=0：GLFW 键盘键码 */
    int          mouseBtn;  /* >=0：GLFW 鼠标键码（0=左 1=右 2=中） */
    int          wheel;     /* +1/-1：按下时发一次滚轮 */
    float        x, y;      /* 千分比：左边缘在 (W-w) 里的比例；上边缘同理 */
    float        w, h;      /* 千分比：边长 / 屏幕高度；0 = 禁用该键 */
    int          vis;       /* 0 = 总是显示(ALWAYS) 1 = 只在游戏内(IN_GAME)，语义照 FCL */
    int          fromCfg;   /* 1 = 被配置文件覆盖 */
    int          down;      /* 当前是否按下 */
} VctlBtn;

typedef struct {
    SDL_FingerID id;             /* 0 = 空槽 */
    int          kind;           /* VCTL_T_* */
    int          btn;            /* kind==BTN 时的按钮下标 */
    float        lastX, lastY;   /* 上一帧归一化坐标（自己算增量） */
    float        downX, downY;   /* 按下点（归一化），轻点判定用 */
    float        sx, sy;         /* 摇杆：按下点（像素），摇杆以此为圆心 */
    int          moved;
} VctlTouch;

/* ── 可配置项 ── */
static int   sVctlEnabled   = 1;      /* enabled=0/1 */
static float sVctlSens      = 1.0f;   /* look_sensitivity */
static int   sVctlInvertY   = 0;      /* invert_y=0/1 */
static int   sVctlDebug     = 0;      /* debug=1：每次触摸都打日志 */
/* tap_attack=1：游戏内「视角区轻点」= 左键单击（没有可见浮层时最实用，
 * 也和手机版 MC「点屏幕挖方块」的习惯一致；拖动仍然只转视角） */
static int   sVctlTapAttack = 1;
static int   sVctlCfgLoaded = 0;
static int   sVctlLayoutW   = 0;
static int   sVctlLayoutH   = 0;
static int   sVctlLogMask   = 0;      /* 位掩码：每类事件只打一次 */
/* 可选：把视角限制在一小块区域（千分比，x0,y0,x1,y1）。默认全 0 = 不限制 */
static float sVctlLookZone[4] = { 0, 0, 0, 0 };
static int   sVctlHasLookZone = 0;

/* 摇杆（FCL 默认：左下 x=50,y=900,size=450，边长按屏幕高度千分比） */
static struct {
    float x, y, w, h;
    int   sneakOnDoubleTap;   /* FCL 的 sneak:true —— 双击方向锁定潜行 */
} sVctlStick = { 50.0f, 900.0f, 450.0f, 450.0f, 1 };

/* 移动控件：FCL 的 ControlDirectionStyle.Type 有两值 BUTTON(十字键) / ROCKER(摇杆)，
 * 而 ControlDirectionStyle.java:58 的默认值就是 BUTTON，默认布局的
 * directionStyles[0].styleType 也是 "BUTTON" —— 所以这里默认 move_mode=dpad。
 * 摇杆实现整套保留，配置 move_mode=stick 即切回。 */
static int   sVctlMoveMode  = 1;           /* 0 = stick(摇杆)  1 = dpad(十字键) */
/* 十字键布区（千分比，宽高以屏幕高为基准），默认沿用 FCL 摇杆的足迹 50,900,450 */
static float sVctlDpad[4]   = { 50.0f, 900.0f, 450.0f, 450.0f };
/* 方向键之间的间隙（千分比，FCL buttonStyle.interval 默认 50） */
static float sVctlDpadInterval = 50.0f;
static int   sVctlDpadDiag      = 1;       /* 1 = 允许斜向（相邻两方向同时按住） */
static int   sVctlDpadSneakCtr  = 1;       /* 1 = 中心区按下 = 潜行（FCL sneak:true） */
static int   sVctlDpadSneakKey  = 340;     /* GLFW_KEY_LEFT_SHIFT */

/* 十字键运行期状态：0=上 1=左 2=中心 3=右 4=下 */
static int   sVctlDpadDown[5] = { 0, 0, 0, 0, 0 };
/* 当前方向位掩码（VCTL_DIR_* 组合；斜向时两位同时置位） */
static int   sVctlDpadBits     = 0;
/* FCL directionList 的 up/down/left/right keycode，已换成 GLFW 码（W/S/A/D） */
static const int sVctlDpadKey[5] = { 87, 65, -1, 68, 83 };

/* ── 热键栏（快捷栏切换）─────────────────────────────────────────
 * FCL 里没有独立的"快捷栏按钮"，玩家靠滚轮/数字键；手机上两者都没有，
 * 所以这里做一排槽位：点哪个槽就注入 GLFW_KEY_1..9（MC 的选槽键）。
 * 注意 KeyMapping 轮询语义：PRESS 与 RELEASE 必须是**两次独立回调**且**跨帧**，
 * 同帧一按一放会被 consumeClick() 吃掉。实现方式：
 *   - select() 里只发 PRESS，并记下待释放的键；
 *   - 下一次 glfwPollEvents() **开头**才发 RELEASE（见 vctl_flush_pending）。 */
static int   sVctlHotbarOn      = 1;                                  /* hotbar=0/1 */
static float sVctlHotbarZone[4] = { 300.0f, 855.0f, 400.0f, 52.0f };  /* ‰ x,y,w,h（放在原版快捷栏上方，避开底边手势区）*/
static int   sVctlHotbarSlots   = 9;                                  /* 1..9 */
static int   sVctlHotbarSel     = -1;      /* 最近切到的槽（0 起），浮层高亮用 */
static int   sVctlHotbarDown    = -1;      /* 当前手指按住的槽 */
static int   sVctlHotbarPendKey = -1;      /* 待释放的 GLFW 键 */

/* 摇杆运行期状态 */
static int   sVctlStickBits  = 0;          /* 当前方向位 */
static int   sVctlStickSneak = 0;          /* 本次按住是否进入潜行 */
static int   sVctlStickHeld  = 0;          /* 是否有手指按在摇杆上（浮层高亮用） */
static Uint64 sVctlLastDirUpNs = 0;        /* 上一次方向键抬起时刻（双击判定） */

static VctlTouch sVctlTouch[VCTL_MAX_TOUCH];

/* ------------------------------------------------------------------ */
/* 按钮表 —— 默认值直接搬 FCL 的 assets/controllers/00000000.json        */
/*   键码已换成 GLFW 值（回调收的就是 GLFW 码）：                        */
/*   键盘：A=65..Z=90、SPACE=32、LEFT_SHIFT=340、ESC=256、F3=290、       */
/*         ENTER=257、BACKSPACE=259                                      */
/*   鼠标：LEFT=0、RIGHT=1、MIDDLE=2                                     */
/*   （FCL 的 1000=鼠标左、1002=鼠标右 是它自己的扩展码，这里已换算）     */
/* ------------------------------------------------------------------ */
static VctlBtn sVctlBtn[] = {
    /* 名字        文字   键/鼠标/滚轮       x        y        w        h     vis
     * vis 照 FCL 的 visibilityType：0=ALWAYS（菜单里也显示）1=IN_GAME（只在游戏内） */
    { "ATTACK",   "×",    -1,      0,    0,   800.0f,  750.0f, 140.0f, 140.0f, 0 },  /* 左键：挖掘/攻击 */
    { "JUMP",     "■",    32,     -1,    0,   875.0f,  750.0f, 140.0f, 140.0f, 1 },  /* 空格：跳 */
    { "USE",      "○",    -1,      1,    0,   875.0f,  565.0f, 140.0f, 140.0f, 0 },  /* 右键：放置/使用 */
    { "INV",      "E",    69,     -1,    0,   720.0f, 1000.0f, 100.0f, 100.0f, 0 },  /* 背包 */
    { "SNEAK",    "▼",    340,    -1,    0,   950.0f,  935.0f, 140.0f, 140.0f, 0 },  /* 潜行 */
    /* DROP 原值 x=1000（贴右缘）实测被系统「右边缘返回手势」吃掉，改 940 */
    { "DROP",     "Q",    81,     -1,    0,   940.0f,  400.0f, 120.0f, 120.0f, 1 },  /* 丢弃 */
    { "ESC",      "Esc",  256,    -1,    0,     0.0f,    0.0f, 160.0f, 120.0f, 0 },  /* 菜单 */
    { "F3",       "F3",   290,    -1,    0,   100.0f,    0.0f, 160.0f, 120.0f, 0 },  /* 调试屏 */
    { "CHAT",     "T",    84,     -1,    0,   200.0f,    0.0f, 160.0f, 120.0f, 1 },  /* 聊天 */
    { "ENTER",    "↵",    257,    -1,    0,   900.0f,    0.0f, 160.0f, 120.0f, 0 },  /* 回车 */
    { "BACKSP",   "⌫",    259,    -1,    0,  1000.0f,    0.0f, 160.0f, 120.0f, 0 },  /* 退格 */
    /* 以下默认禁用（w=h=0），需要时在配置里写 btn_<名字>=x,y,w,h 打开 */
    { "WHEELUP",  "▲",    -1,     -1,    1,   700.0f,  980.0f,   0.0f,   0.0f, 1 },  /* 上一格 */
    { "WHEELDN",  "▽",    -1,     -1,   -1,   700.0f,  860.0f,   0.0f,   0.0f, 1 },  /* 下一格 */
    { "KEY_W",    "W",    87,     -1,    0,   100.0f,  700.0f,   0.0f,   0.0f, 1 },
    { "KEY_A",    "A",    65,     -1,    0,     0.0f,  800.0f,   0.0f,   0.0f, 1 },
    { "KEY_S",    "S",    83,     -1,    0,   100.0f,  800.0f,   0.0f,   0.0f, 1 },
    { "KEY_D",    "D",    68,     -1,    0,   200.0f,  800.0f,   0.0f,   0.0f, 1 },
};
static const int sVctlBtnN = (int)(sizeof(sVctlBtn) / sizeof(sVctlBtn[0]));

/* ------------------------------------------------------------------ */
/* 小工具                                                             */
/* ------------------------------------------------------------------ */
static VctlBtn *vctl_btn_by_name(const char *name)
{
    for (int i = 0; i < sVctlBtnN; i++) {
        if (strcmp(sVctlBtn[i].name, name) == 0) return &sVctlBtn[i];
    }
    return NULL;
}

static char *vctl_trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) e--;
    *e = 0;
    return s;
}

/* 从 "1.2, 3.4 ,5" 里解析最多 n 个 float */
static int vctl_parse_floats(const char *v, float *out, int n)
{
    int got = 0;
    const char *p = v;
    while (got < n && *p) {
        char *end = NULL;
        float f = strtof(p, &end);
        if (end == p) break;
        out[got++] = f;
        p = end;
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
    }
    return got;
}

/* 千分比 -> 像素矩形（FCL 公式） */
static void vctl_geo(float xm, float ym, float wm, float hm, int W, int H,
                     float *x0, float *y0, float *x1, float *y1)
{
    float wpx = (float)H * wm / 1000.0f;
    float hpx = (float)H * hm / 1000.0f;
    float xpx = ((float)W - wpx) * xm / 1000.0f;
    float ypx = ((float)H - hpx) * ym / 1000.0f;
    if (wpx < 0) wpx = 0;
    if (hpx < 0) hpx = 0;
    *x0 = xpx; *y0 = ypx;
    *x1 = xpx + wpx; *y1 = ypx + hpx;
}

static int vctl_pt_in(float px, float py, float x0, float y0, float x1, float y1)
{
    return (px >= x0 && px <= x1 && py >= y0 && py <= y1);
}

/* ------------------------------------------------------------------ */
/* 读配置：key=value 每行一条，# 起注释；任何错误只降级不打挂游戏         */
/* ------------------------------------------------------------------ */
static void vctl_load_config(void)
{
    if (sVctlCfgLoaded) return;
    sVctlCfgLoaded = 1;

    FILE *f = fopen(VCTL_CFG_PRIMARY, "r");
    const char *used = VCTL_CFG_PRIMARY;
    if (!f) { f = fopen(VCTL_CFG_FALLBACK, "r"); used = VCTL_CFG_FALLBACK; }
    if (!f) {
        VCTL_LOG("未找到 %s，使用 FCL 默认布局（虚拟键已启用）", VCTL_CFG_PRIMARY);
        return;
    }

    char line[256];
    int  nline = 0, nbad = 0;
    while (fgets(line, sizeof(line), f)) {
        nline++;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *s = vctl_trim(line);
        if (!*s) continue;

        char *eq = strchr(s, '=');
        if (!eq) { nbad++; continue; }
        *eq = 0;
        char *k = vctl_trim(s);
        char *v = vctl_trim(eq + 1);
        if (!*k) { nbad++; continue; }

        if (strcmp(k, "enabled") == 0) {
            sVctlEnabled = (atoi(v) != 0);
        } else if (strcmp(k, "look_sensitivity") == 0) {
            float t = strtof(v, NULL);
            if (t > 0.05f && t < 20.0f) sVctlSens = t; else nbad++;
        } else if (strcmp(k, "invert_y") == 0) {
            sVctlInvertY = (atoi(v) != 0);
        } else if (strcmp(k, "debug") == 0) {
            sVctlDebug = (atoi(v) != 0);
        } else if (strcmp(k, "tap_attack") == 0) {
            sVctlTapAttack = (atoi(v) != 0);
        } else if (strcmp(k, "stick_sneak") == 0) {
            sVctlStick.sneakOnDoubleTap = (atoi(v) != 0);
        } else if (strcmp(k, "move_mode") == 0) {
            if (strcmp(v, "dpad") == 0)       sVctlMoveMode = 1;
            else if (strcmp(v, "stick") == 0) sVctlMoveMode = 0;
            else nbad++;
        } else if (strcmp(k, "dpad_zone") == 0) {
            /* dpad_zone=x,y,w,h（千分比，宽高以屏幕高为基准） */
            float t[4];
            if (vctl_parse_floats(v, t, 4) == 4 && t[2] > 0.0f && t[3] > 0.0f) {
                for (int i = 0; i < 4; i++) sVctlDpad[i] = t[i];
            } else nbad++;
        } else if (strcmp(k, "dpad_interval") == 0) {
            float t = strtof(v, NULL);
            if (t >= 0.0f && t <= 300.0f) sVctlDpadInterval = t; else nbad++;
        } else if (strcmp(k, "dpad_diag") == 0) {
            sVctlDpadDiag = (atoi(v) != 0);
        } else if (strcmp(k, "dpad_sneak_center") == 0) {
            sVctlDpadSneakCtr = (atoi(v) != 0);
        } else if (strcmp(k, "dpad_sneak_key") == 0) {
            int t = atoi(v);
            if (t > 0 && t <= GLFW_KEY_LAST) sVctlDpadSneakKey = t; else nbad++;
        } else if (strcmp(k, "hotbar") == 0) {
            sVctlHotbarOn = (atoi(v) != 0);
        } else if (strcmp(k, "hotbar_zone") == 0) {
            /* hotbar_zone=x,y,w,h（千分比，宽高以屏幕高为基准，同 dpad_zone） */
            float t[4];
            if (vctl_parse_floats(v, t, 4) == 4 && t[2] > 0.0f && t[3] > 0.0f) {
                for (int i = 0; i < 4; i++) sVctlHotbarZone[i] = t[i];
            } else nbad++;
        } else if (strcmp(k, "hotbar_slots") == 0) {
            int t = atoi(v);
            if (t >= 1 && t <= 9) sVctlHotbarSlots = t; else nbad++;
        } else if (strcmp(k, "stick") == 0) {
            /* stick=x,y,size  千分比（x,y 位置，size 边长/屏幕高度） */
            float t[3];
            if (vctl_parse_floats(v, t, 3) == 3) {
                sVctlStick.x = t[0]; sVctlStick.y = t[1];
                sVctlStick.w = t[2]; sVctlStick.h = t[2];
            } else nbad++;
        } else if (strcmp(k, "look_zone") == 0) {
            float t[4];
            if (vctl_parse_floats(v, t, 4) == 4 &&
                t[0] >= 0.0f && t[0] < t[2] && t[2] <= 1000.0f &&
                t[1] >= 0.0f && t[1] < t[3] && t[3] <= 1000.0f) {
                for (int i = 0; i < 4; i++) sVctlLookZone[i] = t[i];
                sVctlHasLookZone = 1;
            } else nbad++;
        } else if (strncmp(k, "btn_", 4) == 0) {
            /* btn_<NAME>=x,y,w,h —— 千分比；x,y 是位置，w,h 是边长/屏幕高度
               （与 FCL 的 xPosition/yPosition/percentageWidth 同一套语义）
               把 w 或 h 写 0 就是禁用这个键。 */
            VctlBtn *b = vctl_btn_by_name(k + 4);
            float t[4];
            if (b && vctl_parse_floats(v, t, 4) == 4) {
                b->x = t[0]; b->y = t[1];
                b->w = (t[2] > 0.0f ? t[2] : 0.0f);
                b->h = (t[3] > 0.0f ? t[3] : 0.0f);
                b->fromCfg = 1;
            } else nbad++;
        } else {
            nbad++;                        /* 未知键：忽略（前向兼容） */
        }
    }
    fclose(f);

    VCTL_LOG("配置已加载: %s (行=%d 无效=%d)", used, nline, nbad);
    VCTL_LOG("move_mode=%s dpad=(%.0f,%.0f,%.0f,%.0f) interval=%.0f diag=%d sneakCtr=%d sneakKey=%d",
             sVctlMoveMode ? "dpad" : "stick",
             (double)sVctlDpad[0], (double)sVctlDpad[1], (double)sVctlDpad[2], (double)sVctlDpad[3],
             (double)sVctlDpadInterval, sVctlDpadDiag, sVctlDpadSneakCtr, sVctlDpadSneakKey);
    VCTL_LOG("enabled=%d sens=%.2f invert_y=%d debug=%d tap_attack=%d stick=(%.0f,%.0f,%.0f) sneak=%d",
             sVctlEnabled, (double)sVctlSens, sVctlInvertY, sVctlDebug, sVctlTapAttack,
             (double)sVctlStick.x, (double)sVctlStick.y, (double)sVctlStick.w,
             sVctlStick.sneakOnDoubleTap);
    if (sVctlHasLookZone)
        VCTL_LOG("look_zone=%.0f,%.0f,%.0f,%.0f（千分比，已限制视角区）",
                 (double)sVctlLookZone[0], (double)sVctlLookZone[1],
                 (double)sVctlLookZone[2], (double)sVctlLookZone[3]);
    else
        VCTL_LOG("视角区 = 整屏减去按钮/摇杆（FCL 默认行为）");
}

/* ------------------------------------------------------------------ */
/* 布局：算一次，打一份「千分比 + 像素」对照表（自检 & 文档用）          */
/* ------------------------------------------------------------------ */
/* 十字键几何（定义在下面，布局日志要先用到） */
static int vctl_dpad_rect(int idx, float *ox0, float *oy0, float *ox1, float *oy1);

static void vctl_layout(void)
{
    int W = sWin.width, H = sWin.height;
    if (W <= 0 || H <= 0) return;
    if (W == sVctlLayoutW && H == sVctlLayoutH) return;
    sVctlLayoutW = W; sVctlLayoutH = H;

    for (int i = 0; i < sVctlBtnN; i++) {
        VctlBtn *b = &sVctlBtn[i];
        if (b->w <= 0.0f || b->h <= 0.0f) {
            VCTL_LOG("布局: %-8s(%s) 已禁用", b->name, b->label);
            continue;
        }
        float x0, y0, x1, y1;
        vctl_geo(b->x, b->y, b->w, b->h, W, H, &x0, &y0, &x1, &y1);
        VCTL_LOG("布局: %-8s(%s) ‰=(%.0f,%.0f,%.0f,%.0f) 像素=[%d,%d]-[%d,%d] %dx%d",
                 b->name, b->label, (double)b->x, (double)b->y, (double)b->w, (double)b->h,
                 (int)x0, (int)y0, (int)x1, (int)y1, (int)(x1 - x0), (int)(y1 - y0));
    }
    if (sVctlMoveMode == 1) {
        static const char *nm[5] = { "DPAD-UP", "DPAD-LEFT", "DPAD-CENTER", "DPAD-RIGHT", "DPAD-DOWN" };
        VCTL_LOG("布局: 移动控件 = 十字键(BUTTON 风格) ‰=(%.0f,%.0f,%.0f,%.0f) interval=%.0f",
                 (double)sVctlDpad[0], (double)sVctlDpad[1], (double)sVctlDpad[2],
                 (double)sVctlDpad[3], (double)sVctlDpadInterval);
        for (int i = 0; i < 5; i++) {
            float x0, y0, x1, y1;
            if (!vctl_dpad_rect(i, &x0, &y0, &x1, &y1)) {
                VCTL_LOG("布局: %-12s 已禁用", nm[i]);
                continue;
            }
            VCTL_LOG("布局: %-12s 像素=[%d,%d]-[%d,%d] %dx%d",
                     nm[i], (int)x0, (int)y0, (int)x1, (int)y1, (int)(x1 - x0), (int)(y1 - y0));
        }
    } else {
        float x0, y0, x1, y1;
        vctl_geo(sVctlStick.x, sVctlStick.y, sVctlStick.w, sVctlStick.h,
                 W, H, &x0, &y0, &x1, &y1);
        VCTL_LOG("布局: %-8s   ‰=(%.0f,%.0f,%.0f,%.0f) 像素=[%d,%d]-[%d,%d] 半径=%d",
                 "STICK", (double)sVctlStick.x, (double)sVctlStick.y,
                 (double)sVctlStick.w, (double)sVctlStick.h,
                 (int)x0, (int)y0, (int)x1, (int)y1, (int)((x1 - x0) * 0.5f));
    }
    /* 热键栏 */
    if (sVctlHotbarOn) {
        float hx0, hy0, hx1, hy1;
        VCTL_LOG("布局: 热键栏 ‰=(%.0f,%.0f,%.0f,%.0f) 槽数=%d",
                 (double)sVctlHotbarZone[0], (double)sVctlHotbarZone[1],
                 (double)sVctlHotbarZone[2], (double)sVctlHotbarZone[3], sVctlHotbarSlots);
        for (int i = 0; i < sVctlHotbarSlots; i++) {
            if (!vctl_hotbar_rect(i, &hx0, &hy0, &hx1, &hy1)) {
                VCTL_LOG("布局: 热键栏槽%d 已禁用（条太窄）", i + 1);
                continue;
            }
            VCTL_LOG("布局: 热键栏槽%-2d 像素=[%d,%d]-[%d,%d]", i + 1,
                     (int)hx0, (int)hy0, (int)hx1, (int)hy1);
        }
    } else {
        VCTL_LOG("布局: 热键栏 hotbar=0（关闭）");
    }
    VCTL_LOG("布局完成: 窗口 %dx%d（长按拖动的视角区 = 除上述矩形以外的整屏）", W, H);
}

/* ------------------------------------------------------------------ */
/* 合成事件：直接走 GLFW 回调（与真实事件同一条路）                     */
/* ------------------------------------------------------------------ */
static void vctl_inject_key(int key, int down)
{
    if (key < 0) return;
    int action = down ? GLFW_PRESS : GLFW_RELEASE;
    if (key <= GLFW_KEY_LAST) sKeyState[key] = (unsigned char)(down ? 1 : 0);
    if (cbKey) cbKey(&sWin, key, glfwkey_to_scancode(key), action, 0);
}

static void vctl_inject_mouse(int btn, int down)
{
    if (btn < 0) return;
    int action = down ? GLFW_PRESS : GLFW_RELEASE;
    if (btn <= GLFW_MOUSE_BUTTON_LAST) sMouseState[btn] = (unsigned char)(down ? 1 : 0);
    if (cbMouseButton) cbMouseButton(&sWin, btn, action, 0);
}

static void vctl_inject_scroll(int dir)
{
    if (dir == 0) return;
    if (cbScroll) cbScroll(&sWin, 0.0, (double)dir);
}

/* 按下 / 抬起某个虚拟键（带去重，避免重复 PRESS 打乱 MC 状态） */
static void vctl_btn_apply(int i, int down)
{
    if (i < 0 || i >= sVctlBtnN) return;
    VctlBtn *b = &sVctlBtn[i];
    if (b->down == down) return;
    b->down = down;

    if (b->wheel) { if (down) vctl_inject_scroll(b->wheel); return; }
    if (b->glfwKey  >= 0) vctl_inject_key(b->glfwKey, down);
    if (b->mouseBtn >= 0) vctl_inject_mouse(b->mouseBtn, down);

    if (sVctlDebug) VCTL_LOG("键 %s(%s) %s", b->name, b->label, down ? "按下" : "抬起");
}

/* 视角 / 光标移动（dxPx/dyPx 单位 = 窗口像素） */
static void vctl_move_cursor(double dxPx, double dyPx)
{
    double k = (double)sVctlSens;
    if (sVctlInvertY) dyPx = -dyPx;

    if (sCursorMode == GLFW_CURSOR_DISABLED) {
        /* 相对模式：虚拟位置无界累积 —— 和物理鼠标走完全同一条路，
           MC 的 MouseHandler 靠相邻 cursorpos 的差值算视角转动。 */
        sCursorX += dxPx * k;
        sCursorY += dyPx * k;
        sCursorPosValid = 1;
        if (cbCursorPos) cbCursorPos(&sWin, sCursorX, sCursorY);
    } else {
        /* 菜单模式：绝对坐标，钳制在窗口内（GUI 命中测试要求合法范围） */
        double w = (sWin.width  > 0 ? sWin.width  : 1);
        double h = (sWin.height > 0 ? sWin.height : 1);
        double nx = sCursorX + dxPx * k;
        double ny = sCursorY + dyPx * k;
        if (nx < 0) nx = 0;
        if (nx > w - 1) nx = w - 1;
        if (ny < 0) ny = 0;
        if (ny > h - 1) ny = h - 1;
        sCursorX = nx;
        sCursorY = ny;
        sCursorPosValid = 1;
        if (cbCursorPos) cbCursorPos(&sWin, sCursorX, sCursorY);
    }
}

/* 菜单模式：手指按下即把光标放到该点（点哪指哪） */
static void vctl_set_cursor_abs(double nx, double ny)
{
    double w = (sWin.width  > 0 ? sWin.width  : 1);
    double h = (sWin.height > 0 ? sWin.height : 1);
    double x = nx * w, y = ny * h;
    if (x < 0) x = 0; if (x > w - 1) x = w - 1;
    if (y < 0) y = 0; if (y > h - 1) y = h - 1;
    sCursorX = x; sCursorY = y; sCursorPosValid = 1;
    if (cbCursorPos) cbCursorPos(&sWin, sCursorX, sCursorY);
}

/* ------------------------------------------------------------------ */
/* 十字键（FCL 的 BUTTON 风格）                                         */
/*   在一个正方形足迹内按 3x3 排布：只有 上/左/中心/右/下 五格，四角留空，  */
/*   格子边长 cell = (S - 2*interval) / 3，相邻格子之间正好隔 interval。   */
/*   idx: 0=上 1=左 2=中心 3=右 4=下                                    */
/* ------------------------------------------------------------------ */
static int vctl_dpad_rect(int idx, float *ox0, float *oy0, float *ox1, float *oy1)
{
    int W = sWin.width, H = sWin.height;
    if (W <= 0 || H <= 0 || idx < 0 || idx > 4) return 0;
    if (sVctlDpad[2] <= 0.0f || sVctlDpad[3] <= 0.0f) return 0;
    if (idx == 2 && !sVctlDpadSneakCtr) return 0;      /* 中心潜行关掉时中心格不参与 */

    float zx0, zy0, zx1, zy1;
    vctl_geo(sVctlDpad[0], sVctlDpad[1], sVctlDpad[2], sVctlDpad[3], W, H, &zx0, &zy0, &zx1, &zy1);
    float S  = (zx1 - zx0) < (zy1 - zy0) ? (zx1 - zx0) : (zy1 - zy0);   /* 取正方形 */
    float iv = (float)H * sVctlDpadInterval / 1000.0f;
    if (iv < 0.0f) iv = 0.0f;
    float cell = (S - 2.0f * iv) / 3.0f;
    if (cell < 8.0f) { iv = 0.0f; cell = S / 3.0f; }   /* 足迹太小就别留缝 */
    if (cell < 4.0f) return 0;

    static const int col[5] = { 1, 0, 1, 2, 1 };
    static const int row[5] = { 0, 1, 1, 1, 2 };
    float bx = zx0 + (float)col[idx] * (cell + iv);
    float by = zy0 + (float)row[idx] * (cell + iv);
    *ox0 = bx; *oy0 = by; *ox1 = bx + cell; *oy1 = by + cell;
    return 1;
}

/* 按下 / 抬起某一格；PRESS 与 RELEASE 是两次独立回调（KeyMapping 轮询语义） */
static void vctl_dpad_set(int idx, int down)
{
    if (idx < 0 || idx > 4) return;
    if (sVctlDpadDown[idx] == down) return;
    if (idx == 2 && !sVctlDpadSneakCtr) return;

    /* dpad_diag=0 时方向互斥：按下新方向先松开其它方向（中心潜行不算方向） */
    if (down && !sVctlDpadDiag && idx != 2) {
        for (int i = 0; i < 5; i++) {
            if (i == 2 || i == idx || !sVctlDpadDown[i]) continue;
            sVctlDpadDown[i] = 0;
            vctl_inject_key(sVctlDpadKey[i], 0);
        }
    }

    sVctlDpadDown[idx] = down;
    int key = (idx == 2) ? sVctlDpadSneakKey : sVctlDpadKey[idx];
    vctl_inject_key(key, down);
    if (sVctlDebug) {
        static const char *nm[5] = { "上", "左", "中心", "右", "下" };
        VCTL_LOG("十字键 %s(键%d) %s", nm[idx], key, down ? "按下" : "抬起");
    }
}

static void vctl_dpad_release_all(void)
{
    for (int i = 0; i < 5; i++) {
        if (sVctlDpadDown[i]) vctl_dpad_set(i, 0);
    }
}

/* 当前按住的键位图（浮层用）：bit0=上 bit1=左 bit2=中心 bit3=右 bit4=下 */
static int vctl_dpad_bits(void)
{
    int b = 0;
    for (int i = 0; i < 5; i++) if (sVctlDpadDown[i]) b |= (1 << i);
    return b;
}

/* 命中：返回格下标，-1 = 不在十字键上 */
/*
 * 十字键：由触点角度算 **8 向**位掩码。
 *
 * 为什么不再用「3x3 九宫格格子」做命中：单指永远只能命中一个格子，
 * 于是按不出「上+右」这种斜向；而 MC 的移动是数字量（WASD 组合），
 * 斜跑必须**两个键同时按下**。所以改成按角度分 8 个扇区：
 *     0°=右 45°=右下 90°=下 135°=左下 180°=左 225°=左上 270°=上 315°=右上
 * 中心留死区（半径 S*0.17）= 潜行（等价 FCL direction event 的 sneak:true）。
 *
 * 返回：方向位掩码（0 = 命中死区）
 *   *inZone=1 表示触点落在十字键范围内（此时返回 0 就是「潜行」）
 *   *inZone=0 表示压根不在十字键上
 */
static int vctl_dpad_bits_at(float px, float py, int *inZone)
{
    if (inZone) *inZone = 0;
    int W = sWin.width, H = sWin.height;
    if (W <= 0 || H <= 0) return 0;
    if (sVctlDpad[2] <= 0.0f || sVctlDpad[3] <= 0.0f) return 0;

    float zx0, zy0, zx1, zy1;
    vctl_geo(sVctlDpad[0], sVctlDpad[1], sVctlDpad[2], sVctlDpad[3], W, H,
             &zx0, &zy0, &zx1, &zy1);

    float S = (zx1 - zx0) < (zy1 - zy0) ? (zx1 - zx0) : (zy1 - zy0);
    if (S < 24.0f) return 0;

    /* 与 vctl_dpad_rect 同一基准（左上角 + 边长 S）：浮层画的位置与命中区一致 */
    float gx0 = zx0, gy0 = zy0, gx1 = gx0 + S, gy1 = gy0 + S;
    if (px < gx0 || px > gx1 || py < gy0 || py > gy1) return 0;
    if (inZone) *inZone = 1;

    float cx = (gx0 + gx1) * 0.5f, cy = (gy0 + gy1) * 0.5f;
    float dx = px - cx, dy = py - cy;
    float r  = sqrtf(dx * dx + dy * dy);
    if (r < S * 0.17f) return 0;             /* 死区 -> 潜行 */

    /* 屏幕坐标 y 向下：0°=右、90°=下 */
    float ang = atan2f(dy, dx) * 57.29577951f;
    if (ang < 0.0f) ang += 360.0f;
    int sector = (int)((ang + 22.5f) / 45.0f);
    if (sector >= 8) sector -= 8;

    static const int secBits[8] = {
        VCTL_DIR_RIGHT,                       /*   0° 右   */
        VCTL_DIR_RIGHT | VCTL_DIR_DOWN,       /*  45° 右下 */
        VCTL_DIR_DOWN,                        /*  90° 下   */
        VCTL_DIR_LEFT  | VCTL_DIR_DOWN,       /* 135° 左下 */
        VCTL_DIR_LEFT,                        /* 180° 左   */
        VCTL_DIR_LEFT  | VCTL_DIR_UP,         /* 225° 左上 */
        VCTL_DIR_UP,                          /* 270° 上   */
        VCTL_DIR_UP    | VCTL_DIR_RIGHT       /* 315° 右上 */
    };
    int bits = secBits[sector];

    if (!sVctlDpadDiag) {                     /* dpad_diag=0：只保留主轴，不斜跑 */
        bits = (fabsf(dx) > fabsf(dy))
             ? (dx > 0.0f ? VCTL_DIR_RIGHT : VCTL_DIR_LEFT)
             : (dy > 0.0f ? VCTL_DIR_DOWN  : VCTL_DIR_UP);
    }
    return bits;
}

/*
 * 把方向掩码落到按键上：只对**状态变化**的轴发 PRESS/RELEASE。
 * bits = -1 表示「离开十字键」→ 全部松开（含中心潜行）。
 * 逻辑格编号沿用原表：0=上 1=左 2=中心(潜行) 3=右 4=下
 */
static void vctl_dpad_apply_bits(int bits)
{
    if (bits < 0) {
        vctl_dpad_release_all();
        sVctlDpadBits = 0;
        return;
    }

    if (sVctlDebug && bits != sVctlDpadBits)
        VCTL_LOG("十字键方向 0x%x -> 0x%x（%s）", sVctlDpadBits, bits,
                 (bits && (bits & (bits - 1))) ? "斜向" : (bits ? "单轴" : "中心/死区"));
    sVctlDpadBits = bits;

    static const struct { int bit; int cell; } map[4] = {
        { VCTL_DIR_UP,    0 },
        { VCTL_DIR_LEFT,  1 },
        { VCTL_DIR_RIGHT, 3 },
        { VCTL_DIR_DOWN,  4 },
    };
    for (int i = 0; i < 4; i++) {
        int want = (bits & map[i].bit) ? 1 : 0;
        if (sVctlDpadDown[map[i].cell] != want) vctl_dpad_set(map[i].cell, want);
    }

    /* 死区 -> 潜行（只有仍在十字键范围内且方向为 0 时才会到这里） */
    if (sVctlDpadSneakCtr) {
        int wantSneak = (bits == 0) ? 1 : 0;
        if (sVctlDpadDown[2] != wantSneak) vctl_dpad_set(2, wantSneak);
    }
}

/* 摇杆方向位 -> 键 */
static void vctl_stick_apply_bits(int bits)
{
    if (bits == sVctlStickBits) return;
    int old = sVctlStickBits;
    sVctlStickBits = bits;

    static const struct { int bit; int key; } map[4] = {
        { VCTL_DIR_UP,    87 },   /* W */
        { VCTL_DIR_DOWN,  83 },   /* S */
        { VCTL_DIR_LEFT,  65 },   /* A */
        { VCTL_DIR_RIGHT, 68 },   /* D */
    };
    for (int i = 0; i < 4; i++) {
        int was = (old  & map[i].bit) != 0;
        int now = (bits & map[i].bit) != 0;
        if (was != now) vctl_inject_key(map[i].key, now);
    }
    if (sVctlDebug)
        VCTL_LOG("摇杆方向 0x%x -> 0x%x%s", old, bits, sVctlStickSneak ? " (潜行中)" : "");
}

static void vctl_stick_release(void)
{
    vctl_stick_apply_bits(0);
    if (sVctlStickSneak) {
        sVctlStickSneak = 0;
        vctl_inject_key(340, 0);          /* 松开潜行 */
    }
    sVctlLastDirUpNs = SDL_GetTicksNS();
}

/* 全部松开：切光标模式（进/出菜单）、窗口失焦、关闭时调用，防按键卡住 */
static void vctl_release_all(void)
{
    for (int i = 0; i < sVctlBtnN; i++) {
        if (sVctlBtn[i].down) vctl_btn_apply(i, 0);
    }
    vctl_stick_release();
    vctl_dpad_release_all();
    vctl_flush_pending();          /* 热键栏：把待释放的选槽键放掉 */
    sVctlHotbarDown = -1;
    for (int i = 0; i < VCTL_MAX_TOUCH; i++) {
        sVctlTouch[i].id   = 0;
        sVctlTouch[i].kind = VCTL_T_NONE;
        sVctlTouch[i].btn  = -1;
    }
}

/* ------------------------------------------------------------------ */
/* 热键栏（快捷栏）                                                    */
/* ------------------------------------------------------------------ */

/* 第 idx 个槽的像素矩形；返回 0 表示该槽不可用 */
static int vctl_hotbar_rect(int idx, float *ox0, float *oy0, float *ox1, float *oy1)
{
    int W = sWin.width, H = sWin.height;
    if (!sVctlHotbarOn || W <= 0 || H <= 0) return 0;
    if (idx < 0 || idx >= sVctlHotbarSlots) return 0;
    if (sVctlHotbarZone[2] <= 0.0f || sVctlHotbarZone[3] <= 0.0f) return 0;

    float zx0, zy0, zx1, zy1;
    vctl_geo(sVctlHotbarZone[0], sVctlHotbarZone[1],
             sVctlHotbarZone[2], sVctlHotbarZone[3], W, H, &zx0, &zy0, &zx1, &zy1);

    float gap = 3.0f;                                  /* 槽间像素间隙 */
    float cw  = ((zx1 - zx0) - gap * (float)(sVctlHotbarSlots - 1)) / (float)sVctlHotbarSlots;
    if (cw < 6.0f) return 0;                           /* 太窄就别画/别命中 */

    float bx = zx0 + (float)idx * (cw + gap);
    *ox0 = bx; *oy0 = zy0; *ox1 = bx + cw; *oy1 = zy1;
    return 1;
}

static int vctl_hit_hotbar(float px, float py)
{
    for (int i = 0; i < sVctlHotbarSlots; i++) {
        float x0, y0, x1, y1;
        if (!vctl_hotbar_rect(i, &x0, &y0, &x1, &y1)) continue;
        if (vctl_pt_in(px, py, x0, y0, x1, y1)) return i;
    }
    return -1;
}

/* 释放上一次没放完的键（在泵的开头调用 -> 保证 PRESS/RELEASE 不同帧） */
static void vctl_flush_pending(void)
{
    if (sVctlHotbarPendKey >= 0) {
        vctl_inject_key(sVctlHotbarPendKey, 0);
        if (sVctlDebug) VCTL_LOG("热键栏 GLFW_KEY_%d 释放", sVctlHotbarPendKey);
        sVctlHotbarPendKey = -1;
    }
}

/* 切到第 idx 槽：注入 GLFW_KEY_(idx+1) 的 PRESS，RELEASE 留到下一帧 */
static void vctl_hotbar_select(int idx)
{
    if (!sVctlHotbarOn) return;
    if (idx < 0 || idx >= sVctlHotbarSlots) return;

    /* 上一次还没释放的先补一次释放，避免键卡住 */
    vctl_flush_pending();

    sVctlHotbarSel  = idx;
    sVctlHotbarDown = idx;
    sVctlHotbarPendKey = 49 + idx;                     /* GLFW_KEY_1 == 49 */
    vctl_inject_key(sVctlHotbarPendKey, 1);
    if (sVctlDebug)
        VCTL_LOG("热键栏 -> 槽%d（GLFW_KEY_%d 按下，下一帧释放）", idx + 1, sVctlHotbarPendKey);
}

/* ------------------------------------------------------------------ */
/* 命中测试                                                           */
/* ------------------------------------------------------------------ */
static int vctl_hit_button(float px, float py)
{
    int W = sWin.width, H = sWin.height;
    if (W <= 0 || H <= 0) return -1;
    /* 倒序遍历：后定义的（备用键）在重叠时优先 */
    for (int i = sVctlBtnN - 1; i >= 0; i--) {
        VctlBtn *b = &sVctlBtn[i];
        if (b->w <= 0.0f || b->h <= 0.0f) continue;
        float x0, y0, x1, y1;
        vctl_geo(b->x, b->y, b->w, b->h, W, H, &x0, &y0, &x1, &y1);
        if (vctl_pt_in(px, py, x0, y0, x1, y1)) return i;
    }
    return -1;
}

static int vctl_hit_stick(float px, float py)
{
    int W = sWin.width, H = sWin.height;
    if (W <= 0 || H <= 0) return 0;
    if (sVctlStick.w <= 0.0f || sVctlStick.h <= 0.0f) return 0;
    float x0, y0, x1, y1;
    vctl_geo(sVctlStick.x, sVctlStick.y, sVctlStick.w, sVctlStick.h, W, H, &x0, &y0, &x1, &y1);
    return vctl_pt_in(px, py, x0, y0, x1, y1);
}

/* 视角区：默认整屏（按钮/摇杆已在前面被优先命中）；配置了 look_zone 则额外限制 */
static int vctl_in_look_zone(float nx, float ny)
{
    if (!sVctlHasLookZone) return 1;
    float xm = nx * 1000.0f, ym = ny * 1000.0f;
    return (xm >= sVctlLookZone[0] && xm <= sVctlLookZone[2] &&
            ym >= sVctlLookZone[1] && ym <= sVctlLookZone[3]);
}

static VctlTouch *vctl_touch_find(SDL_FingerID id)
{
    for (int i = 0; i < VCTL_MAX_TOUCH; i++) {
        if (sVctlTouch[i].id == id) return &sVctlTouch[i];
    }
    return NULL;
}

static VctlTouch *vctl_touch_alloc(SDL_FingerID id)
{
    for (int i = 0; i < VCTL_MAX_TOUCH; i++) {
        if (sVctlTouch[i].id == 0) {
            VctlTouch *t = &sVctlTouch[i];
            t->id = id; t->btn = -1; t->moved = 0;
            return t;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* 摇杆处理：按下点当圆心（FCL 的 CENTER_FOLLOW），位移决定方向           */
/* ------------------------------------------------------------------ */
static void vctl_stick_update(VctlTouch *t, float px, float py)
{
    int W = sWin.width, H = sWin.height;
    float x0, y0, x1, y1;
    vctl_geo(sVctlStick.x, sVctlStick.y, sVctlStick.w, sVctlStick.h, W, H, &x0, &y0, &x1, &y1);
    float r = (x1 - x0) < (y1 - y0) ? (x1 - x0) * 0.5f : (y1 - y0) * 0.5f;
    if (r < 8.0f) r = 8.0f;

    float dx = px - t->sx;
    float dy = py - t->sy;
    float len = sqrtf(dx * dx + dy * dy);

    float dead = r * 0.22f;          /* 死区：手指几乎没动时不走 */
    float minor = dead * 1.8f;       /* 副轴阈值更大，避免斜向误触双方向 */

    int bits = 0;
    if (len > dead) {
        if (fabsf(dx) > minor) bits |= (dx < 0 ? VCTL_DIR_LEFT : VCTL_DIR_RIGHT);
        if (fabsf(dy) > minor) bits |= (dy < 0 ? VCTL_DIR_UP   : VCTL_DIR_DOWN);
        if (bits == 0) {
            /* 只过了死区、没到副轴阈值：按主方向走 */
            if (fabsf(dx) >= fabsf(dy)) bits |= (dx < 0 ? VCTL_DIR_LEFT : VCTL_DIR_RIGHT);
            else                        bits |= (dy < 0 ? VCTL_DIR_UP   : VCTL_DIR_DOWN);
        }
    }
    vctl_stick_apply_bits(bits);
}

/* ------------------------------------------------------------------ */
/* 手指事件处理（唯一入口；返回 1 = 已消费，不再转给旧逻辑）             */
/* ------------------------------------------------------------------ */
static int vctl_handle_finger(const SDL_Event *e)
{
    SDL_FingerID id = e->tfinger.fingerID;
    float nx = e->tfinger.x, ny = e->tfinger.y;   /* SDL 已归一化到窗口 */
    int   type = (int)e->type;

    /* 配置惰性加载：本移植里 native glfwInit 不一定被执行，这里兜底读一次 */
    vctl_load_config();
    vctl_layout();

    if (type == SDL_EVENT_FINGER_DOWN && !(sVctlLogMask & 1)) {
        sVctlLogMask |= 1;
        VCTL_LOG("首个 FINGER_DOWN @(%.3f,%.3f) 光标模式=%s",
                 (double)nx, (double)ny,
                 sCursorMode == GLFW_CURSOR_DISABLED ? "DISABLED(游戏内)" : "NORMAL(菜单)");
    }
    if (type == SDL_EVENT_FINGER_MOTION && !(sVctlLogMask & 2)) {
        sVctlLogMask |= 2;
        VCTL_LOG("首个 FINGER_MOTION（拖动链路正常）");
    }

    if (type == SDL_EVENT_FINGER_DOWN) {
        VctlTouch *t = vctl_touch_find(id);
        if (t) return 1;
        t = vctl_touch_alloc(id);
        if (!t) return 1;

        t->lastX = nx; t->lastY = ny;
        t->downX = nx; t->downY = ny;

        int W = sWin.width, H = sWin.height;
        float px = nx * (float)(W > 0 ? W : 1);
        float py = ny * (float)(H > 0 ? H : 1);

        /* 按钮优先（摇杆与按钮不重叠，但顺序上仍先查按钮） */
        int bi = vctl_hit_button(px, py);
        if (bi >= 0) {
            t->kind = VCTL_T_BTN;
            t->btn  = bi;
            vctl_btn_apply(bi, 1);
            if (sVctlDebug)
                VCTL_LOG("手指 %llu 按下 %s(%s) @像素(%d,%d)",
                         (unsigned long long)id, sVctlBtn[bi].name, sVctlBtn[bi].label,
                         (int)px, (int)py);
            return 1;
        }

        /* ── 十字键（move_mode=dpad，默认）── */
        if (sVctlMoveMode == 1) {
            int inz = 0;
            int dbits = vctl_dpad_bits_at(px, py, &inz);
            if (inz) {
                t->kind = VCTL_T_DPAD;
                t->btn  = -1;                 /* 方向由 sVctlDpadBits/sVctlDpadDown[] 统一管理 */
                vctl_dpad_apply_bits(dbits);
                if (sVctlDebug)
                    VCTL_LOG("手指 %llu 按下十字键 方向=0x%x（0x0=中心/潜行）@像素(%d,%d)",
                             (unsigned long long)id, dbits, (int)px, (int)py);
                return 1;
            }
        }

        /* ── 热键栏（hotbar=1，默认）── */
        if (sVctlHotbarOn) {
            int hi = vctl_hit_hotbar(px, py);
            if (hi >= 0) {
                t->kind = VCTL_T_HOTBAR;
                t->btn  = hi;
                vctl_hotbar_select(hi);
                if (sVctlDebug)
                    VCTL_LOG("手指 %llu 按下热键栏槽%d @像素(%d,%d)",
                             (unsigned long long)id, hi + 1, (int)px, (int)py);
                return 1;
            }
        }

        if (sVctlMoveMode == 0 && vctl_hit_stick(px, py)) {
            t->kind = VCTL_T_STICK;
            sVctlStickHeld = 1;
            t->sx = px; t->sy = py;
            /* 双击方向键 -> 本次按住进入潜行（FCL 的 sneak:true） */
            if (sVctlStick.sneakOnDoubleTap) {
                Uint64 now = SDL_GetTicksNS();
                if (sVctlLastDirUpNs && now - sVctlLastDirUpNs < 400ULL * 1000000ULL) {
                    sVctlStickSneak = 1;
                    vctl_inject_key(340, 1);
                    if (sVctlDebug) VCTL_LOG("摇杆：双击 -> 潜行锁定");
                }
            }
            if (sVctlDebug)
                VCTL_LOG("手指 %llu 按下摇杆 @像素(%d,%d)", (unsigned long long)id, (int)px, (int)py);
            return 1;
        }

        if (vctl_in_look_zone(nx, ny)) {
            t->kind = VCTL_T_LOOK;
            if (sCursorMode != GLFW_CURSOR_DISABLED) vctl_set_cursor_abs(nx, ny);
            return 1;
        }

        t->kind = VCTL_T_NONE;
        return 1;
    }

    if (type == SDL_EVENT_FINGER_MOTION) {
        VctlTouch *t = vctl_touch_find(id);
        if (!t) return 1;

        int W = sWin.width, H = sWin.height;
        if (W <= 0 || H <= 0) { t->lastX = nx; t->lastY = ny; return 1; }

        double dxPx = (double)(nx - t->lastX) * (double)W;
        double dyPx = (double)(ny - t->lastY) * (double)H;
        t->lastX = nx; t->lastY = ny;

        if (!t->moved) {
            double ax = (double)(nx - t->downX) * (double)W;
            double ay = (double)(ny - t->downY) * (double)H;
            if (ax * ax + ay * ay > 24.0 * 24.0) t->moved = 1;
        }

        if (t->kind == VCTL_T_DPAD) {
            /* 手指在十字键上滑动 -> 换格（松开旧格、按下新格）；滑出去就全松开 */
            /* 按角度实时切方向：可以单轴，也可以斜向（斜向 = 两个方向键同时按下）；
             * 滑出十字键范围就全部松开。 */
            {
                int inz = 0;
                int dbits = vctl_dpad_bits_at(nx * (float)W, ny * (float)H, &inz);
                vctl_dpad_apply_bits(inz ? dbits : -1);
            }
        } else if (t->kind == VCTL_T_HOTBAR) {
            /* 手指在槽位条上滑动 -> 跟着换槽（像 PC 滚轮那样连滑） */
            int hi = vctl_hit_hotbar(nx * (float)W, ny * (float)H);
            if (hi >= 0 && hi != sVctlHotbarDown) {
                t->btn = hi;
                vctl_hotbar_select(hi);
                if (sVctlDebug) VCTL_LOG("热键栏 滑动 -> 槽%d", hi + 1);
            }
        } else if (t->kind == VCTL_T_STICK) {
            vctl_stick_update(t, nx * (float)W, ny * (float)H);
        } else if (t->kind == VCTL_T_LOOK && (dxPx != 0.0 || dyPx != 0.0)) {
            vctl_move_cursor(dxPx, dyPx);
            if (sVctlDebug)
                VCTL_LOG("视角拖动 d=(%.1f,%.1f)px -> 光标=(%.1f,%.1f)",
                         dxPx, dyPx, sCursorX, sCursorY);
        }
        return 1;
    }

    if (type == SDL_EVENT_FINGER_UP || type == SDL_EVENT_FINGER_CANCELED) {
        VctlTouch *t = vctl_touch_find(id);
        if (!t) return 1;

        if (t->kind == VCTL_T_BTN && t->btn >= 0) {
            vctl_btn_apply(t->btn, 0);
        } else if (t->kind == VCTL_T_DPAD) {
            vctl_dpad_apply_bits(-1);                    /* 方向 + 中心潜行一起松开 */
            t->btn = -1;
        } else if (t->kind == VCTL_T_HOTBAR) {
            sVctlHotbarDown = -1;      /* 选槽键由 vctl_flush_pending 负责释放 */
            t->btn = -1;
        } else if (t->kind == VCTL_T_STICK) {
            sVctlStickHeld = 0;
            vctl_stick_release();          /* 只释放摇杆自己的键 */
        } else if (t->kind == VCTL_T_LOOK && type == SDL_EVENT_FINGER_UP && !t->moved) {
            if (sCursorMode != GLFW_CURSOR_DISABLED) {
                /* 菜单里的“轻点”= 挪光标 + 左键单击 */
                vctl_set_cursor_abs(nx, ny);
                vctl_inject_mouse(0, 1);
                vctl_inject_mouse(0, 0);
                if (!(sVctlLogMask & 4)) {
                    sVctlLogMask |= 4;
                    VCTL_LOG("菜单轻点 @(%.3f,%.3f) -> 左键单击", (double)nx, (double)ny);
                }
            } else if (sVctlTapAttack) {
                /* 游戏内“轻点”= 左键单击（挖掘/攻击一次）。
                   MC 用 KeyMapping 轮询，press+release 必须分两次回调：
                   先 PRESS（click 计数 +1 → 下一 tick 触发 startAttack），
                   再 RELEASE（让 continueAttack(false) 复位 missTime）。 */
                vctl_inject_mouse(0, 1);
                vctl_inject_mouse(0, 0);
                if (!(sVctlLogMask & 8)) {
                    sVctlLogMask |= 8;
                    VCTL_LOG("游戏内轻点 @(%.3f,%.3f) -> 左键单击（tap_attack）", (double)nx, (double)ny);
                }
            }
        }

        t->id = 0; t->kind = VCTL_T_NONE; t->btn = -1;
        return 1;
    }

    return 0;
}

/* ================================================================== */
/* 可见浮层的 JNI 桥                                                    */
/* ------------------------------------------------------------------ */
/*  Java 侧类：com.example.mcart.VctlOverlay（只负责"画"，不吃触摸）      */
/*  ★ 命中判定仍然只在 native 这边算，Java 只来这里"查"几何与状态，       */
/*    这样浮层画出来的方框和真正能吃触摸的矩形永远是同一套数（单一真相）。 */
/*  线程：这些函数由 UI 线程调用，渲染线程同时在改状态；             */
/*        读的都是简单 int/float，且布局只在窗口尺寸变化时重算，安全。     */
/* ================================================================== */
#define VCTL_VIEW_CLASS "com/example/mcart/VctlOverlay"

static void vctl_jni_ensure(void)
{
    vctl_load_config();
    vctl_layout();
}

JNIEXPORT jint JNICALL
Java_com_example_mcart_VctlOverlay_nativeCount(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    vctl_jni_ensure();
    if (!sVctlEnabled) return 0;
    if (sWin.width <= 0 || sWin.height <= 0) return 0;   /* 窗口还没建好：先不画 */
    return (jint)sVctlBtnN;
}

JNIEXPORT jstring JNICALL
Java_com_example_mcart_VctlOverlay_nativeName(JNIEnv *env, jclass cls, jint i)
{
    (void)cls;
    if (!env || i < 0 || i >= sVctlBtnN) return NULL;
    return (*env)->NewStringUTF(env, sVctlBtn[i].name);
}

JNIEXPORT jstring JNICALL
Java_com_example_mcart_VctlOverlay_nativeLabel(JNIEnv *env, jclass cls, jint i)
{
    (void)cls;
    if (!env || i < 0 || i >= sVctlBtnN) return NULL;
    return (*env)->NewStringUTF(env, sVctlBtn[i].label);
}

/* 0 = 总是显示（ALWAYS，菜单里也画）  1 = 只在游戏内显示（IN_GAME） */
JNIEXPORT jint JNICALL
Java_com_example_mcart_VctlOverlay_nativeVis(JNIEnv *env, jclass cls, jint i)
{
    (void)env; (void)cls;
    if (i < 0 || i >= sVctlBtnN) return 0;
    return (jint)sVctlBtn[i].vis;
}

/* 按钮矩形（窗口像素）：out = {x, y, w, h}；禁用或窗口未好时全 0 */
JNIEXPORT void JNICALL
Java_com_example_mcart_VctlOverlay_nativeRect(JNIEnv *env, jclass cls, jint i, jfloatArray out)
{
    (void)cls;
    if (!env || !out || (*env)->GetArrayLength(env, out) < 4) return;
    vctl_jni_ensure();
    float r[4] = { 0, 0, 0, 0 };
    if (i >= 0 && i < sVctlBtnN &&
        sVctlBtn[i].w > 0.0f && sVctlBtn[i].h > 0.0f &&
        sWin.width > 0 && sWin.height > 0) {
        float x0, y0, x1, y1;
        vctl_geo(sVctlBtn[i].x, sVctlBtn[i].y, sVctlBtn[i].w, sVctlBtn[i].h,
                 sWin.width, sWin.height, &x0, &y0, &x1, &y1);
        r[0] = x0; r[1] = y0; r[2] = x1 - x0; r[3] = y1 - y0;
    }
    (*env)->SetFloatArrayRegion(env, out, 0, 4, r);
}

/* 摇杆矩形（窗口像素）：out = {x, y, w, h} */
JNIEXPORT void JNICALL
Java_com_example_mcart_VctlOverlay_nativeStickRect(JNIEnv *env, jclass cls, jfloatArray out)
{
    (void)cls;
    if (!env || !out || (*env)->GetArrayLength(env, out) < 4) return;
    vctl_jni_ensure();
    float r[4] = { 0, 0, 0, 0 };
    if (sVctlStick.w > 0.0f && sVctlStick.h > 0.0f &&
        sWin.width > 0 && sWin.height > 0) {
        float x0, y0, x1, y1;
        vctl_geo(sVctlStick.x, sVctlStick.y, sVctlStick.w, sVctlStick.h,
                 sWin.width, sWin.height, &x0, &y0, &x1, &y1);
        r[0] = x0; r[1] = y0; r[2] = x1 - x0; r[3] = y1 - y0;
    }
    (*env)->SetFloatArrayRegion(env, out, 0, 4, r);
}

/* 十字键某一格的像素矩形：idx 0=上 1=左 2=中心 3=右 4=下；out={x,y,w,h} */
JNIEXPORT void JNICALL
Java_com_example_mcart_VctlOverlay_nativeDpadRect(JNIEnv *env, jclass cls, jint idx, jfloatArray out)
{
    (void)cls;
    if (!env || !out || (*env)->GetArrayLength(env, out) < 4) return;
    vctl_jni_ensure();
    float r[4] = { 0, 0, 0, 0 };
    float x0, y0, x1, y1;
    if (vctl_dpad_rect((int)idx, &x0, &y0, &x1, &y1)) {
        r[0] = x0; r[1] = y0; r[2] = x1 - x0; r[3] = y1 - y0;
    }
    (*env)->SetFloatArrayRegion(env, out, 0, 4, r);
}

/*
 * 十字键 3x3 网格的**任意一格**矩形（给浮层画贴图用）。
 *   col: 0=左 1=中 2=右      row: 0=上 1=中 2=下
 * 说明：这里的几何是**贴图**用的格子；**命中判定不走格子**，
 * 而是 vctl_dpad_bits_at() 按角度在整个方块内算 8 向（见该函数注释）。
 * 浮层还可以在此基础上再往各自中心内缩一点留空隙，不影响判定。
 */
JNIEXPORT void JNICALL
Java_com_example_mcart_VctlOverlay_nativeDpadCellRect(JNIEnv *env, jclass cls,
                                                     jint col, jint row, jfloatArray out)
{
    (void)cls;
    if (!env || !out || (*env)->GetArrayLength(env, out) < 4) return;
    float r[4] = { 0, 0, 0, 0 };

    int W = sWin.width, H = sWin.height;
    if (W > 0 && H > 0 && sVctlDpad[2] > 0.0f && sVctlDpad[3] > 0.0f
        && col >= 0 && col <= 2 && row >= 0 && row <= 2) {
        float zx0, zy0, zx1, zy1;
        vctl_geo(sVctlDpad[0], sVctlDpad[1], sVctlDpad[2], sVctlDpad[3], W, H,
                 &zx0, &zy0, &zx1, &zy1);
        float S = (zx1 - zx0) < (zy1 - zy0) ? (zx1 - zx0) : (zy1 - zy0);
        float iv = (float)H * sVctlDpadInterval / 1000.0f;
        if (iv < 0.0f) iv = 0.0f;
        float cell = (S - 2.0f * iv) / 3.0f;
        if (cell < 8.0f) { iv = 0.0f; cell = S / 3.0f; }
        if (cell >= 4.0f) {
            float bx = zx0 + (float)col * (cell + iv);
            float by = zy0 + (float)row * (cell + iv);
            r[0] = bx; r[1] = by; r[2] = cell; r[3] = cell;
        }
    }
    (*env)->SetFloatArrayRegion(env, out, 0, 4, r);
}

/* ── 热键栏：给浮层用 ── */
JNIEXPORT jint JNICALL
Java_com_example_mcart_VctlOverlay_nativeHotbarOn(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return (jint)(sVctlHotbarOn ? 1 : 0);
}

JNIEXPORT jint JNICALL
Java_com_example_mcart_VctlOverlay_nativeHotbarCount(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return (jint)(sVctlHotbarOn ? sVctlHotbarSlots : 0);
}

JNIEXPORT jint JNICALL
Java_com_example_mcart_VctlOverlay_nativeHotbarSel(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return (jint)sVctlHotbarSel;      /* -1 = 还没选过 */
}

JNIEXPORT void JNICALL
Java_com_example_mcart_VctlOverlay_nativeHotbarRect(JNIEnv *env, jclass cls, jint idx, jfloatArray out)
{
    (void)cls;
    if (!env || !out || (*env)->GetArrayLength(env, out) < 4) return;
    float r[4] = { 0, 0, 0, 0 };
    float x0, y0, x1, y1;
    if (vctl_hotbar_rect((int)idx, &x0, &y0, &x1, &y1)) {
        r[0] = x0; r[1] = y0; r[2] = x1 - x0; r[3] = y1 - y0;
    }
    (*env)->SetFloatArrayRegion(env, out, 0, 4, r);
}

/* 运行状态位：
 *   bit0..bit15 : 第 i 个按钮当前是否按下（顺序 = native 按钮表顺序）
 *   bit16       : 1 = 光标 DISABLED（游戏内）  0 = NORMAL（菜单）
 *   bit17       : 1 = 有手指按在摇杆上
 *   bit18       : 1 = 虚拟键总开关打开
 */
JNIEXPORT jint JNICALL
Java_com_example_mcart_VctlOverlay_nativeState(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    int st = 0;
    for (int i = 0; i < sVctlBtnN && i < 16; i++) {
        if (sVctlBtn[i].down) st |= (1 << i);
    }
    if (sCursorMode == GLFW_CURSOR_DISABLED) st |= (1 << 16);
    if (sVctlStickHeld) st |= (1 << 17);
    if (sVctlEnabled)   st |= (1 << 18);
    /* bit19..bit23 = 十字键 上/左/中心/右/下 是否按下 */
    {
        int b = vctl_dpad_bits();
        st |= (b & 0x1F) << 19;
    }
    if (sVctlMoveMode == 1) st |= (1 << 24);   /* 1 = 当前是十字键模式 */
    return (jint)st;
}

/* 这个鼠标事件是不是「触摸合成」的？SDL 用 which == SDL_TOUCH_MOUSEID 标记 */
static int vctl_is_touch_mouse(const SDL_Event *e)
{
    if (e->type == SDL_EVENT_MOUSE_MOTION) return (e->motion.which == SDL_TOUCH_MOUSEID);
    if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        e->type == SDL_EVENT_MOUSE_BUTTON_UP) return (e->button.which == SDL_TOUCH_MOUSEID);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 时间 / 事件                                                          */
/* ------------------------------------------------------------------ */
double glfwGetTime(void)
{
    if (!sStartNs) sStartNs = SDL_GetTicksNS();
    return (double)(SDL_GetTicksNS() - sStartNs) / 1e9;
}

void glfwSetTime(double time)
{
    uint64_t now = SDL_GetTicksNS();
    sStartNs = now - (uint64_t)(time * 1e9);
}

uint64_t glfwGetTimerValue(void) { return SDL_GetTicksNS(); }
uint64_t glfwGetTimerFrequency(void) { return 1000000000ULL; }

static void deliverEvent(const SDL_Event *e)
{
    /* ── 虚拟触屏按键 / 虚拟鼠标（VCTL）总闸 ──
     * enabled=0 时整段跳过：行为与改动前完全一致。
     * enabled=1 时：
     *   · 手指事件由 vctl 全权处理（自己判定是「视角拖动」还是「虚拟按键」），
     *     不再往下走旧的「二指=右键」逻辑；
     *   · SDL 由触摸合成的鼠标事件一律丢弃，避免与上面的注入重复
     *     （物理鼠标的 which 不是 SDL_TOUCH_MOUSEID，照常走原路径）。 */
    if (sVctlEnabled && sWin.sdl) {
        switch (e->type) {
            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_FINGER_MOTION:
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_CANCELED:
                vctl_handle_finger(e);
                return;
            default:
                if (vctl_is_touch_mouse(e)) return;
                break;
        }
    }

    switch (e->type) {
        case SDL_EVENT_QUIT:
            sWin.shouldClose = 1;
            if (cbWinClose) cbWinClose(&sWin);
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            sWin.shouldClose = 1;
            if (cbWinClose) cbWinClose(&sWin);
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
            int w = e->window.data1, h = e->window.data2;
            sWin.width = w; sWin.height = h;
            if (cbWinSize) cbWinSize(&sWin, w, h);
            if (cbFbSize)  cbFbSize(&sWin, w, h);
            break;
        }
        case SDL_EVENT_WINDOW_MOVED:
            sWin.posx = e->window.data1; sWin.posy = e->window.data2;
            if (cbWinPos) cbWinPos(&sWin, sWin.posx, sWin.posy);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            sWin.focused = 1;
            if (cbWinFocus) cbWinFocus(&sWin, 1);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            sWin.focused = 0;
            /* 切后台/被抢焦点：虚拟键全部松开，回来时不会「莫名其妙一直走」 */
            if (sVctlEnabled) vctl_release_all();
            if (cbWinFocus) cbWinFocus(&sWin, 0);
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
            sWin.iconified = 1;
            if (cbWinIconify) cbWinIconify(&sWin, 1);
            break;
        case SDL_EVENT_WINDOW_RESTORED:
            sWin.iconified = 0;
            if (cbWinIconify) cbWinIconify(&sWin, 0);
            break;
        case SDL_EVENT_WINDOW_EXPOSED:
            if (cbWinRefresh) cbWinRefresh(&sWin);
            break;
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            if (cbCursorEnter) cbCursorEnter(&sWin, 1);
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            if (cbCursorEnter) cbCursorEnter(&sWin, 0);
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            int key = scancode_to_glfwkey(e->key.scancode);
            if (key < 0) break;
            int pressed = (e->type == SDL_EVENT_KEY_DOWN);
            int action  = pressed ? (e->key.repeat ? GLFW_REPEAT : GLFW_PRESS) : GLFW_RELEASE;
            if (key <= GLFW_KEY_LAST) sKeyState[key] = pressed;
            if (cbKey) cbKey(&sWin, key, (int)e->key.scancode, action, 0);
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            if (cbChar && e->text.text) {
                const char *p = e->text.text;
                /* UTF-8 -> codepoint（MC 只关心 ASCII 范围） */
                while (*p) {
                    unsigned int cp = (unsigned char)*p++;
                    if (cp < 0x80) {
                        if (cbChar) cbChar(&sWin, cp);
                    } else {
                        /* 简化：跳过续字节 */
                        while ((*p & 0xC0) == 0x80) p++;
                    }
                }
            }
            break;
        case SDL_EVENT_FINGER_DOWN:
            sTouchCount++;
            if (sTouchCount == 2 && !sRightHeld) {
                sRightHeld = 1;
                SHIMLOG("触摸: 二指 -> 右键按下");
                if (cbMouseButton) {
                    sMouseState[0] = GLFW_RELEASE;
                    sMouseState[1] = GLFW_PRESS;
                    cbMouseButton(&sWin, 0, GLFW_RELEASE, 0);   /* 先抬左键 */
                    cbMouseButton(&sWin, 1, GLFW_PRESS, 0);     /* 再按右键 */
                }
            }
            break;
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            if (sTouchCount > 0) sTouchCount--;
            if (sRightHeld && sTouchCount <= 1) {
                sRightHeld = 0;
                SHIMLOG("触摸: 松开右键 (剩 %d 指)", sTouchCount);
                if (cbMouseButton) {
                    sMouseState[1] = GLFW_RELEASE;
                    cbMouseButton(&sWin, 1, GLFW_RELEASE, 0);
                    if (sTouchCount == 1) {
                        sMouseState[0] = GLFW_PRESS;
                        cbMouseButton(&sWin, 0, GLFW_PRESS, 0);
                    }
                }
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            int btn = sdl_button_to_glfw((int)e->button.button);
            /* 右键手势期间忽略 SDL 合成的左键事件，否则会变成左右同按 */
            if (sRightHeld) { break; }
            int pressed = (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            if (btn >= 0 && btn <= GLFW_MOUSE_BUTTON_LAST) sMouseState[btn] = pressed;
            if (cbMouseButton) cbMouseButton(&sWin, btn, pressed ? GLFW_PRESS : GLFW_RELEASE, 0);
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
            if (sRightHeld) { break; }
            if (sCursorMode == GLFW_CURSOR_DISABLED) {
                /* 相对模式：用无界增量累积虚拟位置（鼠标/触摸都适用） */
                sCursorX += (double)e->motion.xrel;
                sCursorY += (double)e->motion.yrel;
            } else {
                /* 普通模式：绝对位置 */
                sCursorX = (double)e->motion.x;
                sCursorY = (double)e->motion.y;
            }
            sCursorPosValid = 1;
            if (cbCursorPos) cbCursorPos(&sWin, sCursorX, sCursorY);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (cbScroll) cbScroll(&sWin, e->wheel.x, e->wheel.y);
            break;
        default:
            break;
    }
}

void glfwPollEvents(void)
{
    SDL_Event e;
    /* 先冲刷上一帧遗留的「待释放键」（热键栏选槽）：
     * 放在泵开头才能保证 PRESS 与 RELEASE 落在**不同的泵周期**——
     * MC 的 KeyMapping 用 consumeClick() 轮询，同帧一按一放会被吃掉。 */
    vctl_flush_pending();
    while (SDL_PollEvent(&e)) deliverEvent(&e);
}

void glfwWaitEvents(void)
{
    if (SDL_WaitEvent(NULL)) glfwPollEvents();
}

void glfwWaitEventsTimeout(double timeout)
{
    SDL_Event e;
    if (SDL_WaitEventTimeout(&e, (Sint32)(timeout * 1000.0))) {
        deliverEvent(&e);
        glfwPollEvents();
    }
}

void glfwPostEmptyEvent(void) { SDL_PushEvent(&(SDL_Event){ .type = SDL_EVENT_USER }); }

/* ------------------------------------------------------------------ */
/* 输入查询                                                             */
/* ------------------------------------------------------------------ */
int glfwGetKey(GLFWwindow *window, int key)
{
    (void)window;
    if (key < 0 || key > GLFW_KEY_LAST) return GLFW_RELEASE;
    SDL_Scancode sc = (SDL_Scancode)glfwkey_to_scancode(key);
    const bool *ks = SDL_GetKeyboardState(NULL);
    if (ks && sc != SDL_SCANCODE_UNKNOWN) return ks[sc] ? GLFW_PRESS : GLFW_RELEASE;
    return sKeyState[key] ? GLFW_PRESS : GLFW_RELEASE;
}

int glfwGetMouseButton(GLFWwindow *window, int button)
{
    (void)window;
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return GLFW_RELEASE;
    float x, y;
    Uint32 mask = SDL_GetGlobalMouseState(&x, &y);
    int bit = 1u << button;
    return (mask & bit) ? GLFW_PRESS : GLFW_RELEASE;
}

void glfwGetCursorPos(GLFWwindow *window, double *xpos, double *ypos)
{
    (void)window;
    if (!sCursorPosValid) {
        float x = 0, y = 0;
        SDL_GetMouseState(&x, &y);
        sCursorX = x;
        sCursorY = y;
        sCursorPosValid = 1;
    }
    if (xpos) *xpos = sCursorX;
    if (ypos) *ypos = sCursorY;
}

int  glfwGetInputMode(GLFWwindow *window, int mode)
{
    (void)window;
    if (mode == GLFW_CURSOR) return sCursorMode;
    if (mode == GLFW_RAW_MOUSE_MOTION) return sRawMotion;
    return 0;
}

void glfwSetInputMode(GLFWwindow *window, int mode, int value)
{
    if (!window || !window->sdl) return;

    if (mode == GLFW_RAW_MOUSE_MOTION) {
        /* SDL 的相对模式本身就给原始增量，这里只记状态 */
        sRawMotion = (value != 0);
        return;
    }

    if (mode != GLFW_CURSOR) return;

    int v = value;
    if (v != GLFW_CURSOR_NORMAL && v != GLFW_CURSOR_HIDDEN && v != GLFW_CURSOR_DISABLED) {
        v = GLFW_CURSOR_NORMAL;
    }

    if (v == GLFW_CURSOR_DISABLED) {
        /* 进入相对模式：先把虚拟位置同步到当前真实位置，避免第一帧巨大 delta */
        float mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        sCursorX = mx;
        sCursorY = my;
        sCursorPosValid = 1;
        if (!SDL_SetWindowRelativeMouseMode(window->sdl, true)) {
            SHIMLOG("相对鼠标模式开启失败: %s", SDL_GetError());
            sCursorMode = GLFW_CURSOR_NORMAL;
            return;
        }
        SDL_HideCursor();
        sCursorMode = GLFW_CURSOR_DISABLED;
        /* 进/出游戏时把所有虚拟键松开，避免「按住 W 时开菜单」导致键卡住 */
        if (sVctlEnabled) vctl_release_all();
        SHIMLOG("GLFW_CURSOR_DISABLED -> SDL relative mouse ON");
        return;
    }

    /* 退出相对模式 / 普通 / 隐藏 */
    if (sCursorMode == GLFW_CURSOR_DISABLED) {
        SDL_SetWindowRelativeMouseMode(window->sdl, false);
        SHIMLOG("GLFW_CURSOR_NORMAL -> SDL relative mouse OFF");
    }
    if (v == GLFW_CURSOR_HIDDEN) {
        SDL_HideCursor();
    } else {
        SDL_ShowCursor();
    }
    sCursorMode = v;
    if (sVctlEnabled) vctl_release_all();   /* 同上：切模式即松开全部虚拟键 */
}

int glfwRawMouseMotionSupported(void) { return GLFW_TRUE; }

const char *glfwGetKeyName(int key, int scancode)
{
    (void)scancode;
    return SDL_GetScancodeName((SDL_Scancode)glfwkey_to_scancode(key));
}

int glfwGetKeyScancode(int key) { return glfwkey_to_scancode(key); }

void glfwSetCursorPos(GLFWwindow *window, double xpos, double ypos)
{
    if (sCursorMode == GLFW_CURSOR_DISABLED) {
        /* 相对模式：warp 会污染增量，只更新虚拟坐标 */
        sCursorX = xpos;
        sCursorY = ypos;
        sCursorPosValid = 1;
        return;
    }

    if (window && window->sdl) {
        SDL_WarpMouseInWindow(window->sdl, (float)xpos, (float)ypos);
        float mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);   /* 取回被钳制后的真实位置，避免下一次 delta 跳变 */
        sCursorX = mx;
        sCursorY = my;
        sCursorPosValid = 1;
    }
}

/* ------------------------------------------------------------------ */
/* 光标                                                                 */
/* ------------------------------------------------------------------ */
GLFWcursor *glfwCreateCursor(const void *image, int w, int h)
{
    (void)image; (void)w; (void)h;
    return NULL;
}

GLFWcursor *glfwCreateStandardCursor(int shape)
{
    (void)shape;
    return NULL;   /* 用系统默认光标即可 */
}

void glfwDestroyCursor(GLFWcursor *cursor) { (void)cursor; }

void glfwSetCursor(GLFWwindow *window, GLFWcursor *cursor)
{
    (void)window; (void)cursor;
}

/* ------------------------------------------------------------------ */
/* 显示器                                                               */
/* ------------------------------------------------------------------ */
static void ensureMode(void)
{
    SDL_DisplayID d = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode *m = SDL_GetCurrentDisplayMode(d);
    if (m) {
        sMode.width  = m->w;
        sMode.height = m->h;
        sMode.refreshRate = (int)(m->refresh_rate > 0 ? m->refresh_rate : 60);
    } else {
        sMode.width = 1080;
        sMode.height = 2400;
        sMode.refreshRate = 60;
    }
    sMode.redBits = 8; sMode.greenBits = 8; sMode.blueBits = 8;
}

GLFWmonitor **glfwGetMonitors(int *count)
{
    static GLFWmonitor *list[1];
    ensureMode();
    sMon.id = SDL_GetPrimaryDisplay();
    list[0] = &sMon;
    if (count) *count = 1;
    return list;
}

GLFWmonitor *glfwGetPrimaryMonitor(void)
{
    ensureMode();
    sMon.id = SDL_GetPrimaryDisplay();
    return &sMon;
}

void glfwGetMonitorPos(GLFWmonitor *monitor, int *xpos, int *ypos)
{
    (void)monitor;
    SDL_Rect r;
    if (SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &r)) {
        if (xpos) *xpos = r.x;
        if (ypos) *ypos = r.y;
    } else {
        if (xpos) *xpos = 0;
        if (ypos) *ypos = 0;
    }
}

void glfwGetMonitorWorkarea(GLFWmonitor *monitor, int *xpos, int *ypos, int *width, int *height)
{
    glfwGetMonitorPos(monitor, xpos, ypos);
    ensureMode();
    if (width)  *width  = sMode.width;
    if (height) *height = sMode.height;
}

void glfwGetMonitorPhysicalSize(GLFWmonitor *monitor, int *widthMM, int *heightMM)
{
    (void)monitor;
    if (widthMM)  *widthMM  = 0;
    if (heightMM) *heightMM = 0;
}

void glfwGetMonitorContentScale(GLFWmonitor *monitor, float *xscale, float *yscale)
{
    (void)monitor;
    if (xscale) *xscale = 1.0f;
    if (yscale) *yscale = 1.0f;
}

const char *glfwGetMonitorName(GLFWmonitor *monitor)
{
    (void)monitor;
    const char *n = SDL_GetDisplayName(SDL_GetPrimaryDisplay());
    return n ? n : "Android";
}

void glfwSetMonitorUserPointer(GLFWmonitor *monitor, void *pointer) { (void)monitor; (void)pointer; }
void *glfwGetMonitorUserPointer(GLFWmonitor *monitor) { (void)monitor; return NULL; }

const GLFWvidmode *glfwGetVideoMode(GLFWmonitor *monitor)
{
    (void)monitor;
    ensureMode();
    return &sMode;
}

const GLFWvidmode *glfwGetVideoModes(GLFWmonitor *monitor, int *count)
{
    (void)monitor;
    ensureMode();
    if (count) *count = 1;
    return &sMode;
}

void glfwSetGamma(GLFWmonitor *monitor, float gamma) { (void)monitor; (void)gamma; }

/* ------------------------------------------------------------------ */
/* 剪贴板                                                               */
/* ------------------------------------------------------------------ */
const char *glfwGetClipboardString(GLFWwindow *window)
{
    (void)window;
    static char *cached = NULL;
    if (cached) { SDL_free(cached); cached = NULL; }
    char *t = SDL_GetClipboardText();
    if (!t) return "";
    size_t n = strlen(t);
    cached = (char *)malloc(n + 1);
    if (cached) memcpy(cached, t, n + 1);
    SDL_free(t);
    return cached ? cached : "";
}

void glfwSetClipboardString(GLFWwindow *window, const char *string)
{
    (void)window;
    SDL_SetClipboardText(string ? string : "");
}

/* ------------------------------------------------------------------ */
/* 回调注册                                                             */
/* ------------------------------------------------------------------ */
#define CB_SETTER(NAME, TYPE, FIELD)                    \
    TYPE NAME(GLFWwindow *w, TYPE cb)                   \
    { (void)w; TYPE old = FIELD; FIELD = cb; return old; }

CB_SETTER(glfwSetKeyCallback,         GLFWkeyfun,          cbKey)
CB_SETTER(glfwSetCharCallback,        GLFWcharfun,         cbChar)
CB_SETTER(glfwSetCharModsCallback,    GLFWcharmodsfun,     cbCharMods)
CB_SETTER(glfwSetMouseButtonCallback, GLFWmousebuttonfun,  cbMouseButton)
CB_SETTER(glfwSetCursorPosCallback,   GLFWcursorposfun,    cbCursorPos)
CB_SETTER(glfwSetScrollCallback,      GLFWscrollfun,       cbScroll)
CB_SETTER(glfwSetFramebufferSizeCallback, GLFWframebuffersizefun, cbFbSize)
CB_SETTER(glfwSetWindowSizeCallback,  GLFWwindowsizefun,   cbWinSize)
CB_SETTER(glfwSetWindowPosCallback,   GLFWwindowposfun,    cbWinPos)
CB_SETTER(glfwSetWindowFocusCallback, GLFWwindowfocusfun,  cbWinFocus)
CB_SETTER(glfwSetWindowCloseCallback, GLFWwindowclosefun,  cbWinClose)
CB_SETTER(glfwSetWindowIconifyCallback, GLFWwindowiconifyfun, cbWinIconify)
CB_SETTER(glfwSetWindowRefreshCallback, GLFWwindowrefreshfun, cbWinRefresh)
CB_SETTER(glfwSetCursorEnterCallback, GLFWcursorenterfun,  cbCursorEnter)
CB_SETTER(glfwSetDropCallback,        GLFWdropfun,         cbDrop)
CB_SETTER(glfwSetPreeditCallback,     GLFWpreeditfun,      cbPreedit)
CB_SETTER(glfwSetIMEStatusCallback,   GLFWimestatusfun,    cbImeStatus)

GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun cb)
{
    GLFWerrorfun old = cbError;
    cbError = cb;
    return old;
}

GLFWmonitorfun glfwSetMonitorCallback(GLFWmonitorfun cb)
{
    GLFWmonitorfun old = cbMonitor;
    cbMonitor = cb;
    return old;
}

/* ------------------------------------------------------------------ */
/* MC 的 IME 扩展（SDL3 无对应功能，打桩）                                */
/* ------------------------------------------------------------------ */
void glfwSetPreeditCursorRectangle(GLFWwindow *w, int x, int y, int width, int height)
{
    (void)w; (void)x; (void)y; (void)width; (void)height;
}

void glfwGetPreeditCursorRectangle(GLFWwindow *w, int *x, int *y, int *width, int *height)
{
    (void)w;
    if (x) *x = 0;
    if (y) *y = 0;
    if (width) *width = 0;
    if (height) *height = 0;
}

void glfwResetPreeditText(GLFWwindow *w) { (void)w; }

const char *glfwGetPreeditCandidate(GLFWwindow *w, int index) { (void)w; (void)index; return NULL; }

void glfwSetPreeditCandidateCallback(GLFWwindow *w, void *cb) { (void)w; (void)cb; }

/* ================================================================== */
/* 补齐 GLFW 3.4 剩余必需符号                                          */
/*                                                                     */
/* GLFW$Functions 的 <clinit> 用 APIUtil.apiGetFunctionAddress（必需    */
/* 语义）为每个字段取地址，缺一个就抛 NullPointerException：            */
/*   "A required function is missing: glfwXxx"                         */
/* 下面这些原来没导出，导致 GLFW.<clinit> 直接失败。                     */
/* ================================================================== */

typedef void (*GLFWwindowcontentscalefun)(GLFWwindow *, float, float);
typedef void (*GLFWwindowmaximizefun)(GLFWwindow *, int);
typedef void (*GLFWjoystickfun)(int, int);

static GLFWwindowcontentscalefun cbWinContentScale;
static GLFWwindowmaximizefun     cbWinMaximize;
static GLFWjoystickfun           cbJoystick;

/* 分配器：SDL3 用自己的分配器，忽略 MC 传入的 GLFWallocator */
void glfwInitAllocator(const void *allocator) { (void)allocator; }

/* Android 窗口没有传统边框，全 0 */
void glfwGetWindowFrameSize(GLFWwindow *window, int *left, int *top, int *right, int *bottom)
{
    (void)window;
    if (left)   *left = 0;
    if (top)    *top = 0;
    if (right)  *right = 0;
    if (bottom) *bottom = 0;
}

/* 返回稳定的空串而不是 NULL，避免 MC 侧 NPE */
const char *glfwGetWindowTitle(GLFWwindow *window) { (void)window; return ""; }

void glfwIconifyWindow(GLFWwindow *window)
{
    if (window && window->sdl) SDL_MinimizeWindow(window->sdl);
}

GLFWwindowcontentscalefun glfwSetWindowContentScaleCallback(GLFWwindow *w, GLFWwindowcontentscalefun cb)
{
    (void)w;
    GLFWwindowcontentscalefun old = cbWinContentScale;
    cbWinContentScale = cb;
    return old;
}

GLFWwindowmaximizefun glfwSetWindowMaximizeCallback(GLFWwindow *w, GLFWwindowmaximizefun cb)
{
    (void)w;
    GLFWwindowmaximizefun old = cbWinMaximize;
    cbWinMaximize = cb;
    return old;
}

/* gamma ramp：Android 无此概念 */
const void *glfwGetGammaRamp(GLFWmonitor *monitor) { (void)monitor; return NULL; }
void glfwSetGammaRamp(GLFWmonitor *monitor, const void *ramp) { (void)monitor; (void)ramp; }

/* ---------------- 摇杆 / 手柄：暂全打桩（Android 无外接手柄） ---------------- */
int glfwJoystickPresent(int jid) { (void)jid; return GLFW_FALSE; }

const float *glfwGetJoystickAxes(int jid, int *count)
{
    (void)jid;
    if (count) *count = 0;
    return NULL;
}

const unsigned char *glfwGetJoystickButtons(int jid, int *count)
{
    (void)jid;
    if (count) *count = 0;
    return NULL;
}

const unsigned char *glfwGetJoystickHats(int jid, int *count)
{
    (void)jid;
    if (count) *count = 0;
    return NULL;
}

const char *glfwGetJoystickName(int jid) { (void)jid; return NULL; }
const char *glfwGetJoystickGUID(int jid) { (void)jid; return NULL; }

void  glfwSetJoystickUserPointer(int jid, void *pointer) { (void)jid; (void)pointer; }
void *glfwGetJoystickUserPointer(int jid) { (void)jid; return NULL; }

int glfwJoystickIsGamepad(int jid) { (void)jid; return GLFW_FALSE; }

const char *glfwGetGamepadName(int jid) { (void)jid; return NULL; }

int glfwGetGamepadState(int jid, void *state) { (void)jid; (void)state; return GLFW_FALSE; }

GLFWjoystickfun glfwSetJoystickCallback(GLFWjoystickfun cb)
{
    GLFWjoystickfun old = cbJoystick;
    cbJoystick = cb;
    return old;
}

int glfwUpdateGamepadMappings(const char *string) { (void)string; return GLFW_FALSE; }

/* ------------------------------------------------------------------ */
/* 软键盘开关（给 GlfwProbeActivity 的返回键用）                        */
/* ------------------------------------------------------------------ */
/* 只调 Java 的 InputMethodManager 是不够的：SDL 内部还有 text_input_active 状态，
 * 不调 SDL_StartTextInput() 的话键盘能弹出来但不会产生 SDL_EVENT_TEXT_INPUT，
 * MC 的文本框（世界种子/聊天/服务器地址）收不到字符。
 * SDL_StartTextInput 自己会去主线程 post ShowTextInputTask，跨线程调用安全。 */
JNIEXPORT void JNICALL
Java_com_example_mcart_GlfwProbeActivity_nativeSetSoftKeyboard(JNIEnv *env, jobject thiz, jboolean show)
{
    (void)env; (void)thiz;
    if (sWin.sdl == NULL) return;
    if (show == JNI_TRUE) {
        SDL_StartTextInput(sWin.sdl);
        SHIMLOG("软键盘: SDL_StartTextInput()");
    } else {
        SDL_StopTextInput(sWin.sdl);
        SHIMLOG("软键盘: SDL_StopTextInput()");
    }
}

/* ------------------------------------------------------------------ */
/* 内部渲染分辨率（给 GlfwProbeActivity 用）                            */
/* 必须在 glfwCreateWindow 之前调用；改了之后要重启游戏才生效。         */
/* ------------------------------------------------------------------ */
JNIEXPORT void JNICALL
Java_com_example_mcart_GlfwProbeActivity_nativeSetRenderScale(JNIEnv *env, jclass cls, jfloat scale)
{
    (void)env; (void)cls;
    sRenderScale = (scale > 0.2f && scale <= 1.0f) ? scale : 1.0f;
    SHIMLOG("nativeSetRenderScale -> %.3f", sRenderScale);
}
