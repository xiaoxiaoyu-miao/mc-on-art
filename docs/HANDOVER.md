# 交接文档：MC 26.2 → ART + SDL3 移植

> ⚠️ **新会话请先读 `HANDOVER-NEXT.md`**（本轮整理的完整独立交接文档，含环境/目录/已完成四块/
> 当前卡点与下一步）。本文档是**历史记录**，从第一轮到本轮的过程都在这里。

> 原「`MemoryUtil.write8` 非法地址」已定位并修复，`memAlloc/memPut/memGet/memFree/memSet/memCopy/MemoryStack` 全绿。
> 卡点已转移到：libffi → libglfw → GL。详见文末「十、本轮结论」。

## 一、目标

把官方 Minecraft 26.2 client.jar 直接跑在 Android ART 上（不经过桌面 JVM），渲染层最终换 SDL3。
不是"从零重写 MC"（77 万行不现实），而是**移植**：

```
官方 client.jar  ──d8──►  dex（12 个）
                        + liblwjgl.so（自编，Android 版）
                        + libglfw.so（待做：SDL3 实现 24 个 GLFW 函数）
                        + MobileGlues（桌面 GL → GLES）
                        → APK → ART
```

## 二、已验证的成果

| 项目 | 状态 | 证据 |
|---|---|---|
| 26.2 反编译 | ✅ 7055 个 .java | `26.2-src-e/`（26.x 官方已不混淆、无 mappings） |
| 全量 dex 化 | ✅ 12 个 dex，APK 28 MB | `mcprobe/pack.sh` |
| MC 版本探测 | ✅ | `SharedConstants.tryDetectVersion()` |
| **Bootstrap 全量初始化** | ✅ 3~4 秒 | 物品/方块/实体/数据修复器全部注册成功 |
| 关键类加载 | ✅ | Items / Blocks / Entity / LevelChunk / MinecraftServer / Minecraft |
| liblwjgl.so（Android） | ✅ 534 KB，1890 个 JNI 符号 | `lwjglbuild/liblwjgl.so` |
| MemoryUtil 静态初始化 | ✅ | 日志 `step2 class loaded` |
| **MemoryUtil 全量** | ✅ | memAlloc/memPut/memGet/memFree/memSet/memCopy/MemoryStack/UTF8 全部验证通过（40 轮 0 不一致） |

## 三、目录结构与关键文件

```
mc-work/
├── 26.2-src-e/              反编译源码（补丁的唯一来源，51 MB，勿删）
├── 26.2-client.jar          官方 jar（原始字节码 v69）
├── mc65.jar                 class 版本 69→65 的副本，仅供 javac 读取
├── mcpatched.jar            打了补丁的 jar（d8 的输入之一）
├── libs26/                  133 个依赖库（原始）
├── libs-patched/            libs26 + 补丁后的 LWJGL MemoryUtil
├── mcdex2/                  MC 打补丁后的 3 个 dex
├── libsdex/                 依赖库的 8 个 dex
├── lwjgl3-src/              LWJGL 3.4.1 源码（稀疏 clone，生成 C 的来源）
├── lwjglbuild/              ★ liblwjgl.so 构建目录
│   ├── org_lwjgl_system_*.c 从 LWJGL 仓库取出的生成 C
│   ├── android_shim.c       自写：nGetDirectBufferAddress
│   ├── patch/MemoryUtil.java  ★ ART 适配版 MemoryUtil（关键！）
│   └── liblwjgl.so          产物
├── mcprobe/                 ★ 探针 APK 工程
│   ├── src/com/example/mcart/MainActivity.java  （Bootstrap 探针）
│   ├── src/com/example/mcart/MuActivity.java    （LWJGL 内存探针）
│   ├── lib-arm64/liblwjgl.so
│   ├── assets/assets/minecraft/lang/*.json
│   └── pack.sh              aapt2 + d8 + zip + apksigner 一条龙
├── make_patches.py          ★ 从 26.2-src-e 生成 patch/src（7 处 MC 补丁）
├── rebuild_mc.sh            mcpatched.jar → d8 → mcdex2
├── rebuild_libs.sh          libs-patched → d8 → libsdex
├── PATCHES.md               MC 补丁记录
├── PORTING.md               平台层用量统计与路线
└── sdlprobe/, sdljni/, apk/ 早期 SDL3 验证实验（可留可删）
```

## 四、MC 侧补丁（`make_patches.py` 生成，7 处）

