# MC 26.2 平台层移植分析（ART + SDL3）

## 现状

- MC 的 Java 逻辑层在 ART 上**已经完全跑通**：`Bootstrap.bootStrap()` 3.3 秒，注册表/物品/方块/实体全部初始化成功。
- 卡在 LWJGL 原生层：连 `org.lwjgl.system.MemoryUtil.memAllocInt()` 都抛
  `UnsatisfiedLinkError: Failed to locate library: liblwjgl.so`。

## MC 用到的 LWJGL 面（从 26.2 源码实测）

| 模块 | 用量 | 说明 |
|---|---|---|
| `org.lwjgl.system.MemoryUtil / MemoryStack / PointerBuffer` | 34 / 34 / 18 个文件 | 全部走 liblwjgl 的 native（memAlloc/memFree/memSet…） |
| `org.lwjgl.glfw.GLFW` | 24 个函数 | 建窗、输入、回调、显示器信息 |
| `org.lwjgl.opengl.GL33C` | **95 个函数** | blaze3d/opengl 后端 |
| `org.lwjgl.openal.AL10/ALC10` | ~10 个函数 | 声音 |
| `org.lwjgl.vulkan.*`（VK12/KHR*/VMA/struct） | **120 个类 + 74 个函数** | blaze3d/vulkan 后端 |
| `shaderc` + `spvc` | - | Vulkan 后端运行时编译 GLSL→SPIR-V 并反射 |

## 两条路线的真实成本

### 路线 A：GL 后端（GL33C → GLES 3.2）
需要：
1. 编出 Android 版 `liblwjgl.so`（JNI 派发 + 内存/动态加载）——LWJGL 的 Java 绑定层可以**原样复用**（jar 已经 dex 进去了）
2. 写 `libGL.so` 转发层：GLESv2 里同名函数直接转，少数桌面独有函数打桩（glPolygonMode、glDrawElementsInstancedBaseVertex、glGetTexLevelParameteri 等，约 6~10 个）
3. 写 `libglfw.so`（SDL3 实现 24 个函数 + `SDL_GL_CreateContext` 开 GLES3 上下文）
4. 着色器 `#version 330` → `#version 300 es`（MC 自带 blaze3d/preprocessor，加个 GLES 分支）

### 路线 B：Vulkan 后端（用户偏好）
需要：
1. 同样先有 Android 版 `liblwjgl.so`（Vulkan 绑定也靠它派发到 `libvulkan.so`）
2. `libglfw.so` 换成 SDL3 + `SDL_Vulkan_CreateSurface`（Android 上 surface 来自 ANativeWindow）
3. VMA（`org.lwjgl.util.vma`）→ 需要编 `libvma`/或头文件内联
4. **shaderc 需要自己编**（Android 没有），或者离线预编译 SPIR-V + 改 MC 加载逻辑
5. spvc 反射：RAL 里已有 `libspirv-cross-c-shared.so` 可复用

**结论**：路线 B 多出「120 个 struct 绑定 + 74 函数 + shaderc 编库 + 预编译着色器」；
路线 A 只有「95 个 GL 函数转发 + 10 个打桩 + 着色器版本转换」。
两者都必须先解决 `liblwjgl.so` —— 这是唯一的硬门槛，也是下一步要攻的点。

## 下一步（按顺序）

1. 从 LWJGL 3.4.1 源码编 Android 版 `liblwjgl.so`（core native，C 约 5k 行，改少量平台宏）
2. 写 `libglfw` SDL3 shim（24 函数），跑通 MC 的 Window 创建
3. 写 GL→GLES 转发 + 着色器转换，跑通 GlDevice 初始化（黑屏但有帧）
4. 再决定要不要转 Vulkan 后端（届时 liblwjgl 已就绪，成本主要在 shaderc/SPIR-V）

---

## 进展（本轮）

### 1. Android 版 liblwjgl.so —— 已编出来 ✅

从 LWJGL 3.4.1 的生成 C 源码编出（NDK 29, aarch64, 533 KB, 1888 个 JNI 符号）：

```
org_lwjgl_system_JNI.c                  # 全量 JNI 派发（~1.8k 个 invoke 变体）
org_lwjgl_system_MemoryAccessJNI.c      # Unsafe 内存访问桥
org_lwjgl_system_jni_JNINativeInterface.c
org_lwjgl_system_libc_LibCStdlib.c      # malloc/calloc/realloc/free
org_lwjgl_system_libc_LibCString.c
org_lwjgl_system_libc_LibCStdio.c
```

logcat 确认：`Load .../lib/arm64/liblwjgl.so using class loader ns clns-9 ... ok`

### 2. LWJGL MemoryUtil 适配 ART

原版靠“按值探测”（NewDirectByteBuffer 后扫字段值）找 `java.nio.Buffer` 的 mark/position/limit/capacity/address 偏移。
把它改成按字段名反射 + 编译注入 libs dex。

### 3. 新墙：Android hidden-API 限制

```
hiddenapi: Accessing hidden field Ljava/nio/Buffer;->mark:I ... using reflection: denied
Fatal signal 11 (SIGSEGV)   # 偏移拿不到 → Unsafe 读写野指针
```

`targetSdk=21` 和 `android:debuggable="true"` 都试了，未通过（需要验证 debuggable 是否生效）。

### 下一步（明确）

1. **彻底不用反射**：给 liblwjgl 加 JNI 原生方法
   - `GetDirectBufferAddress` / `GetDirectBufferCapacity`（JNI 自带）→ 取代 ADDRESS/CAPACITY
   - mark/position/limit 用 `Buffer` 公共 API，不走 Unsafe 偏移
2. 补 libffi（Android 版）→ `org_lwjgl_system_libffi_*.c`，GLFW 回调/GL capabilities 需要
3. 之后才是 SDL3 的 glfw shim（24 个函数）+ MobileGlues 提供桌面 GL

---

## 最新一轮（liblwjgl 落地）

### 已做
1. **rebuild liblwjgl.so**：加入 `android_shim.c`，新 native `nGetDirectBufferAddress`（用 JNI 的 `GetDirectBufferAddress`，绕开 hidden-api）
2. **重写 MemoryUtil**（编译注入 libs dex）：
   - `memAddress0(Buffer)` → `nGetDirectBufferAddress(buffer)`
   - `wrapBufferXxx()` → 用 `NewDirectByteBuffer(...).asXxxBuffer()`
   - `memDuplicate()` → 同上重建
   - clinit 里所有字段偏移置 -1，不再反射 `java.nio.Buffer`
3. 结果：`MemoryUtil` **静态初始化通过**（`lwjgl: step2 loaded`），但下一步崩：

```
#06 org.lwjgl.system.MemoryUtil.write8+0
#00 art::Unsafe_putByte(...)  ← fault addr 0x0000000074e727f0
```

### 判断
`write8(target, offset, value)` 是 `memASCII/memUTF8` 的写字节助手；说明有代码在给一个**堆缓冲区/计算错误的地址**写字符串。
很可能来自 `Library.initialize()` / `Configuration`（首次用 native 前会把路径等字符串写进 off-heap 缓冲区）。
下一步要给这些地址为 0 的调用加断言/兜底，或改走 `memUTF8` 的 JNI 版本。

### 下一步（按优先级）
1. 在 `write8` 加地址校验并打日志（哪个调用方传了 0/坏地址）
2. 检查 `memAddress0` 对 `NewDirectByteBuffer` 产物的返回值是否非 0
3. 补 libffi（Android）→ GLFW 回调 / GL capabilities
4. SDL3 glfw shim（24 函数）+ MobileGlues 提供桌面 GL
