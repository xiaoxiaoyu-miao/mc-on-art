#!/data/data/com.termux/files/usr/bin/bash
set -e
cd "$(dirname "$0")"
AJ=${ANDROID_JAR:-/data/data/com.termux/files/usr/opt/android-sdk/platforms/android-36/android.jar}
# 资源（settings 图标等）：res/ -> .flat -> link
FLATS=""
if [ -d res ]; then
  rm -rf res.compiled && mkdir -p res.compiled
  aapt2 compile --dir res -o res.compiled
  FLATS=$(ls res.compiled/*.flat 2>/dev/null | tr '\n' ' ')
fi
aapt2 link -o base.apk --manifest AndroidManifest.xml -I "$AJ" --min-sdk-version 21 --target-sdk-version 36 $FLATS
rm -rf stage && mkdir -p stage
cp dex/classes.dex stage/classes.dex
if [ -d assets/assets ]; then cp -r assets/assets stage/assets; fi
if [ -d lib-arm64 ]; then mkdir -p stage/lib/arm64-v8a && cp lib-arm64/*.so stage/lib/arm64-v8a/; fi
# ICU4J 数据文件：dex 里没有这些 classpath 资源（ubidi.icu 等），
# 必须原样放进 APK 根，否则 Bidi/Normalizer 初始化 NPE
if [ ! -d icures/com ]; then
  python3 ../extract_jarres.py ../libs26/com.ibm.icu_icu4j_78.3.jar icures com/ibm/icu/impl/data/
fi
cp -r icures/com stage/
# log4j / slf4j 的 META-INF/services 注册文件（dex 打包会丢掉，缺了 MC 日志全是 NOP）
if [ ! -d ../logres/META-INF ]; then
  python3 ../gen_logres.py ../logres
fi
cp -r ../logres/META-INF stage/
# log4j 配置（否则默认只打 ERROR，看不到 INFO/DEBUG 的诊断信息）
cp log4j2.xml stage/

i=1
for f in ../mcdex2/classes*.dex ../libsdex/classes*.dex; do
  i=$((i+1)); cp "$f" "stage/classes$i.dex"
done
python3 - <<'PY'
import zipfile, os
src = "base.apk"; dst = "unsigned.apk"
os.path.exists(dst) and os.remove(dst)
with zipfile.ZipFile(src) as zin, zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as zout:
    for it in zin.infolist():
        zout.writestr(it, zin.read(it.filename))
    for root, dirs, files in os.walk("stage"):
        for f in sorted(files):
            full = os.path.join(root, f)
            zout.write(full, os.path.relpath(full, "stage"))
print("packed", os.path.getsize(dst))
PY
[ -f debug.keystore ] || keytool -genkeypair -keystore debug.keystore -storepass android -keypass android -alias k -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=MCART" >/dev/null 2>&1
apksigner sign --ks debug.keystore --ks-pass pass:android --key-pass pass:android --ks-key-alias k --min-sdk-version 28 --out mc-on-art.apk unsigned.apk
apksigner verify mc-on-art.apk >/dev/null && echo "SIGNED OK"
ls -lh mc-on-art.apk
