# 光影（Shader）专项交接

> 本文只讲「光影」这条线。整体移植状态见 `HANDOVER-NEXT.md`（1732 行）。
> 目标：在**不装模组**的前提下，给 MC 26.2 on ART 加尽可能接近光影的画面效果，
> 并且把它当作压力测试手段（吃满 GPU）来验证最终性能。

---

## 1. 必须接受的硬事实

**真正的 Minecraft 光影包（BSL / Complementary / Photon / Kappa / Sildur's…）做不到。**

原因不是"难"，而是结构性的：

| 光影包依赖什么 | 我们的处境 |
|---|---|
| Iris（或 OptiFine）作为 host | 没有任何 mod loader；我们是 vanilla `client.jar` → d8 → ART |
| Iris 用 **mixin** 在类加载时重写几十个 MC 渲染类 | ART 没有运行时字节码注入；mixin 只能在 d8 之前离线做 |
| Iris 自带 Sodium 式的区块渲染重写（gbuffers / 顶点格式） | 等于重写渲染器 |
| 阴影贴图（额外一遍地形渲染到 depth-only target） | 需要改 `LevelRenderer` 的整条渲染流程 |
| 延迟光照 / PBR / 水面反射 / 体积光 | 依赖上述 gbuffers |

**乐观估计**：把 Iris 的核心搬到 26.2 + 离线 mixin 应用 + ART 适配，是**几周~几个月的工程量**，
而且 26.2 是全新版本，Iris 上游能不能适配都未知。**不适合作为"最后一步"。**

还有一个现实障碍：Iris 依赖 Fabric API 与 Sodium 的接口，Fabric 本身也是 mod loader，
要先实现一个 ARM/ART 上的 loader —— 这是另一个大项目。

---

## 2. 可行路径：MC 26.2 数据驱动的 post_effect

好消息：**26.2 把后处理做成了数据驱动的 GLSL 管线**，而且我们能改源码 + 能改资源。

### 2.1 系统构成（源码位置都在 `26.2-src-e/`）

| 文件 | 作用 |
|---|---|
| `net/minecraft/client/renderer/PostChainConfig.java` | JSON codec（targets / passes / inputs / uniforms）|
| `net/minecraft/client/renderer/PostChain.java` | 编译并依次执行 pass |
| `net/minecraft/client/renderer/ShaderManager.java` | 从 `assets/minecraft/post_effect/*.json` 加载，`getPostChain(id, allowedTargets)` |
| `net/minecraft/client/renderer/GameRenderer.java:427` | **唯一的触发点**（见下）|
| `net/minecraft/client/renderer/LevelTargetBundle.java` | `MAIN_TARGETS = Set.of("minecraft:main")` |

### 2.2 触发点原文（`GameRenderer.java` ~427-435）

```java
this.minecraft.levelRenderer.doEntityOutline();
if (this.postEffectId != null && this.effectActive) {
   PostChain postChain = this.minecraft.getShaderManager().getPostChain(this.postEffectId, LevelTargetBundle.MAIN_TARGETS);
   if (postChain != null) {
      postChain.process(this.mainRenderTarget, this.resourcePool);
   }
}
```

`postEffectId` 由 `setPostEffect(Identifier)` 设置，vanilla 只用于三件事：
creeper（苦力怕视野）、spider（蜘蛛视野）、invert（`/client` 或按键），
值形如 `Identifier.withDefaultNamespace("creeper")` → 文件 `assets/minecraft/post_effect/creeper.json`。
**注意：id 不带 `post_effect/` 前缀**（`FileToIdConverter.json("post_effect")` 负责路径映射）。

### 2.3 JSON 格式（用 `invert.json` 逆向出来的）

```json
{
  "targets": { "swap": {} },                      // 内部 target；InternalTarget(width,height,persistent,clearColor)
  "passes": [
    {
      "vertex_shader": "minecraft:core/screenquad",
      "fragment_shader": "minecraft:post/invert",
      "inputs": [
        { "sampler_name": "In",            // GLSL 里对应 `uniform sampler2D InSampler;`（名字 + "Sampler"）
          "target": "minecraft:main",      // 只能是 MAIN_TARGETS 里的，或 JSON 里定义的内部 target
          "use_depth_buffer": false,       // ← 支持采样深度！
          "bilinear": false }
      ],
      "output": "swap",                        // 输出 target
      "uniforms": {
        "InvertConfig": [                      // GLSL 里 `layout(std140) uniform InvertConfig { ... };`
          { "name": "InverseAmount", "type": "float", "value": 0.8 }
        ]
      }
    }
  ]
}
```

