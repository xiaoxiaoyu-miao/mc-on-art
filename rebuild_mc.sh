#!/data/data/com.termux/files/usr/bin/bash
set -u
cd /data/data/com.termux/files/home/mc-work
AJ=/data/data/com.termux/files/usr/opt/android-sdk/platforms/android-35/android.jar
rm -rf mcdex2 && mkdir mcdex2
java -Xmx3000m -cp /data/data/com.termux/files/usr/share/java/d8.jar com.android.tools.r8.D8 \
  --min-api 28 --lib "$AJ" --output mcdex2 mcpatched.jar > mc-d8.log 2>&1
echo "mc dex: $(ls mcdex2/*.dex 2>/dev/null | wc -l) files"
chmod 444 mcdex2/*.dex 2>/dev/null
