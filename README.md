# mc-on-art — 在 Android 上把 Minecraft Java 版跑起来

不是在写模组，是在做**移植**：把官方 Minecraft（26.2 / Java 25 / class v69）搬到
Android ART 上直接跑。核心手段是

* 一个自写的 **GLFW → SDL3 shim**（`glfwshim/glfw_sdl.c`），让 MC 的 LWJGL 调用落到原生 SDL3 窗口；
* 一组 **字节码补丁**（`make_patches.py`），补上 Android 上没有的文件系统 provider、反射差异、ART 下的热点；
* 复用 Termux 的 **d8/apksigner/aapt2** 自己打包 APK，不依赖 Gradle。

## 现在能做什么

| 能力 | 状态 |
| --- | --- |
| 进世界正常玩（GL/ANGLE 后端） | ✅ 100~270 fps（随内部渲染分辨率） |
| 虚拟触屏按键（FCL 布局：按钮/十字键/摇杆/热键栏）+ 可拖动帧率浮层 | ✅ |
| 内部渲染分辨率缩放 20%~100%（buffer 缩小、合成器拉伸） | ✅ |
| 一个 APK 两个桌面图标：**Minecraft** + **MC 设置** | ✅ |
| 自制「光影」后处理（bloom/ACES/调色/暗角/锐化） | ✅（`artshaders/`） |
| Vulkan 后端 / 光追 / Iris 光影包运行时 | 🚧 实验性，见 `docs/` |

## 两个构建产物

| 产物 | 说明 |
| --- | --- |
| `mcprobe/mc-on-art.apk`（~57 MB） | **常规版**：不含资产，运行时读 `/sdcard/.minecraft/assets` |
| `mcprobe/mc-on-art-assets.apk`（~518 MB） | **自用版**：内嵌 462 MB assets（`assets/mcassets.zip`），首次启动自动解压到 `/sdcard/.minecraft/assets`。⚠️ 含 Mojang 资产，**只能自用，不要分发** |

自用版构建：`bash scripts/make_assets_zip.sh && ./build_all.sh`；想回到常规版：`bash scripts/make_assets_zip.sh --clean`。

**权限**：首次启动会动态申请「所有文件访问」（Android 11+ 必须用户手动点一下）。
未授权时不会启动游戏，而是跳系统授权页，授权后返回自动继续。

## 目录速览

```
build_all.sh            一条龙：补丁 -> javac -> mcpatched.jar -> dex -> APK
make_patches.py         ★ 全部字节码补丁的源码（对反编译源码做文本级替换）
glfwshim/glfw_sdl.c     ★ GLFW 3.4 -> SDL3 shim + 虚拟按键(VCTL) + 渲染缩放
mcprobe/                ★ 探针 APK 工程（Activity / 悬浮层 / VctlOverlay / 设置页 / pack.sh）
lwjglbuild/             liblwjgl.so 的 Android 构建（ART 适配版 MemoryUtil 等）
artshaders/             自制后处理链资源（post_effect/*.json + shaders/post/*.fsh）
scripts/                复刻脚本：抓官方输入 / 反编译 / 生成 mc65.jar / 编 shim / 编 libsdex
examples/               vcontrols.txt、artlauncher.json 样例
docs/                   REPRODUCE.md（从零复刻）+ PATCHES.md（补丁记录）+ PORTING.md（移植分析）
```

## 想自己复刻

见 **[docs/REPRODUCE.md](docs/REPRODUCE.md)** —— 从下载官方客户端、反编译、生成 `mc65.jar`，
到编原生库、打包 APK、装机运行的完整步骤。

最短路径（假设已按文档备好 `mc65.jar` / `26.2-src-e/` / `libs26/` / `mcprobe/lib-arm64/` / `libsdex/`）：

```bash
./build_all.sh                 # 出 mcprobe/mc-on-art.apk
adb install -r mcprobe/mc-on-art.apk   # 或用 su -c "pm install -r ..."
```

## 版权与合规

* 本仓库**不包含** Minecraft 客户端 jar、其反编译源码、以及任何 Mojang 资产。
  `scripts/fetch_client_and_libs.py` 只从 Mojang 官方分发地址下载，**使用需自备正版授权**。
* `patch/src/`（反编译 MC 后打补丁的产物）是**构建中间产物**，不入库，由 `make_patches.py` 生成。
* 本仓库自己的代码（shim、补丁脚本、探针工程、artshaders）以 MIT 授权，见 `LICENSE`。
* 依赖项目各自授权：LWJGL (BSD-3)、SDL3 (Zlib)、ANGLE (BSD-3)、Vineflower (Apache-2.0)。
