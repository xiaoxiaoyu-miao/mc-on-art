# MC 26.2 → ART 爆改记录

把官方 26.2 client.jar 打包进 APK，直接跑在 ART 上（无桌面 JVM），渲染层后续换 SDL3。

## 已跑通

```
OK   SharedConstants.tryDetectVersion()
OK   Bootstrap.bootStrap() in 3337 ms
OK   net.minecraft.core.registries.BuiltInRegistries
OK   net.minecraft.world.item.Items
OK   net.minecraft.world.level.block.Blocks
OK   net.minecraft.world.entity.Entity
OK   net.minecraft.world.level.chunk.LevelChunk
OK   net.minecraft.server.MinecraftServer
OK   net.minecraft.client.Minecraft
DONE in 3476 ms
```
注册表全量初始化（物品/方块/实体/datafixer）3.3 秒完成，说明 MC 的核心逻辑层在 ART 上没问题。

## 打包

client.jar + 133 个依赖 → d8（`--min-api 28`）得 12 个 dex，APK 28 MB。
`mcprobe/pack.sh`：aapt2 link + glue dex + mcdex2 + libsdex → apksigner 签名。

## 补丁（patch/src，由 make_patches.py 生成）

1. `Util`：Android 无 jar FileSystemProvider → `orElse(null)`；修两处反编译泛型
2. `StringRepresentable`：`Enum[]`/`StringRepresentable[]` 擦除不一致 → ART VerifyError，父类构造改 `Object[]`
3. `FileAccessProvider`：`java.lang.ScopedValue` → `ThreadLocal`
4. `ModifyContent`：`ScopedValue.where(..).run(..)` → `ThreadLocal.set/remove`
5. `Language.parseTranslations`：资源取不到时回退 `files/assets`
6. `Language.loadFromJson`：null 流兜底
7. `Language.getVisualOrder`：反编译接口不匹配 → 重写 sink 转发

## 工具链

- `mc65.jar`：class 版本 69→65 的副本，只给 javac 读原始 jar 用（javac 21 读不了 v69）
- `make_patches.py`：从 26.2-src-e 重新生成补丁源码（可反复迭代）
- `rebuild_mc.sh`：打补丁 jar → d8 → mcdex2
- `mcprobe/pack.sh`：组 APK 并签名

## 下一步（平台层移植）

`Main.main()` 现在会让进程静默退出，卡在 LWJGL/原生层：
- `org.lwjgl.glfw`（24 文件）→ SDL3 窗口/输入
- `com.mojang.blaze3d.opengl.GlDevice` → 新增 `Sdl3Device implements GpuDeviceBackend`（18 方法）
- `org.lwjgl.openal`（8 文件）→ SDL3 音频
- assets（11175 文件）+ assetsDir/dataDir 布局
- 剩余 libcore 差异：`java.lang.management`、`javax.sound`、AWT