**GLSL 约定**（`invert.fsh` 实证）：
- `#version 330`
- 采样器：`uniform sampler2D InSampler;`
- 顶点输入：`in vec2 texCoord;`（由 `screenquad` vsh 提供）
- MC 自动注入 `layout(std140) uniform SamplerInfo { vec2 OutSize; vec2 InSize; };`
- 输出：`out vec4 fragColor;`
- uniform 类型只支持：`int / float / vec2 / vec3 / vec4 / ivec3`
  （`UniformValue.Type`）。**std140 对齐**：block 里把 vec2/vec3/vec4 放在 float 前面

**限制**：
- `getPostChain(id, MAIN_TARGETS)` 只允许引用 `minecraft:main`；其他 target 必须在 JSON 的 `targets` 里自己声明
- `InternalTarget` 支持 `width`/`height`（固定像素，不是比例），做半分辨率 bloom 时要么写死要么全分辨率
- 采样与写入同一个 target 是非法的 → 合成 pass 必须写到一个中间 target，再 blit 回 `main`

---

## 3. 已经写好的东西（**尚未接线，目前是死资源**）

全部在 `/sdcard/.minecraft/assets-jar/assets/minecraft/`（这是 patch 12 让 vanilla pack 读取的路径）：

```
shaders/post/art_bright.fsh      亮部提取（软膝阈值）
shaders/post/art_blur.fsh        可分离高斯模糊（Direction 切换 H/V）
shaders/post/art_composite.fsh   bloom 合成 + ACES tone mapping + 饱和度/对比度 + 暗角 + CAS 锐化
post_effect/enhance.json         5 个 pass：
                                  1) main      -> bloom_a  (art_bright)
                                  2) bloom_a   -> bloom_b  (art_blur, Direction=(1,0))
                                  3) bloom_b   -> bloom_a  (art_blur, Direction=(0,1))
                                  4) main+bloom_a -> swap  (art_composite)
                                  5) swap      -> main     (blit)
```

**为什么现在没效果**：没有任何代码去 `getPostChain("minecraft:enhance")`，所以这四份文件只是躺在资源里。
（这也意味着它们现在是**完全安全的**，不会影响现有运行。）

---

## 4. 下一步要做的事（新会话从这里开始）

### 4.1 打补丁让链条常驻（必须，改动很小）

`make_patches.py` 里加一个 patch，改 `net/minecraft/client/renderer/GameRenderer.java`：

1) 在字段 `private @Nullable Identifier postEffectId;` 之后加：

```java
   // ART 补丁：自定义后处理链（光影-lite），资源见 assets/minecraft/post_effect/enhance.json
   private static final Identifier ART_POST_EFFECT = Identifier.withDefaultNamespace("enhance");
```

2) 把 2.2 那段触发代码改成（没有原版效果时跑我们的链，有原版效果时保持原版优先级）：

```java
         // ART 补丁：postEffectId 为空时改为常驻运行自定义后处理
         Identifier artPost = this.postEffectId != null ? this.postEffectId : ART_POST_EFFECT;
         if (artPost != null && (this.effectActive || this.postEffectId == null)) {
            PostChain postChain = this.minecraft.getShaderManager().getPostChain(artPost, LevelTargetBundle.MAIN_TARGETS);
            if (postChain != null) {
               postChain.process(this.mainRenderTarget, this.resourcePool);
            }
         }
```

> 注意 `effectActive` 的语义：原版用它临时关掉特效（比如按 F4？/`/client` 相关）。
> 用 `|| this.postEffectId == null` 让我们的链在没有原版特效时始终生效。

### 4.2 构建流程（和主项目一致）

```bash
cd /data/user/0/com.termux/files/home/mc-work
python3 make_patches.py
export JAVA_HOME=/data/data/com.termux/files/usr/lib/jvm/java-21-openjdk
export PATH=$JAVA_HOME/bin:$PATH
AJ=/data/data/com.termux/files/usr/opt/android-sdk/platforms/android-36/android.jar
CP="mc65.jar:$AJ:$(ls libs26/*.jar | tr '\n' ':')"
rm -rf patch/out && mkdir -p patch/out
javac -nowarn -cp "$CP" -d patch/out $(find patch/src -name '*.java')
cd patch/out && zip -q -r ../../mcpatched.jar com net && cd ../..
./rebuild_mc.sh          # d8，3~4 分钟
mcprobe/pack.sh
su -c "pm install -r -d $PWD/mcprobe/mc-on-art.apk"
su -c "appops set com.example.mcart MANAGE_EXTERNAL_STORAGE allow"
su -c "am start -n com.example.mcart/.GlfwProbeActivity"
```

