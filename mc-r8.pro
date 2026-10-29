# ── R8 配置：MC on ART ──
# 原则：**不混淆**（-dontobfuscate）。MC 的 26.x 本来就没混淆，混淆会打断
#       ① LWJGL 的 JNI 符号（Java_org_lwjgl_...）② 反射/ServiceLoader ③ 记录类组件名
#       只做 shrink（删死代码）+ optimize（内联/去虚化/常量折叠）。

-dontobfuscate
-dontwarn **

# 保留属性（记录类组件、泛型签名、注解、内部类）
-keepattributes *Annotation*,Signature,InnerClasses,EnclosingMethod,Exceptions,SourceFile,LineNumberTable

# ── JNI / 反射 / 动态加载敏感（必须整包保留名字）──
-keep class org.lwjgl.** { *; }
-keep class org.libsdl.** { *; }
-keep class com.example.** { *; }
-keep class javax.sound.** { *; }

# ── 服务发现（META-INF/services）与日志 ──
-keep class org.slf4j.** { *; }
-keep class org.apache.logging.** { *; }
-keep class com.google.gson.** { *; }
-keep class com.ibm.icu.** { *; }
-keep class it.unimi.** { *; }

# ── 记录类（MC 大量用 record + Codec 反射读组件名）──
-keepclassmembers,allowoptimization class * implements java.lang.Record { <fields>; }

# ── MC 里明确靠 Class.forName / 反射 / 资源查找的类 ──
-keep class net.minecraft.SharedConstants { *; }
-keep class net.minecraft.server.Bootstrap { *; }
-keep class net.minecraft.client.ClientBootstrap { *; }
-keep class net.minecraft.client.main.** { *; }
-keep class net.minecraft.server.packs.** { *; }
-keep class net.minecraft.resources.** { *; }
-keep class net.minecraft.util.datafix.** { *; }
-keep class net.minecraft.datafixer.** { *; }
-keep class net.minecraft.locale.** { *; }
-keep class com.mojang.serialization.** { *; }
-keep class com.mojang.datafixers.** { *; }
-keep class com.mojang.blaze3d.** { *; }
-keep class com.mojang.realmsclient.** { *; }
-keep class com.mojang.logging.** { *; }

# 枚举/接口的 values()/valueOf() 反射
-keepclassmembers,allowoptimization enum * { *; }