1. `Util` — Android 无 jar FileSystemProvider → `orElse(null)`；两处泛型修正
2. `StringRepresentable` — `Enum[]`/`StringRepresentable[]` 擦除不一致导致 VerifyError → 父类构造改 `Object[]`
3. `FileAccessProvider` — `java.lang.ScopedValue` → `ThreadLocal`
4. `ModifyContent` — `ScopedValue.where(..).run(..)` → `ThreadLocal.set/remove`
5. `Language.parseTranslations` — APK 里取不到资源 → 回退 `files/assets`
6. `Language.loadFromJson` — null 流兜底
7. `Language.getVisualOrder` — 反编译接口不匹配 → 重写 sink 转发

## 五、LWJGL 侧（关键工作）

### 5.1 自编 Android 版 liblwjgl.so

```bash
cd mc-work/lwjglbuild
export JAVA_HOME=/data/data/com.termux/files/usr/lib/jvm/java-21-openjdk
NDK=/data/data/com.termux/files/usr/opt/android-sdk/ndk/29.0.14206865
CC=$NDK/toolchains/llvm/prebuilt/linux-aarch64/bin/aarch64-linux-android28-clang
$CC -shared -fPIC -O2 -DLWJGL_LINUX -I. \
  -I$JAVA_HOME/include -I$JAVA_HOME/include/linux \
  -o liblwjgl.so \
  org_lwjgl_system_JNI.c org_lwjgl_system_MemoryAccessJNI.c \
  org_lwjgl_system_libc_LibCStdlib.c org_lwjgl_system_libc_LibCString.c \
  org_lwjgl_system_libc_LibCStdio.c org_lwjgl_system_jni_JNINativeInterface.c \
  org_lwjgl_system_MemoryUtil.c android_shim.c
```
缺少的 C 文件用 `git show 3.4.1:<path>` 从 `lwjgl3-src` 取。

### 5.2 MemoryUtil 的 ART 适配（`lwjglbuild/patch/MemoryUtil.java`）

- `memAddress0(Buffer)` → 新 native `nGetDirectBufferAddress`（JNI `GetDirectBufferAddress`）
- `wrapBufferXxx()` / `memDuplicate()` → 用 `NewDirectByteBuffer(...).asXxxBuffer()`
- clinit 里 mark/position/limit/capacity/address/parent **全部置 -1**（Android hidden-api 禁止反射 `java.nio.Buffer`）
- `write8()` 增加非规范地址拦截（见下）

编译注入：
```bash
javac -cp libs26/org.lwjgl_lwjgl_3.4.1_unsafe.jar:libs26/org.jspecify_jspecify_1.0.0.jar \
      -d lwjglbuild/patch/out lwjglbuild/patch/MemoryUtil.java
(cd lwjglbuild/patch/out && zip -q -r ../../libs-patched/org.lwjgl_lwjgl_3.4.1_unsafe.jar org/lwjgl/system)
./rebuild_libs.sh        # 约 4~5 分钟
```

## 六、当前卡点（下个会话从这里继续）

`memAllocInt` 触发崩溃：

```
write8 target=0xa85e2727f0 offset=0 value=108   ← 108 = 'l'，正在写 "lwjgl" 字符串
#00 art::Unsafe_putByte(...)  fault addr 0x74e727f0 (= 目标地址低 32 位)
```

判断：某处用**非法地址**写字符串（0xa85e2727f0 ≈ 720 GB，不在用户地址空间）。
已做的处理：`write8` 遇到非规范地址**跳过并记日志**（`files/mu.log`）——但实测仍然崩，且跳过分支没有触发，
说明崩溃可能不在 `write8` 里（ART 的 JIT 帧符号化不可靠，`write8+0` 只是近似归属）。

### 补充证据（重要）
- 目标地址**每次运行都不同**：`0xa85e2727f0`（fault 0x5e2727f0）、`0xa874e727f0`（fault 0x74e727f0）
  → 高位 `0xa8` 固定、低位是某个**随进程变化的值**，很像把"JVM 堆地址/数组地址"当成了 native 指针
- 崩溃时正在写 `value=108`（'l'）——正是字符串 `"lwjgl"` 的第一个字符
  → 也就是 `Library.loadNative("lwjgl")` 这条路径：`memASCII("lwjgl")` → 往一个算错的地址写
- 高度怀疑：LWJGL 的 `MemoryStack`（线程本地栈分配器）内部地址计算依赖我们禁用的字段偏移，
  或者 `memASCII` 的临时缓冲来自 `MemoryStack`，其基址拿到的是垃圾