### 4.3 怎么判断 shader 有没有编译成功

`ShaderManager` 编译失败会**打 ERROR**（现在 log4j 已开到 DEBUG，能看到）：

```bash
P=$(su -c "pidof com.example.mcart")
timeout 25 su -c "logcat -d -v brief --pid=$P" | grep -iE "shader|post|CompilationException|Couldn't compile"
```

FPS/内存用探针里的 PERF 采样线程（每 10 秒一条）：
```bash
... | grep "PERF"
# I/PERF: fps=99  heap=191/512MB  rd=8  sd=8  gfx=custom  mip=4  particles=ALL
```

### 4.4 深度采样（第二个里程碑，未验证）

`TargetInput.useDepthBuffer=true` 存在，但**还没验证过**：
- GLSL 里该用 `sampler2D` 还是 `sampler2DShadow`、深度是 [0,1] 还是反向、是否透视除法后 —— 全部未知
- 验证方法：写一个只输出 `texture(DepthSampler, texCoord).rrr` 的 pass，看画面是不是深度图
- 通了之后可以做：depth-based 雾、景深、描边、简易 SSAO（无法线，效果打折）、水下/岩浆折射扭曲

### 4.5 如果非要做真阴影

那就不再是"给 MC 加后处理"，而是"重写渲染器"。最小可行的实验路径（按难度递增）：

1. **假阴影**：在 post 阶段用深度 + 太阳方向做屏幕空间光线步进阴影（screen-space shadow），
   不需要额外几何 pass，效果有限但确实是"有阴影" —— 纯 GLSL，1~2 个 pass，可以试
2. **真阴影贴图**：需要 hook `LevelRenderer.renderLevel`，把地形再画一遍到 depth target，
   然后在主 pass 里采样 —— 需要改渲染流程、顶点格式、着色器，工作量是上一条的 10 倍以上
3. **完整 Iris**：不建议

---

## 5. 当前基线数据（供对比）

设备：OnePlus PLQ110 / Adreno 830 / 14.75GB RAM，MobileGlues 2.0.0（GL 4.0，GLES 3.2 后端）

| 配置 | 渲染距离 | Java 堆 | FPS |
|---|---|---|---|
| `fabulous` 预设 | 12（预设想设 32，被钳到 12）| **511/512MB 撞顶** | 159 → 43（GC 抖动）|
| `custom`（fabulous 全特性）+ RD=8 | 8 | 163~269MB | 96~99 |

**重要**：`largeHeap` 应用的堆上限 = `dalvik.vm.heapsize` = **512MB**（不可改，见下节），
所以 RD 就是内存的第一杠杆。加后处理会增加 GPU 负载但**几乎不增加 Java 堆**，
所以它是安全的压力来源。

## 6. ⚠️ 绝对不要再做的事

**不要把 `dalvik.vm.heapsize` 调到 3GB。** 试过了，直接导致开机卡第二屏的 bootloop，
只能进安全模式禁用模块才恢复。原因：该属性影响 zygote 和**所有**进程（含 system_server），
开机早期内存紧张时系统起不来。模块 `/data/adb/modules/mc_heap_3g` 已彻底删除，
不要再重建。512MB 是这台设备给 largeHeap 应用的硬上限。

## 7. 现有关键设施（省得重新摸）

- `mcprobe/log4j2.xml` —— 让 MC 日志到 DEBUG（**不加它只能看到 ERROR**，排查等于盲猜）
- `mcprobe` 里的 PERF 采样线程 —— 每 10 秒打 `fps=/heap=/rd=/gfx=` 到 logcat
- 常驻 FPS 显示：`/sdcard/.minecraft/debug-profile.json` = `{"custom":{"minecraft:fps":"alwaysOn"}}`
- `al_test.c` / `al_ogg.c` —— OpenAL 独立播放测试（排查音频链路用）
- 改 `options.txt` 必须**先 force-stop 应用**，否则 MC 退出时会覆盖回去
- `graphicsPreset` 用 `custom` 才能手动控 RD；`fancy`→16、`fabulous`→32（再被钳成 12）都是强制值
