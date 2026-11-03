# 从零复刻 mc-on-art

目标：在一块 arm64 Android 设备（Termux 环境）上，从官方 Minecraft 26.2 客户端出发，
构建出可以安装运行的 `mc-on-art.apk`。

> 只在 **Android arm64 + Termux** 上验证过。桌面 Linux 理论上可行，但脚本里的
> `ANDROID_JAR` / `D8_JAR` / NDK 路径默认按 Termux 写，需要自己改环境变量。

---

## 0. 环境与工具

| 需要 | 说明 | Termux |
| --- | --- | --- |
| JDK 21 | `javac` 21 才能编补丁；`d8` 也要 Java | `pkg install openjdk-21` |
| Android SDK platform | `android.jar`（默认 android-36） | `pkg install android-sdk` + `sdkmanager "platforms;android-36"` |
| Android NDK r29 | 编 `libglfw.so` / `liblwjgl.so` | `sdkmanager "ndk;29.0.14206865"` |
| d8 / r8 | Termux 自带 jar | `pkg install d8` 或自备 R8 jar |
| aapt2 / apksigner | 打包签名 | `pkg install aapt2 apksigner` |
| python3 / curl | 脚本 | `pkg install python curl` |
| 磁盘 | 官方输入 + 中间产物 600 MB~1 GB | — |

脚本都从**仓库根目录**解析路径（`cd "$(dirname "$0")"`），不依赖本机绝对路径。
可用环境变量覆盖：`JAVA_HOME`、`ANDROID_JAR`、`D8_JAR`、`ANDROID_NDK`、`SDL3_INCLUDE`、`SDL3_LIB_DIR`。

---

## 1. 官方输入（不入库，需自己准备）

```bash
# ① 客户端 jar（按 26.2.json 的 URL + sha1 校验；需自备正版授权）
python3 scripts/fetch_client_and_libs.py --client

# ② 130 个依赖库 -> libs26/（地址来自仓库内 libs_list.txt）
python3 scripts/fetch_client_and_libs.py --libs

# ③ 反编译 -> 26.2-src-e/（make_patches.py 的唯一输入；首次会下 Vineflower）
bash scripts/decompile.sh

# ④ class v69 -> v65 副本（javac 21 读不了 v69）-> mc65.jar
python3 scripts/make_mc65.py
```

---

## 2. 原生库（`mcprobe/lib-arm64/`）

| 库 | 来源 | 脚本 |
| --- | --- | --- |
| `libSDL3.so` | SDL3 官方源码交叉编译（arm64-v8a，需 Vulkan/GL） | 见下方 |
| `liblwjgl.so` | `lwjglbuild/` 生成 C + ART 适配层 | `./build_lwjgl.sh` |
| `libglfw.so` | `glfwshim/glfw_sdl.c`（本项目核心） | `bash scripts/build_shim.sh` |
| `libffi.so` | Termux `pkg install libffi` 的 arm64 版 | 手工复制 |
| `libc++_shared.so` | NDK `sources/cxx-stl/llvm-libc++/libs/arm64-v8a/` | 手工复制 |
| `libEGL_angle.so` / `libGLESv2_angle.so` | ANGLE 交叉编译（可选） | 可选 |

SDL3：

```bash
git clone --depth 1 --branch release-3.x https://github.com/libsdl-org/SDL vendor/SDL
cmake -S vendor/SDL -B vendor/SDL/build/android \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
  -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF
cmake --build vendor/SDL/build/android -j8
cp vendor/SDL/build/android/libSDL3.so mcprobe/lib-arm64/
```

然后编 shim：

```bash
SDL3_INCLUDE=vendor/SDL/include SDL3_LIB_DIR=mcprobe/lib-arm64 bash scripts/build_shim.sh
```

---

## 3. 依赖库 dex（`libsdex/`）

```bash
bash scripts/build_libsdex.sh
```

复制 `libs26/` → `libs-patched/`，把 **ART 适配版 `MemoryUtil`**
（`lwjglbuild/patch/MemoryUtil.java`）编进 `org.lwjgl_lwjgl_3.4.1_unsafe.jar`，再 d8 成 `libsdex/`。
这一步是 MC 能在 ART 上跑起来的关键之一，别跳过。

---

## 4. 出包

```bash
./build_all.sh
# 产物：mcprobe/mc-on-art.apk（约 57~59 MB）
```

依次执行：`make_patches.py` → javac 补丁 → 重建 `mcpatched.jar` → `rebuild_mc.sh`(d8)
→ `mcprobe/build_dex.sh` → `mcprobe/pack.sh`(aapt2 + apksigner，含 `res/` 图标)。

---