### 下一步建议
1. 读 `MemoryUtil-341.java` 里 `memASCII(CharSequence, boolean)` / `MemoryStack` 实现，
   确认 target 的来路（大概率是 `stack.malloc()` → `MemoryStack` 的 base 指针）
2. 用 root 权限挂 `lldb`/`gdb` 到 app 进程，`break Unsafe_putByte` 抓精确 Java 栈
   （或临时把 `MemoryStack.stackPush()` 打日志）
3. 如果确认是 MemoryStack：把 `MemoryStack` 也做一份 Android 补丁版（用我们自己的 `malloc` 实现栈）

**下一步（按优先级）**
1. 用跳过版验证 `memAllocInt/memPutInt/memGetInt/memFree` 是否通过；
   若通过 → `MemoryUtil` 基本可用，进入下一步；若仍有崩溃，按 `mu.log` 的调用栈定位调用方
2. 补 **libffi（Android 版）** + `org_lwjgl_system_libffi_*.c`（GLFW 回调、GL capabilities 需要）
3. 写 **`libglfw.so`（SDL3 实现 24 个 GLFW 函数）**：
   `glfwCreateWindow/glfwMakeContextCurrent/glfwSwapBuffers/glfwPollEvents/回调注册/显示器信息…`
   （清单见 `PORTING.md`，来自 `Window.java` 的实际调用）
4. GL 用 **MobileGlues**（`/data/user/0/com.termux/files/home/RotatingartLauncher/app/src/main/jniLibs/arm64-v8a/libmobileglues.so`）
   提供桌面 GL；或用 libGL.so 转发到 GLESv2 + 少数打桩
5. 之后才是 `GlDevice` 初始化、资源加载、真正出画面

## 七、环境事实（下次直接用）

- 设备：OnePlus / Android 16 / SDK 36 / arm64-v8a，**有 root（KernelSU，`su -c` 可用）**
- JDK 21：`/data/data/com.termux/files/usr/lib/jvm/java-21-openjdk`
- SDK：`/data/data/com.termux/files/usr/opt/android-sdk`
  - **aapt2 只有 build-tools/35.0.0 是 arm64**（其余是 x86_64 跑不了）
- NDK：`.../ndk/29.0.14206865`（clang 最高 android35）
- cmake：用 Termux 的 `/data/data/com.termux/files/usr/bin/cmake`（SDK 自带是 x86）
- d8/r8：`/data/data/com.termux/files/usr/share/java/d8.jar`（9.2.4，**接受 Java 25 class v69**）
- javac 读不了 v69 → 用 `mc65.jar`（版本号改 65）
- APK 必须 `android:debuggable=true`（hidden-api 宽松化）+ `android:extractNativeLibs=true`
- 建议 `android:vmSafeMode="true"`（减少 JIT 干扰崩溃栈）

### 常用命令

```bash
# 重建 MC 补丁 dex
python3 make_patches.py && javac ... && ./rebuild_mc.sh
# 重建依赖 dex（改了 libs-patched 后）
./rebuild_libs.sh
# 打包 + 装机 + 跑
cd mcprobe && ./pack.sh
su -c "cp mc-on-art.apk /data/local/tmp/mc.apk; pm install -r /data/local/tmp/mc.apk"
su -c "am start -W -n com.example.mcart/.MainActivity"   # Bootstrap 探针
su -c "am start -W -n com.example.mcart/.MuActivity"     # LWJGL 内存探针
su -c "cat /data/data/com.example.mcart/files/mu-result.txt"
# 崩溃栈（root）
su -c "grep -aE '#0[0-9] pc|signal 11' /data/tombstones/tombstone_XX | head"
```

## 八、会话结束时目录（已清理，530 MB）

```
mc-work/
├── 26.2-client.jar / 26.3-client.jar   官方 jar（26.3 备用）
├── 26.2-src-e/          反编译源码（51 MB，补丁源）
├── mc65.jar             版本降级副本（javac 用）
├── mcpatched.jar        补丁后的 jar
├── libs26/ libs-patched/  依赖库（原始 / 补丁后）
├── mcdex2/ libsdex/     现成 dex（3 + 8 个）
├── lwjglbuild/          liblwjgl.so + 生成 C + MemoryUtil 补丁源
├── mcprobe/             探针 APK 工程 + 成品 APK（29 MB）
├── make_patches.py / rebuild_mc.sh / rebuild_libs.sh
└── HANDOVER.md / PATCHES.md / PORTING.md
```
已删除：反编译中间产物（chunks/out-*/oute-*）、旧的 26.2-src、dexfull/mcdex 等重复 dex、
SDL 早期实验目录、Gradle 编译日志等。

