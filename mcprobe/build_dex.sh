#!/data/data/com.termux/files/usr/bin/bash
# 编译 mcprobe/src/*.java -> dex/classes.dex
set -e
cd /data/data/com.termux/files/home/mc-work/mcprobe
export JAVA_HOME=/data/data/com.termux/files/usr/lib/jvm/java-21-openjdk
export PATH=$JAVA_HOME/bin:$PATH
AJ=/data/data/com.termux/files/usr/opt/android-sdk/platforms/android-36/android.jar
D8=/data/data/com.termux/files/usr/share/java/d8.jar
rm -rf build/classes && mkdir -p build/classes
CP="$AJ:../mc65.jar:../libs26/org.lwjgl_lwjgl_3.4.1_unsafe.jar:../libs26/org.jspecify_jspecify_1.0.0.jar:../libs26/org.lwjgl_lwjgl-glfw_3.4.1.jar:../libs26/org.lwjgl_lwjgl-opengl_3.4.1.jar"
# javax.sound.sampled 属于 JDK 的 java.desktop 模块，javac 不允许「直接编译这个包」。
# 单独用 --patch-module 编它（产物就是普通 class，d8 一样能收）。
javac -nowarn --patch-module java.desktop=src -d build/classes \
      src/javax/sound/sampled/AudioFormat.java

javac -nowarn -cp "$AJ:$CP:build/classes" -d build/classes \
      $(find src -name '*.java' ! -path 'src/javax/*')
rm -rf dex && mkdir -p dex
java -cp "$D8" com.android.tools.r8.D8 --min-api 28 --lib "$AJ" \
     --output dex $(find build/classes -name '*.class')
ls -l dex/
