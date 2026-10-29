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