## 九、产物

- `/sdcard/mc-on-art.apk` — 当前探针 APK（28 MB）
- `mc-work/HANDOVER.md`（本文）、`PATCHES.md`、`PORTING.md`
- `mc-work/lwjglbuild/liblwjgl.so` — Android 版 LWJGL 核心库


---

# 十、本轮结论（2026-10-29 下半场）

## 10.1 根因：不是一处 bug，是四个

上一轮以为「某处算错地址」，实际是 **ART 与桌面 JVM 的四处语义差异**叠加，
每一处都单独致命。按被发现的顺序：

### (1) `memAddress0` 有 8 个重载，上一轮只补了 1 个

`MemoryUtil` 里 `memAddress0` 不是只有一个方法：

```java
public static long memAddress0(Buffer buffer)       { ... }   // ← 上一轮只改了这个
public static long memAddress0(ByteBuffer buffer)   { return UNSAFE.getLong(buffer, ADDRESS); }
public static long memAddress0(ShortBuffer buffer)  { ... }
public static long memAddress0(CharBuffer buffer)   { ... }
public static long memAddress0(IntBuffer buffer)    { ... }
public static long memAddress0(LongBuffer buffer)   { ... }
public static long memAddress0(FloatBuffer buffer)  { ... }
public static long memAddress0(DoubleBuffer buffer) { ... }
```

而 `memAddress(ByteBuffer)` 调的是**更精确的重载** `memAddress0(ByteBuffer)`，
所以上一轮的 JNI 补丁**根本没被调用**。`ADDRESS = -1`（我们置的）→
`UNSAFE.getLong(buf, -1)` 在 ART 上按「字段偏移」解释 → 读到对象头附近 → 返回一个
**每次运行都相同、但完全是垃圾**的常量（实测 `0xa85e262800`）。
MemoryStack 拿这个当基址，于是 `UTF8("lwjgl")` 写到 `0xa85e2727f0` → SIGSEGV。

> 上一轮的「跳过非法地址」分支没触发，是因为阈值写成 `0x7fff_ffff_ffff`，
> 而 0xa85e2727f0 比它小，拦不住。

**修复**：7 个具体类型重载 + 通用版 `memAddress(Buffer)` 全部改成
`nGetDirectBufferAddress(buffer)`。

### (2) ART 的 `Unsafe.putXxx(Object o, long offset, ...)` 不能用 o == null

桌面 JVM 上 `UNSAFE.putInt(null, ptr, v)` 表示「写绝对地址 ptr」。
ART 上会把 offset 当**字段偏移**，且对 `ptr` 做了 **32 位符号扩展**：

```
UNSAFE.putInt(null, 0xacbc8550, 0x11223344)
→ fault addr 0xffffffffacbc8550      ← 高 32 位被填 1，必崩
```

这是上一轮所有「绝对地址写」在 ART 上必然崩的原因。

**修复**：全部换成 **单参数绝对地址版本**（ART 上完全可用，实测 >2GB 地址也正常）：

| 原写法 | 改成 |
|---|---|
| `UNSAFE.getInt(null, p)` | `UNSAFE.getInt(p)` |
| `UNSAFE.putInt(null, p, v)` | `UNSAFE.putInt(p, v)` |
| …Byte/Short/Long/Float/Double 同理 | 共 36 处 |

`sun.misc.Unsafe` 在 ART 上可用的单参数方法（实测）：
`getByte/getShort/getInt/getLong/getFloat/getDouble(long)`、
`putByte/putShort/putInt/putLong/putFloat/putDouble(long, …)`、
`allocateMemory/freeMemory/setMemory/copyMemory`。
（注意 `jdk.internal.misc.Unsafe` 被 hidden-api 挡住，反射拿不到。）

### (3) 默认分配器会递归初始化（jemalloc 陷阱）

`MemoryUtil$LazyInit.<clinit>` 里 `MemoryManage.getInstance()` 默认会去实例化
**jemalloc**。而 jemalloc 的 `<clinit>` 要 `Library.loadNative` → `LinuxLibrary.<init>`
→ `MemoryStack.stackGet().UTF8(name)` → 又要 `MemoryUtil.nmemAlloc` →
此时 `LazyInit.ALLOCATOR` 还没赋值。Android 上还没有 jemalloc 的 .so。

