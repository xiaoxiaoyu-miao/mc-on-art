#!/data/data/com.termux/files/usr/bin/bash
# 用 R8（shrink + optimize，不混淆）重建 MC 的 dex -> mcr8/
set -u
cd /data/data/com.termux/files/home/mc-work
export JAVA_HOME=/data/data/com.termux/files/usr/lib/jvm/java-21-openjdk
export PATH=$JAVA_HOME/bin:$PATH
AJ=/data/data/com.termux/files/usr/opt/android-sdk/platforms/android-35/android.jar
CPRULE="$(ls libs26/*.jar | tr '\n' ' ')"
rm -rf mcr8 && mkdir mcr8
java -Xmx6000m -cp /data/data/com.termux/files/usr/share/java/d8.jar com.android.tools.r8.R8 \
  --release --min-api 28 --lib "$AJ" \
  --classpath $CPRULE \
  --pg-conf mc-r8.pro \
  --output mcr8 \
  mcpatched.jar > mc-r8.log 2>&1
echo "exit=$?"
echo "r8 dex: $(ls mcr8/*.dex 2>/dev/null | wc -l) files, total $(du -sh mcr8 2>/dev/null | cut -f1)"
tail -5 mc-r8.log
chmod 444 mcr8/*.dex 2>/dev/null
