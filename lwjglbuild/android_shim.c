#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

JNIEXPORT jlong JNICALL Java_org_lwjgl_system_MemoryUtil_nGetDirectBufferAddress(JNIEnv *env, jclass clazz, jobject buffer) {
    (void)clazz;
    return (jlong)(intptr_t)(*env)->GetDirectBufferAddress(env, buffer);
}

/*
 * ART 移植诊断：直接暴露 dlopen/dlerror 的原始行为。
 *
 * 目的：LWJGL 的 LinuxLibrary.loadLibrary 在 dlopen 返回 NULL 后调 dlerror()，
 * 拿到 "undefined symbol: Java_org_lwjgl_system_linux_DynamicLinkLoader_ndlerror__"，
 * 但 libglfw.so 的 .dynsym 里根本没有这个未定义符号（48 个 UND 全是 SDL_xxx/LIBC）。
 * 所以要么 dlerror 是残值/被污染，要么 dlopen 的失败另有原因。
 * 这里把 handle、立即 dlerror、二次 dlerror、dlsym 结果一次性带回去。
 */
JNIEXPORT jstring JNICALL Java_com_example_mcart_GlfwProbeActivity_probeDlopen(
        JNIEnv *env, jclass clazz, jstring jpath, jint mode) {
    (void)clazz;
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    char *buf = (char *)malloc(4096);
    size_t n = 0;
    if (!buf) {
        (*env)->ReleaseStringUTFChars(env, jpath, path);
        return NULL;
    }
    buf[0] = 0;

    #define APP(...) do { if (n < 3900) { int _w = snprintf(buf + n, 4000 - n, __VA_ARGS__); \
        if (_w > 0) n += (size_t)_w; } } while (0)

    APP("path=%s mode=0x%x || ", path, (unsigned)mode);

    /* 0. 先清空历史错误，保证后面读到的是本次的 */
    dlerror();

    /* 1. dlopen + 立刻读错误 */
    void *h = dlopen(path, (int)mode);
    const char *e1 = dlerror();
    APP("dlopen=%p || err1=%s || ", h, e1 ? e1 : "(NULL/no-error)");

    /* 2. 再读一次，确认已被消费 */
    const char *e2 = dlerror();
    APP("err2=%s || ", e2 ? e2 : "(NULL)");

    if (h) {
        dlerror();
        void *s1 = dlsym(h, "SDL_Init");
        const char *e3 = dlerror();
        APP("dlsym(SDL_Init)=%p err=%s || ", s1, e3 ? e3 : "(null)");

        dlerror();
        void *s2 = dlsym(h, "glfwInit");
        const char *e4 = dlerror();
        APP("dlsym(glfwInit)=%p err=%s", s2, e4 ? e4 : "(null)");
    } else {
        /* 失败：换 flag 组合再试，看是不是 flag 的问题 */
        dlerror();
        void *h2 = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        const char *e5 = dlerror();
        APP("retry(NOW|LOCAL)=%p err=%s || ", h2, e5 ? e5 : "(null)");
        if (h2) dlclose(h2);

        dlerror();
        void *h3 = dlopen(path, RTLD_LAZY | RTLD_GLOBAL);
        const char *e6 = dlerror();
        APP("retry(LAZY|GLOBAL)=%p err=%s", h3, e6 ? e6 : "(null)");
    }
    #undef APP

    (*env)->ReleaseStringUTFChars(env, jpath, path);
    jstring out = (*env)->NewStringUTF(env, buf);
    free(buf);
    return out;
}