**修复**：在 `MemoryUtil` 内新增 `AndroidAllocator`（直接调 `LibCStdlib.nmalloc/ncalloc/
nrealloc/nfree/naligned_alloc/naligned_free`），把
`ALLOCATOR_IMPL = MemoryManage.getInstance()` 改成 `new AndroidAllocator()`。
jemalloc 彻底不参与。

### (4) `asXxxBuffer()` 视图的字节序（最隐蔽的一个）

`NewDirectByteBuffer(addr, n)` 造出来的 ByteBuffer 是 **BIG_ENDIAN**，
`.asIntBuffer()` / `.asFloatBuffer()` 等会继承大端。
于是 `IntBuffer.put(0, 7)` 在内存里写成 `00 00 00 07`，
而 `memGetInt(addr)` 按本机小端读得 `0x07000000`。

表现为「地址对、值对不上」，极易误判成地址算错。

**修复**：所有 `wrapBufferShort/Char/Int/Long/Float/Double` 和泛型 `slice/duplicate`
都要先 `.order(NATIVE_ORDER)` 再 `.asXxxBuffer()`。
（`wrapBufferByte` 上一轮已经加了，所以只有 ByteBuffer 是对的，误导了很久。）

### (5) 附带：`MemoryUtilTunables` 依赖 ART 没有的字段

`MemoryUtilTunables.<clinit>` 读 `sun.misc.Unsafe.ARRAY_BYTE_BASE_OFFSET` 等
→ `NoSuchFieldError`，导致 `memSet/memCopy` 整条链不可用。

**修复**：整体替换（`lwjglbuild/patch/MemoryUtilTunables.java`）：
- 地址区间 → `LibCString.nmemset` / `nmemcpy`
- 数组 ↔ native → `NewDirectByteBuffer(...).asXxxBuffer().put(arr, off, len)`

## 10.2 已确认可用的 ART 能力（重要事实）

| 能力 | 结论 |
|---|---|
| `JNI GetDirectBufferAddress` | ✅ 对所有 direct buffer（含 view）都返回正确地址 |
| `JNI NewDirectByteBuffer(addr, cap)` | ✅ 地址语义正确；**GC 不会 free 这块内存**（实测 5 轮 full GC + 再 malloc 64 块，内容不变） |
| `Unsafe.allocateMemory/freeMemory` | ✅ 存在（但不用它当分配器，改用 libc） |
| `Unsafe` 单参数绝对地址 get/put | ✅ 可用，>2GB（0xb400…）地址正常 |
| `Unsafe` 双参数 `(Object,long)` 形式 | ❌ 32 位符号扩展，绝对地址不可用 |
| `ByteBuffer.allocateDirect` | ✅ 可用（MemoryStack 就是靠它） |
| `NewDirectByteBuffer` + `asXxxBuffer` | ✅ 可用，**但必须 `.order(nativeOrder())`** |
| Android malloc 返回地址 | 常在 `0xb400_00xx_xxxx_xxxx`（scudo 区），**必须当 64 位处理** |

## 10.3 本轮改动的文件

```
lwjglbuild/patch/MemoryUtil.java          ★ 四处修复（见上）
lwjglbuild/patch/MemoryUtilTunables.java  ★ 新增，整体替换
libs-patched/org.lwjgl_lwjgl_3.4.1_unsafe.jar  注入上两个 class
libsdex/                                 重新生成（8 个 dex）
mcprobe/src/.../DiagActivity.java         ★ 新增：LWJGL 内存层分步诊断探针
mcprobe/src/.../Diag2Activity.java        ★ 新增：Unsafe 能力探测
mcprobe/src/.../Diag3Activity.java        ★ 新增：buffer/GC 语义验证
mcprobe/build_dex.sh                      ★ 新增：src → dex 的编译脚本
mcprobe/AndroidManifest.xml               注册上述探针 Activity
vendor/lwjgl3/                            ★ 新增：LWJGL 3.4.1 源码浅克隆（取 libffi 用）
```

## 10.4 验证结果（`files/diag.txt`）