## 4.1 自用版：把 assets 打进 APK（不发布）

不想单独放 `/sdcard/.minecraft/assets` 时，可以把资产内嵌进 APK，首次启动自动解压：

```bash
bash scripts/make_assets_zip.sh          # /sdcard/.minecraft/assets -> mcprobe/assets/mcassets.zip（~462 MB）
./build_all.sh                            # 产出 mcprobe/mc-on-art-assets.apk（~518 MB）

bash scripts/make_assets_zip.sh --clean   # 删掉内嵌 zip，回到常规版
```

行为：APK 内路径 `assets/mcassets.zip`；`GlfwProbeActivity` 首次启动时解压到
`/sdcard/.minecraft/assets` 并写 `.bundled-ok` 标记，之后启动直接跳过。
解压发生在 MC bootstrap 之前的后台线程（约 30~60 秒，日志见 `内置 assets：已解 N 个文件`）。

> ⚠️ 带 assets 的 APK 含 Mojang 资产，**只能自用，不可分发**。

## 5. 装机与运行

```bash
adb shell pm install -r -d mcprobe/mc-on-art.apk
adb shell appops set com.example.mcart MANAGE_EXTERNAL_STORAGE allow

adb shell am start -W -n com.example.mcart/.GlfwProbeActivity   # 游戏入口
adb shell am start -W -n com.example.mcart/.SettingsActivity    # 设置入口（第二个桌面图标）
```

运行期依赖的外部数据（不随 APK 分发）：

* `/sdcard/.minecraft/assets`（objects+indexes，参考 `copy_assets.py`）
* `/sdcard/.minecraft/vcontrols.txt`（虚拟按键布局，样例 `examples/vcontrols.txt`）
* `/sdcard/.minecraft/artlauncher.json`（「MC 设置」写入，样例 `examples/artlauncher.json`）

---

**权限（必读）**：targetSdk 36 下 `MANAGE_EXTERNAL_STORAGE` 不能靠 manifest 自动获得。
`GlfwProbeActivity` 启动时先**试写** `/sdcard/.minecraft`（`.perm-probe`），写不了就跳
`ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION` 授权页，用户授权后 `onResume` 自动继续启动游戏。
（某些 ROM 的 `isExternalStorageManager()` 会误报 true，所以以试写为准。）

## 6. 踩坑记录（省你几天）

1. **logcat 会被限流**：ColorOS/OPLUS 上日志会丢。排查窗口/渲染问题优先读
   `/sdcard/.minecraft/glfwshim.log`（shim 关键路径已落盘），或 `logcat --uid=<app uid>`。
2. **`SDL_SetWindowSize` 在 Android 上改不了 buffer**：buffer 尺寸恒等于 SurfaceView 的
   surface 尺寸。内部渲染分辨率缩放必须走 `SurfaceHolder.setFixedSize()`，并把
   SurfaceView 的 LayoutParams 强制成 `MATCH_PARENT`（SDL 默认用 WRAP_CONTENT，
   否则画面会缩到角落）。
3. **SDL 建 EGL surface 时会 `setBuffersGeometry(0,0)` 重置 geometry**，别在它之前设尺寸。
4. **触摸归一化**：`SDLSurface.getNormalizedX()` 默认按 surface buffer 尺寸归一化，而
   `MotionEvent` 坐标在视图空间；两者不一致时触控/虚拟按键会整体偏到左上角。
   应优先用 `getWidth()/getHeight()`。
5. **`sWin.width/height` 会被 `WINDOW_RESIZED` 改回窗口尺寸**：布局、命中、光标、
   framebuffer 查询都要现查真实 buffer 尺寸（见 shim 里的 `shim_px_size()`）。
6. d8 版本要 ≥ 9.x（吃得下 class v69）；javac 用 21 读 `mc65.jar`。

---

## 7. 脚本一览

| 脚本 | 作用 |
| --- | --- |
| `scripts/fetch_client_and_libs.py` | 下官方客户端 jar / 130 个依赖库（sha1 校验） |
| `scripts/decompile.sh` | Vineflower 反编译 → `26.2-src-e/` |
| `scripts/make_mc65.py` | class v69 → v65 副本 `mc65.jar` |
| `scripts/build_shim.sh` | 编译 `libglfw.so`（GLFW→SDL3 shim） |
| `scripts/build_libsdex.sh` | `libs26` + ART 版 MemoryUtil → `libsdex/` |
| `build_all.sh` | 补丁 → dex → APK 一条龙 |
| `build_lwjgl.sh` / `build_stb.sh` / `build_r8.sh` | 其它原生库 / R8 优化 dex |
| `rebuild_mc.sh` | `mcpatched.jar` → `mcdex2/` |
