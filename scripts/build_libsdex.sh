#!/data/data/com.termux/files/usr/bin/bash
# libs26 -> libs-patched（打上 ART 适配的 MemoryUtil）-> libsdex（d8）
set -eu
cd "$(dirname "$0")/.."

export JAVA_HOME=${JAVA_HOME:-/data/data/com.termux/files/usr/lib/jvm/java-21-openjdk}
export PATH=$JAVA_HOME/bin:$PATH
AJ=${ANDROID_JAR:-/data/data/com.termux/files/usr/opt/android-sdk/platforms/android-36/android.jar}
D8=${D8_JAR:-/data/data/com.termux/files/usr/share/java/d8.jar}
UNSAFE=org.lwjgl_lwjgl_3.4.1_unsafe.jar

[ -d libs26 ] || { echo "!! 缺 libs26/（scripts/fetch_client_and_libs.py --libs）"; exit 1; }

if [ ! -d libs-patched ]; then
  echo "① 复制 libs26 -> libs-patched"
  cp -r libs26 libs-patched
fi

echo "② 编译 ART 适配版 MemoryUtil 并覆盖进 unsafe jar"
rm -rf lwjglbuild/patch/out && mkdir -p lwjglbuild/patch/out
javac -nowarn -cp "libs-patched/$UNSAFE:libs-patched/org.jspecify_jspecify_1.0.0.jar" \
      -d lwjglbuild/patch/out lwjglbuild/patch/MemoryUtil.java
( cd lwjglbuild/patch/out && zip -q -r "$(cd ../../.. && pwd)/libs-patched/$UNSAFE" org/lwjgl/system )

echo "③ d8 -> libsdex（几分钟）"
rm -rf libsdex && mkdir libsdex
java -Xmx3000m -cp "$D8" com.android.tools.r8.D8 --min-api 28 --lib "$AJ" \
     --output libsdex libs-patched/*.jar > libs-d8.log 2>&1
echo "   libsdex: $(ls libsdex/*.dex | wc -l) 个 dex"
