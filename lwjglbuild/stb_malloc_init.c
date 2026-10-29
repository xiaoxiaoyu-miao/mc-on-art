/*
 * liblwjgl_stb.so 的 malloc 指针初始化。
 *
 * LWJGL 的 stb 模块用「可覆盖的内存管理函数」：
 *   STBI_MALLOC(sz) -> org_lwjgl_malloc(sz)
 * 这些是函数指针全局变量（lwjgl_malloc.h）。正常发行版里由
 * Java_<Lib>_setupMalloc 在 Java 侧填入 Java 堆的 malloc/free；
 * 我们把 stb 单独编成一个 .so 时没有那条初始化路径，指针保持 NULL，
 * 于是 stbi__load_main -> stbi__malloc 直接调 NULL 崩溃
 * （实测 stbi__parse_png_file / stbi__load_main 段错误）。
 *
 * 这里用 constructor 直接指向 bionic 的 malloc 系列。
 * MC 只把 stb 当解码器（一次性分配、一次性释放），不需要跟 Java 的
 * MemoryUtil 共享分配器。
 */
#include <stdlib.h>
#include <android/log.h>
#include "lwjgl_malloc.h"

#define STBLOG(...) __android_log_print(ANDROID_LOG_INFO, "STBINIT", __VA_ARGS__)

__attribute__((constructor))
static void lwjgl_stb_setup_malloc(void)
{
    org_lwjgl_malloc        = malloc;
    org_lwjgl_calloc        = calloc;
    org_lwjgl_realloc       = realloc;
    org_lwjgl_free          = free;
    org_lwjgl_aligned_alloc = aligned_alloc;
    org_lwjgl_aligned_free  = free;
    STBLOG("setup: malloc=%p calloc=%p realloc=%p free=%p aa=%p af=%p",
          (void*)org_lwjgl_malloc, (void*)org_lwjgl_calloc, (void*)org_lwjgl_realloc,
          (void*)org_lwjgl_free, (void*)org_lwjgl_aligned_alloc, (void*)org_lwjgl_aligned_free);
}
