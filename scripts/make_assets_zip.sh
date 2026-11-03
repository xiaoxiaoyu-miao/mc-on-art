#!/data/data/com.termux/files/usr/bin/bash
# 【自用版】把 /sdcard/.minecraft/assets（indexes + objects，约 471MB）打成
# mcprobe/assets/mcassets.zip，随后 ./mcprobe/pack.sh 会把它带进 APK，
# 首次启动自动解压回 /sdcard/.minecraft/assets。
#
# ⚠️ 打出来的 APK 含 Mojang 资产，只能自用，不要分发。
#
# 用法：
#   bash scripts/make_assets_zip.sh            # 从默认目录打包
#   bash scripts/make_assets_zip.sh <assets目录>
#   bash scripts/make_assets_zip.sh --clean    # 删掉已打包的 zip（回到常规版）
set -eu
cd "$(dirname "$0")/.."

OUT=mcprobe/assets/mcassets.zip

if [ "${1:-}" = "--clean" ]; then
  rm -f "$OUT"
  echo "已删除 $OUT（回到常规版：APK 不再内嵌 assets）"
  exit 0
fi

# 默认就是设备上 MC 用的资源目录
SRC=${1:-/sdcard/.minecraft/assets}

[ -d "$SRC/indexes" ] || { echo "!! 缺 $SRC/indexes"; exit 1; }
[ -d "$SRC/objects" ] || { echo "!! 缺 $SRC/objects"; exit 1; }

mkdir -p mcprobe/assets
rm -f "$OUT"

echo "打包 $SRC -> $OUT（-0 仅存储，471MB 级别建议如此）"
( cd "$SRC" && zip -0 -q -r - . ) > "$OUT" 2>/dev/null || {
  # 某些环境 zip 不接受 '-' 输出，退回两步走
  TMP=$(mktemp -d)
  ( cd "$SRC" && zip -0 -q -r "$OLDPWD/$OUT" indexes objects )
  rm -rf "$TMP"
}

echo "ok: $(du -h "$OUT" | cut -f1)，条目 $(unzip -l "$OUT" | tail -1)"
echo "接下来 ./mcprobe/pack.sh 会产出 mc-on-art-assets.apk"
