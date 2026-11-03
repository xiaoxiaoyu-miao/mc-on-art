#!/data/data/com.termux/files/usr/bin/bash
# mcpatched.jar -> d8 -> mcdex2（MC 补丁版 dex）
set -u
cd "$(dirname "$0")"

export JAVA_HOME=${JAVA_HOME:-/data/data/com.termux/files/usr/lib/jvm/java-21-openjdk}
export PATH=$JAVA_HOME/bin:$PATH
AJ=${ANDROID_JAR:-/data/data/com.termux/files/usr/opt/android-sdk/platforms/android-36/android.jar}
D8=${D8_JAR:-/data/data/com.termux/files/usr/share/java/d8.jar}

[ -f mcpatched.jar ] || { echo "!! 缺 mcpatched.jar（先跑 ./build_all.sh 到第 ③ 步，或 cp mc65.jar mcpatched.jar 后 zip 补丁 class）"; exit 1; }

rm -rf mcdex2 && mkdir mcdex2
java -Xmx3000m -cp "$D8" com.android.tools.r8.D8 \
  --min-api 28 --lib "$AJ" --output mcdex2 mcpatched.jar > mc-d8.log 2>&1
echo "mc dex: $(ls mcdex2/*.dex 2>/dev/null | wc -l) files"
chmod 444 mcdex2/*.dex 2>/dev/null