```
1  nGetDirectBufferAddress(allocateDirect(64)) = 0xacbce930
1b memAddress                                  = 0xacbce930   ← 一致了
1c memPutInt/memGetInt = 0x11223344
1d memGetLong = 0xaaaabbbbccccdddd
1e Unsafe.putByte/getByte = 0x5a
2d MemoryStack.nmalloc(8,6) 正常，2f 栈内读写正常
3d UTF8("hello") -> hello        3e UTF8("lwjgl") -> lwjgl   ← 原崩溃点
4  nmemAlloc(16) = 0xb40000718ff92980 / nmemFree ok
5c memGetInt=123456              5d IntBuffer.get(0)=123456   ← 字节序一致了
5f memSet(512) 全 0xab           5g memCopy ok
   ALLOCATOR = MemoryUtil$AndroidAllocator
```

`Diag3Activity` 里 40 轮 `memAllocInt` 地址/视图双向一致性检查：**不一致 0 次**。

## 10.5 下个会话从这里继续

**门槛已过，进入原生层三件事**（按依赖顺序）：

1. **libffi**（GLFW 回调 / GL capabilities 需要）
   - LWJGL 只打包了 `ffi.h` + 各架构 `ffitarget.h`（`vendor/lwjgl3/modules/lwjgl/core/src/main/c/libffi/`）
   - 生成的 JNI 绑定在 `.../src/generated/c/org_lwjgl_system_libffi_{LibFFI,FFICIF,FFIClosure}.c`
   - **注意版本**：LWJGL 3.4.1 的头是 **libffi 3.5.0**；Termux 装的是 **3.8.0**
     （`/data/data/com.termux/files/usr/lib/libffi.so`，SONAME=`libffi.so`，只依赖 libc.so）
   - 两条路：① 用 Termux 3.8.0 的头编 LWJGL 的 3 个 .c + 链接 3.8.0 的 .so（自洽，最快）
     ② clone libffi 3.5.0 交叉编静态库（最稳）
2. **libglfw.so**：可选 SDL3（Termux 有 `libSDL3.so` 0.4.18，但依赖 `libandroid-shmem.so`/`libiconv.so`）
   或**直接自己用 JNI + ANativeWindow + EGL** 实现，见下条
3. **GL**：`MobileGlues`（`RotatingartLauncher/app/src/main/jniLibs/arm64-v8a/libmobileglues.so`，
   依赖只有 libandroid/liblog/libm/libdl/libc，**可直接打包**）；
   同目录还有 `libOSMesa.so`（软件 GL，兜底）、`libGLESv2_angle.so`

## 10.6 环境补充事实（本轮新增）

- 文件系统**禁止硬链接**（/data/data 上 `link()` 被 SELinux 拒），所以写文件要走 shell 重定向/heredoc，
  用 python 或 `cat > file` 都可以，不要指望「原子写入」工具。
- `su -c "..."` 可用；tombstone 在 `/data/tombstones/`，logcat 里 `logcat -d -b crash` 有完整 Java 栈
  （ART 崩在 JNI 里时 `#0` 是 `art::Unsafe_putXxx`，往上是解释器帧，能一路看到 Java 调用链）。
- d8：`java -cp /data/data/com.termux/files/usr/share/java/d8.jar com.android.tools.r8.D8 --min-api 28 --lib android.jar --output <dir> <jar...>`
- aapt2/apksigner 用 build-tools/35.0.0（arm64 版）


---

# 十一、SDL3 打通（同日更晚）

## 11.1 结论：SDL3 + GLES3.2 在 ART 上完整可用，已成功渲染并 swap 一帧

```
SDL_Init(VIDEO)      = 1（SDL3 返回 bool，1=成功）
video driver         = android
displays             = 1, display[0] = 1080x2378
SDL_CreateWindow     = 0xb4000070b7c42a00
SDL_GL_CreateContext = 0xb4000070b7d00f00
framebuffer          = 1080x2378
GL_VERSION           = OpenGL ES 3.2 V@0800.74 (Adreno 830)
GL_RENDERER          = Adreno (TM) 830
swap ok              ← glClear + SDL_GL_SwapWindow 真的画上屏了
```

## 11.2 做法（自己编 SDL3，不用 Termux 那套）

```bash
cd vendor/SDL                        # git clone --depth 1 https://github.com/libsdl-org/SDL.git
export ANDROID_NDK_HOME=/data/data/com.termux/files/usr/opt/android-sdk/ndk/29.0.14206865
export PATH=$ANDROID_NDK_HOME:$PATH
./build-scripts/androidbuildlibs.sh APP_PLATFORM=android-28 -j4
# => build/android/lib/arm64-v8a/libSDL3.so  (2.1 MB, SONAME=libSDL3.so)
```
依赖干净：`libdl / libGLESv1_CM / libGLESv2 / libOpenSLES / liblog / libandroid / libc / libm / libstdc++`
（都是系统库，不像 Termux 版还要 `libandroid-shmem` + `libiconv`）

