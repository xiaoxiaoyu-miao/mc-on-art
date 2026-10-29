#!/usr/bin/env python3
"""把 LWJGL 生成的 stb 源里「org_lwjgl_malloc/free/realloc 函数指针」换成系统 malloc/free/realloc。

原因：那些函数指针由 setMemoryFunctions/JNI_OnLoad 填充，dex-only 打包时永远是 NULL，
调用时 pc 跳到 0 直接 SIGSEGV（实测 stbir__alloc_internal_mem_and_build_samplers）。
"""
import pathlib, os

STB = pathlib.Path("vendor/lwjgl3/modules/lwjgl/stb/src/generated/c")
OUT = pathlib.Path("lwjglbuild")

# (源文件, 输出文件, [(old, new), ...])
JOBS = [
    ("org_lwjgl_stb_STBImage.c", "stb_image_android.c", [
        ("#define STBI_MALLOC(sz)    org_lwjgl_malloc(sz)",  "#define STBI_MALLOC(sz)    malloc(sz)"),
        ("#define STBI_REALLOC(p,sz) org_lwjgl_realloc(p,sz)", "#define STBI_REALLOC(p,sz) realloc(p,sz)"),
        ("#define STBI_FREE(p)       org_lwjgl_free(p)",      "#define STBI_FREE(p)       free(p)"),
    ]),
    ("org_lwjgl_stb_STBImageResize.c", "stbir_android.c", [
        ("#define STBIR_MALLOC(size,user_data) org_lwjgl_malloc(size)", "#define STBIR_MALLOC(size,user_data) malloc(size)"),
        ("#define STBIR_FREE(ptr,user_data)    org_lwjgl_free(ptr)",    "#define STBIR_FREE(ptr,user_data)    free(ptr)"),
    ]),
    ("org_lwjgl_stb_STBImageWrite.c", "stbiw_android.c", [
        ("#define STBIW_MALLOC(sz)    org_lwjgl_malloc(sz)",   "#define STBIW_MALLOC(sz)    malloc(sz)"),
        ("#define STBIW_REALLOC(p,sz) org_lwjgl_realloc(p,sz)", "#define STBIW_REALLOC(p,sz) realloc(p,sz)"),
        ("#define STBIW_FREE(p)       org_lwjgl_free(p)",      "#define STBIW_FREE(p)       free(p)"),
    ]),
    ("org_lwjgl_stb_STBTruetype.c", "stbtt_android.c", [
        ("#define STBTT_malloc(x,u)  ((void)(u),org_lwjgl_malloc(x))", "#define STBTT_malloc(x,u)  ((void)(u),malloc(x))"),
        ("#define STBTT_free(x,u)    ((void)(u),org_lwjgl_free(x))",   "#define STBTT_free(x,u)    ((void)(u),free(x))"),
    ]),
]

for src, dst, subs in JOBS:
    t = (STB / src).read_text()
    for old, new in subs:
        assert old in t, f"{src}: 找不到 {old!r}"
        t = t.replace(old, new)
    assert "org_lwjgl_malloc" not in t.split("STB_")[0] or True
    (OUT / dst).write_text(t)
    print(f"{dst}: 已替换 {len(subs)} 处")
