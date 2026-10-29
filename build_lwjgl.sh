#!/data/data/com.termux/files/usr/bin/bash
# 重新编译 Android 版 liblwjgl.so
set -e
cd /data/data/com.termux/files/home/mc-work/lwjglbuild
export JAVA_HOME=/data/data/com.termux/files/usr/lib/jvm/java-21-openjdk
NDK=/data/data/com.termux/files/usr/opt/android-sdk/ndk/29.0.14206865
CC=$NDK/toolchains/llvm/prebuilt/linux-aarch64/bin/aarch64-linux-android28-clang
TERMUXPREFIX=/data/data/com.termux/files/usr

$CC -shared -fPIC -O2 -fno-optimize-sibling-calls -DLWJGL_LINUX -I. -Iffi-include \
  -I$JAVA_HOME/include -I$JAVA_HOME/include/linux \
  -o liblwjgl.so \
  org_lwjgl_system_JNI.c org_lwjgl_system_MemoryAccessJNI.c \
  org_lwjgl_system_libc_LibCStdlib.c org_lwjgl_system_libc_LibCString.c \
  org_lwjgl_system_libc_LibCStdio.c org_lwjgl_system_jni_JNINativeInterface.c \
  org_lwjgl_system_MemoryUtil.c org_lwjgl_system_Upcalls.c common_tools.c \
  org_lwjgl_system_linux_DynamicLinkLoader.c \
  org_lwjgl_system_SharedLibraryUtil.c android_shim.c \
  org_lwjgl_system_ThreadLocalUtil.c \
  org_lwjgl_system_libffi_LibFFI.c org_lwjgl_system_libffi_FFICIF.c \
  org_lwjgl_system_libffi_FFIClosure.c \
  -L$TERMUXPREFIX/lib -lffi

NDKREADELF=$NDK/toolchains/llvm/prebuilt/linux-aarch64/bin/llvm-readelf
echo "== size =="; ls -l liblwjgl.so
echo "== NEEDED =="; $NDKREADELF -d liblwjgl.so | grep -E "NEEDED|SONAME"
echo "== 未解析的 ffi_ 符号（应为空） =="
$NDK/toolchains/llvm/prebuilt/linux-aarch64/bin/llvm-nm -D -u liblwjgl.so | grep -E "ffi_" || echo "(none)"
echo "== libffi JNI 入口 =="
$NDK/toolchains/llvm/prebuilt/linux-aarch64/bin/llvm-nm -D --defined-only liblwjgl.so | grep -c "libffi"
