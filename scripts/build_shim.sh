#!/data/data/com.termux/files/usr/bin/bash
# 编译 GLFW->SDL3 shim（glfwshim/glfw_sdl.c -> mcprobe/lib-arm64/libglfw.so）
#
# 依赖（按需改环境变量）：
#   ANDROID_NDK      NDK 路径（默认 Termux 的 ndk/29.0.14206865）
#   JAVA_HOME        JDK（需要 include/jni.h）
#   SDL3_INCLUDE     SDL3 头文件目录（vendor/SDL/include）
#   SDL3_LIB_DIR     含 libSDL3.so 的目录（通常是 mcprobe/lib-arm64）
set -eu
cd "$(dirname "$0")/.."

NDK=${ANDROID_NDK:-/data/data/com.termux/files/usr/opt/android-sdk/ndk/29.0.14206865}
JH=${JAVA_HOME:-/data/data/com.termux/files/usr/lib/jvm/java-21-openjdk}
SDL3_INC=${SDL3_INCLUDE:-vendor/SDL/include}
SDL3_LIB=${SDL3_LIB_DIR:-mcprobe/lib-arm64}
OUT=${OUT:-mcprobe/lib-arm64/libglfw.so}

CC=$NDK/toolchains/llvm/prebuilt/linux-aarch64/bin/aarch64-linux-android28-clang
[ -x "$CC" ] || CC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang
[ -x "$CC" ] || { echo "!! 找不到 NDK clang（设 ANDROID_NDK）"; exit 1; }
[ -d "$SDL3_INC" ] || { echo "!! 缺 SDL3 头文件目录（设 SDL3_INCLUDE）"; exit 1; }
[ -f "$SDL3_LIB/libSDL3.so" ] || { echo "!! 缺 $SDL3_LIB/libSDL3.so（先构建 SDL3）"; exit 1; }

"$CC" -shared -fPIC -O2 -o glfwshim/libglfw.so glfwshim/glfw_sdl.c \
  -I"$SDL3_INC" -I"$JH/include" -I"$JH/include/linux" \
  -L"$SDL3_LIB" -lSDL3 -llog -ldl -lm
cp glfwshim/libglfw.so "$OUT"
echo "ok: $OUT ($(stat -c%s "$OUT") bytes)"
