#!/data/data/com.termux/files/usr/bin/bash
set -u
cd /data/data/com.termux/files/home/mc-work
AJ=/data/data/com.termux/files/usr/opt/android-sdk/platforms/android-35/android.jar
rm -rf libsdex && mkdir libsdex
java -Xmx3000m -cp /data/data/com.termux/files/usr/share/java/d8.jar com.android.tools.r8.D8 \
  --min-api 28 --lib "$AJ" --output libsdex libs-patched/*.jar > libs-d8.log 2>&1
echo "libs dex: $(ls libsdex/*.dex 2>/dev/null | wc -l)"
chmod 444 libsdex/*.dex
