/* 最小验证：SDL3 能否在 app 进程里初始化、建窗、建 GL 上下文 */
#include <jni.h>
#include <android/log.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengles2.h>

#define TAG "SDLPROBE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static SDL_Window   *g_win = NULL;
static SDL_GLContext g_ctx = NULL;

JNIEXPORT jint JNICALL
Java_com_example_mcart_SdlProbeActivity_nProbe(JNIEnv *env, jclass clazz)
{
    (void)env; (void)clazz;
    /* SDL3: 返回 bool，true 表示成功 */
    bool ok = SDL_Init(SDL_INIT_VIDEO);
    LOGI("SDL_Init(VIDEO) = %d  err='%s'", (int)ok, SDL_GetError());
    if (!ok) return -1;

    int ver = SDL_GetVersion();
    LOGI("SDL version %d.%d.%d (runtime %d)",
         SDL_VERSIONNUM_MAJOR(ver), SDL_VERSIONNUM_MINOR(ver), SDL_VERSIONNUM_MICRO(ver), ver);
    LOGI("SDL built  %d.%d.%d", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
    LOGI("video driver = %s", SDL_GetCurrentVideoDriver());

    int ndisp = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&ndisp);
    LOGI("displays = %d", ndisp);
    if (displays) {
        for (int i = 0; i < ndisp; i++) {
            SDL_Rect r;
            if (SDL_GetDisplayBounds(displays[i], &r)) {
                LOGI("  display[%d] = %dx%d", i, r.w, r.h);
            }
        }
        SDL_free(displays);
    }

    g_win = SDL_CreateWindow("mc-on-art", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    LOGI("SDL_CreateWindow = %p  err='%s'", (void *)g_win, SDL_GetError());
    if (!g_win) return -2;

    g_ctx = SDL_GL_CreateContext(g_win);
    LOGI("SDL_GL_CreateContext = %p  err='%s'", g_ctx, SDL_GetError());
    if (!g_ctx) return -3;

    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(g_win, &w, &h);
    LOGI("framebuffer = %dx%d", w, h);
    LOGI("GL_VERSION  = %s", (const char *)glGetString(0x1F02));
    LOGI("GL_RENDERER = %s", (const char *)glGetString(0x1F01));

    /* 清一帧，确认 swap 通路 */
    glClearColor(0.1f, 0.3f, 0.6f, 1.0f);
    glClear(0x00004000);
    SDL_GL_SwapWindow(g_win);
    LOGI("swap ok");
    return 0;
}

JNIEXPORT void JNICALL
Java_com_example_mcart_SdlProbeActivity_nQuit(JNIEnv *env, jclass clazz)
{
    (void)env; (void)clazz;
    if (g_ctx) { SDL_GL_DestroyContext(g_ctx); g_ctx = NULL; }
    if (g_win) { SDL_DestroyWindow(g_win); g_win = NULL; }
    SDL_Quit();
    LOGI("SDL quit done");
}