## 11.3 三个必须踩过的坑

### (1) SDLActivity 默认要加载 `libmain.so`
`SDLActivity.getLibraries()` 默认返回 `{"SDL3", "main"}`，加载失败 → `onCreate` 提前 return
→ 弹出 "dlopen failed: library libmain.so not found" → `SDL.setupJNI()` 永远不会执行
→ `SDL_Init` 虽然返回成功但 video driver 为 null、displays=0、CreateWindow 返回 NULL。

**修**：自己 Activity 里 `@Override protected String[] getLibraries() { return new String[]{"SDL3"}; }`

### (2) `main()` 必须阻塞
`SDLMain.run()` 在 `SDLActivity.main()` 返回后会 `finish()` 掉 Activity
（日志：`activity finished by caller: ... org.libsdl.app.SDLMain.run:2477`）→ Surface 销毁 → 后续 SDL_Init 失败。

**修**：`@Override protected void main() { while (running) Thread.sleep(500); }`

### (3) SDL3 全 API 返回 bool（不是 SDL2 的 int 0/-1）
`SDL_Init` / `SDL_GetDisplayBounds` 等都是 **true=成功**。
写成 `if (rc != 0) fail` 会把成功当失败 —— 这是本轮最后一个坑。

## 11.4 现在的目录

```
mcprobe/
├── src/org/libsdl/app/*.java        ← 直接从 SDL 源码复制（12 个文件，无需改）
├── src/com/example/mcart/SdlProbeActivity.java   ← SDL3 验证探针（已通过）
├── lib-arm64/
│   ├── liblwjgl.so    (547 KB，含 JNI 派发 + libc + libffi JNI 入口)
│   ├── libffi.so      (Termux 3.8.0，SONAME=libffi.so，只依赖 libc)
│   ├── libSDL3.so     (自编 3.5.0，官方脚本)
│   └── libglfw.so     (当前是 SDL3 探针，下一步变成真正的 GLFW shim)
glfwshim/
└── sdl_probe.c                      ← 编译成 libglfw.so 的验证程序
vendor/SDL/                          ← SDL3 源码 + 构建产物
vendor/lwjgl3/                       ← LWJGL 3.4.1 源码（取 libffi 绑定用）
build_lwjgl.sh                       ← 重编 liblwjgl.so（已含 libffi）
```

## 11.5 编译命令（SDL3 探针）

```bash
NDK=/data/data/com.termux/files/usr/opt/android-sdk/ndk/29.0.14206865
$NDK/toolchains/llvm/prebuilt/linux-aarch64/bin/aarch64-linux-android28-clang \
  -shared -fPIC -O2 -o glfwshim/libglfw.so glfwshim/sdl_probe.c \
  -I vendor/SDL/include \
  -L vendor/SDL/build/android/lib/arm64-v8a -lSDL3 -lGLESv2 -llog
```

## 11.6 下一步：写真正的 libglfw.so（GLFW → SDL3）

MC 实际用到 **61 个** GLFW 函数（`26.2-src-e` 里 `GLFW.xxx(` 统计）。分三类：

1. **真实转发到 SDL3**（约 30 个）：
   `glfwInit/Terminate/CreateWindow/DestroyWindow/ShowWindow/SetWindowTitle/
   MakeContextCurrent/SwapBuffers/SwapInterval/GetFramebufferSize/PollEvents/GetTime/
   GetMonitors/GetPrimaryMonitor/GetMonitorName/GetMonitorPos/GetVideoMode/
   GetClipboardString/SetClipboardString/WindowShouldClose/...`
2. **状态存储 + 打桩**（其余）：
   `glfwWindowHint/glfwSetWindowSizeLimits/glfwSetWindowIcon/glfwSetCursorPos/...`
3. **回调注册 + 事件分发**（17 个）：
   存函数指针，在 `glfwPollEvents` 里把 `SDL_Event` 翻译成 GLFW 回调。
   **注意**：GLFW 的 key code（USB HID 风格，`GLFW_KEY_A=65`、`GLFW_KEY_ESCAPE=256`）
   与 SDL3 的 `SDL_Keycode`/`SDL_Scancode` 都不一样，需要一张映射表。

