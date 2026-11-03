#!/data/data/com.termux/files/usr/bin/bash
# 一条龙：补丁源码 -> 补丁编译 -> mcpatched.jar -> dex -> APK
#
#   ./build_all.sh              # 全套
#   ./build_all.sh --no-pack    # 只做到 mcdex2（APK 手动打包）
#
# 前置（缺一不可，见 docs/REPRODUCE.md）：
#   mc65.jar            脚本 scripts/make_mc65.py 生成
#   26.2-src-e/         scripts/decompile.sh 反编译得到
#   libs26/             scripts/fetch_client_and_libs.py --libs
#   mcprobe/lib-arm64/  原生库（libglfw.so 用 scripts/build_shim.sh 重编）
#   libsdex/            依赖库 dex（首次可用 scripts/build_libsdex.sh 生成）
set -eu
cd "$(dirname "$0")"

export JAVA_HOME=${JAVA_HOME:-/data/data/com.termux/files/usr/lib/jvm/java-21-openjdk}
export PATH=$JAVA_HOME/bin:$PATH
AJ=${ANDROID_JAR:-/data/data/com.termux/files/usr/opt/android-sdk/platforms/android-36/android.jar}

[ -f mc65.jar ] || { echo "!! 缺 mc65.jar（scripts/make_mc65.py）"; exit 1; }
[ -d 26.2-src-e ] || { echo "!! 缺 26.2-src-e/（scripts/decompile.sh）"; exit 1; }

echo "=== ① 生成补丁源码 ==="
_OUT=$(python3 make_patches.py 2>&1) || { echo "$_OUT" | tail -20; exit 1; }
if echo "$_OUT" | grep -q "pattern missing"; then
  echo "!! 补丁原文不匹配（MC 版本不一致？）："
  echo "$_OUT" | grep "pattern missing"
  exit 1
fi
echo "   ok（$(echo "$_OUT" | grep -c '^patched') 个文件）"

echo "=== ② 编译补丁 ==="
CP="mc65.jar:$AJ:$(ls libs26/*.jar | tr '\n' ':')"
rm -rf patch/out && mkdir -p patch/out
javac -nowarn -cp "$CP" -d patch/out $(find patch/src -name '*.java')
echo "   ok（$(find patch/out -name '*.class' | wc -l) 个 class）"

echo "=== ③ 重建 mcpatched.jar ==="
cp mc65.jar mcpatched.jar
(cd patch/out && zip -q -r ../../mcpatched.jar com net 2>/dev/null || zip -q -r ../../mcpatched.jar net)
echo "   ok"

echo "=== ④ MC -> dex ==="
./rebuild_mc.sh

echo "=== ⑤ 探针工程 dex ==="
( cd mcprobe && ./build_dex.sh )

if [ "${1:-}" != "--no-pack" ]; then
  echo "=== ⑥ 打包 APK ==="
  ( cd mcprobe && ./pack.sh > /dev/null )
  echo "   ok: $(ls -la mcprobe/mc-on-art.apk | awk '{print $5}') bytes"
fi
echo "=== 完成 ==="
