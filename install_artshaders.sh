#!/data/data/com.termux/files/usr/bin/bash
# 把本仓库的「光影」资源装到 MC 读取的 assets-jar 里。
# 用途：① 平时改完 artshaders/ 就同步一次 ② extract_jarres.py 重跑后补回来
set -e
SRC=/data/data/com.termux/files/home/mc-work/artshaders
DST=/sdcard/.minecraft/assets-jar/assets/minecraft
mkdir -p "$DST/shaders/post" "$DST/post_effect"
cp "$SRC"/shaders/post/*.fsh        "$DST/shaders/post/"
cp "$SRC"/post_effect/*.json        "$DST/post_effect/"
echo "已安装到 $DST:"
ls -1 "$DST/shaders/post"/art_*.fsh
ls -1 "$DST/post_effect"/art_*.json