GLFW 的 C 符号名就是原始名（`glfwInit` 等），因为 LWJGL 3.4 用
`APIUtil.apiGetFunctionAddress(GLFW.getLibrary(), "glfwInit")` + `JNI.invokeX` 派发，
**不是** JNI native 方法。

另外 GLFW 绑定里还有 Mojang 定制的 IME 扩展（`glfwSetPreeditCallback`、
`glfwSetPreeditCursorRectangle`、`glfwSetIMEStatusCallback`、`glfwResetPreeditText` 等），
SDL3 没有对应功能，先打桩返回 0/NULL。


---

# 十二、当前卡点（GLFW shim 加载）

## 已就绪

| 组件 | 状态 |
|---|---|
| `liblwjgl.so` | ✅ 含 JNI 派发 + libc + **libffi** + **Upcalls**（Callback 机制）+ **DynamicLinkLoader** + SharedLibraryUtil + common_tools |
| `libffi.so` | ✅ Termux 3.8.0，`ffi_prep_cif` 实测 OK |
| `libSDL3.so` | ✅ 自编 3.5.0，init/建窗/GLES3.2 上下文/swap 全部实测通过 |
| `libglfw.so` | ✅ GLFW→SDL3 shim 已写完，**导出 107 个符号，覆盖 MC 需要的全部 59 个** |
| SDL Java 胶水 | ✅ `org/libsdl/app/*.java` 12 个文件可直接用 |

## 卡点：`GLFW.<clinit>` → `Library.loadNative("glfw")` 的 dlopen 失败

崩溃栈（每次一样）：

```
#06 MultiReleaseTextDecoding.decodeUTF8
#10 MemoryUtil.memUTF8Safe
#14 DynamicLinkLoader.dlerror        ← 在 error 分支里
#18 LinuxLibrary.loadLibrary
#22 LinuxLibrary.<init>
#26 APIUtil.apiCreateLibrary
#30 Library.loadNative
#34 Library.loadNativeFromLibraryPath
#38 Library.loadNative
#46 GLFW.<clinit>
```

fault addr 恒为 `0xffffffffb37e9190`（又是 32 位符号扩展的味道）。

**含义**：`dlopen` 本身失败了 → 走 error 分支 → `dlerror()` 返回的指针被当成有效地址 →
`memUTF8Safe` 读它 → SIGSEGV。**崩溃把真正的 dlopen 失败原因盖住了。**

## 下一步（按优先级）

1. **让 `dlerror()` 不再崩**，才能看到 dlopen 的真实原因。
   最省事：写一个 patch 版 `org.lwjgl.system.linux.DynamicLinkLoader`，
   把 `dlerror()` 改成不调 native（直接返回 Java 字符串），或者
   在 `MemoryUtil.memUTF8Safe` 前加地址合法性检查（`address >>> 48 != 0` 就返回 null）。
   编进去和 MemoryUtil 一样注入 `libs-patched/org.lwjgl_lwjgl_3.4.1_unsafe.jar`。

2. 拿到真实原因后大概率是这两类之一：
   - **依赖解析失败**：`libglfw.so` NEEDED `libSDL3.so`，虽然 SDL3 已加载，
     但 Android linker 的 namespace 对「通过 dlopen 绝对路径」加载的库解析依赖有额外限制。
     可尝试给 `libglfw.so` 编成**不 NEEDED SDL3**（用 `dlopen`+`dlsym` 运行时取符号）。
   - **路径问题**：`org.lwjgl.librarypath` 设成 `nativeLibraryDir` 后走
     `dlopen("<dir>/libglfw.so")` 仍失败的话，改用 `System.loadLibrary("glfw")` +
     自己 `apiCreateLibrary` 拿句柄的方式绕开 LWJGL 的加载逻辑。

3. 已排除的干扰项：
   - `MultiReleaseTextDecoding` 的 Java 11 多版本实现已从 jar 里摘掉（base 版仍在用），
     所以崩溃不是这个类本身的实现问题，而是**传进去的地址就是坏的**。
   - `System.loadLibrary("lwjgl")` 已加（让 `JNI_OnLoad` 设置 `jvm`），
     否则 `Upcalls.getEnv` 会拿空 JavaVM 直接崩。

4. 一个可选的绕过办法（如果 dlopen 一时搞不定）：
   **让 Java 侧不要用 `GLFW` 类的 native 绑定**，而是自己 `System.loadLibrary("glfw")`
   拿到句柄后用 `JNI.invokeX` 直接调 shim 里的函数指针 —— 但要重现 MC 的调用路径成本高，
   不推荐。
