#!/data/data/com.termux/files/usr/bin/bash
# 26.2-client.jar -> 26.2-src-e（反编译源码；make_patches.py 的唯一输入）
# 反编译器：Vineflower（Apache-2.0）。首次运行会下载到 tools/ 下。
set -eu
cd "$(dirname "$0")/.."

JAR=26.2-client.jar
OUT=26.2-src-e
VF=tools/vineflower.jar
VF_VER=1.11.1
VF_URL="https://repo1.maven.org/maven2/org/vineflower/vineflower/${VF_VER}/vineflower-${VF_VER}.jar"

[ -f "$JAR" ] || { echo "!! 缺 $JAR（scripts/fetch_client_and_libs.py --client）"; exit 1; }

mkdir -p tools
if [ ! -f "$VF" ]; then
  echo "下载 Vineflower ${VF_VER} ..."
  curl -fSL -o "$VF" "$VF_URL"
fi

rm -rf "$OUT"
mkdir -p "$OUT"
echo "反编译 -> $OUT（可能要几分钟）"
java -jar "$VF" --folder "$OUT" "$JAR"
echo "ok: $(find "$OUT" -name '*.java' | wc -l) 个 .java"
