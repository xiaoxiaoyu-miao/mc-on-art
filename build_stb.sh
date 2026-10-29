#!/data/data/com.termux/files/usr/bin/bash
# 重新编译 Android 版 liblwjgl_stb.so
set -e
cd /data/data/com.termux/files/home/mc-work
export JAVA_HOME=/data/data/com.termux/files/usr/lib/jvm/java-21-openjdk
NDK=/data/data/com.termux/files/usr/opt/android-sdk/ndk/29.0.14206865
CC=$NDK/toolchains/llvm/prebuilt/linux-aarch64/bin/aarch64-linux-android28-clang
STB=vendor/lwjgl3/modules/lwjgl/stb/src/generated/c
LW=lwjglbuild

python3 make_stb_android.py

$CC -shared -fPIC -O2 -fno-optimize-sibling-calls -DLWJGL_LINUX \
  -I$STB -Ivendor/lwjgl3/modules/lwjgl/stb/src/main/c \
  -Ivendor/lwjgl3/modules/lwjgl/core/src/main/c -Ivendor/lwjgl3/modules/lwjgl/core/src/main/c/linux -Ilwjglbuild \
  -I$JAVA_HOME/include -I$JAVA_HOME/include/linux \
  -o $LW/liblwjgl_stb.so \
  $LW/stb_image_android.c $LW/stbir_android.c $LW/stbiw_android.c $LW/stbtt_android.c \
  $STB/org_lwjgl_stb_LibSTB.c $STB/org_lwjgl_stb_STBDXT.c \
  $STB/org_lwjgl_stb_STBEasyFont.c $STB/org_lwjgl_stb_STBPerlin.c \
  $STB/org_lwjgl_stb_STBRectPack.c $STB/org_lwjgl_stb_STBTTFontinfo.c \
  $STB/org_lwjgl_stb_STBVorbis.c -lm

ls -l $LW/liblwjgl_stb.so
echo "== 残留的 org_lwjgl_ 未定义符号（应为空） =="
$NDK/toolchains/llvm/prebuilt/linux-aarch64/bin/llvm-nm -D -u $LW/liblwjgl_stb.so | grep -E "org_lwjgl_" || echo "(none)"
