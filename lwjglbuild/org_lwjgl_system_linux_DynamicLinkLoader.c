/*
 * Copyright LWJGL. All rights reserved.
 * License terms: https://www.lwjgl.org/license
 * MACHINE GENERATED FILE, DO NOT EDIT
 */
#include "common_tools.h"
#include <dlfcn.h>

/*
 * ===== ART / Android 移植补丁 =====
 *
 * bionic 的 dlopen 用 __builtin_return_address(0) 取调用者地址，再据此定位调用者
 * 所属 soinfo 的 linker namespace，并用该 namespace 解析 DT_NEEDED。
 *
 * 原始生成代码是 `return (jlong)(uintptr_t)dlopen(...)`，-O2 下被优化成 tail call
 * （反汇编是 `b dlopen@plt` 而非 `bl`）。于是 dlopen 的返回地址落在调用 ndlopen 的
 * ART JNI trampoline（libart.so）里，namespace 被解析成 `com_android_art`，
 * 而不是 app 自己的 `clns-N`。实测后果：
 *
 *   dlopen failed: library "libSDL3.so" not found:
 *     needed by .../libglfw.so in namespace com_android_art
 *
 * 因为 libSDL3.so 是 SDLActivity 用 System.loadLibrary 加载到 clns-N 的。
 *
 * 修复：用编译器屏障阻止 sibling-call 优化，让返回地址留在本 so 内。
 * （build_lwjgl.sh 另加 -fno-optimize-sibling-calls 双保险。）
 */
#define ART_NO_TAILCALL() __asm__ __volatile__("" ::: "memory")

EXTERN_C_ENTER

JNIEXPORT jlong JNICALL Java_org_lwjgl_system_linux_DynamicLinkLoader_ndlopen(JNIEnv *__env, jclass clazz, jlong filenameAddress, jint mode) {
    char const *filename = (char const *)(uintptr_t)filenameAddress;
    UNUSED_PARAMS(__env, clazz)
    void *handle = dlopen(filename, mode);
    ART_NO_TAILCALL();
    return (jlong)(uintptr_t)handle;
}

JNIEXPORT jlong JNICALL Java_org_lwjgl_system_linux_DynamicLinkLoader_ndlerror(JNIEnv *__env, jclass clazz) {
    UNUSED_PARAMS(__env, clazz)
    char *error = dlerror();
    ART_NO_TAILCALL();
    return (jlong)(uintptr_t)error;
}

JNIEXPORT jlong JNICALL Java_org_lwjgl_system_linux_DynamicLinkLoader_ndlsym(JNIEnv *__env, jclass clazz, jlong handleAddress, jlong nameAddress) {
    void *handle = (void *)(uintptr_t)handleAddress;
    char const *name = (char const *)(uintptr_t)nameAddress;
    UNUSED_PARAMS(__env, clazz)
    void *address = dlsym(handle, name);
    ART_NO_TAILCALL();
    return (jlong)(uintptr_t)address;
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_linux_DynamicLinkLoader_ndlclose(JNIEnv *__env, jclass clazz, jlong handleAddress) {
    void *handle = (void *)(uintptr_t)handleAddress;
    UNUSED_PARAMS(__env, clazz)
    int result = dlclose(handle);
    ART_NO_TAILCALL();
    return (jint)result;
}

EXTERN_C_EXIT
