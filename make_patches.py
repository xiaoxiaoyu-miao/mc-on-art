import pathlib, re, shutil, sys

SRC = pathlib.Path("26.2-src-e")
OUT = pathlib.Path("patch/src")
if OUT.exists():
    shutil.rmtree(OUT)

def patch(rel, *pairs):
    cur = (OUT / rel)
    t = cur.read_text() if cur.exists() else (SRC / rel).read_text()
    for old, new in pairs:
        if old not in t:
            print(f"!! pattern missing in {rel}: {old[:60]!r}")
            return False
        t = t.replace(old, new)
    dst = OUT / rel
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(t)
    print(f"patched {rel}")
    return True

ok = True

# 1) Util: Android 没有 jar FileSystemProvider；另外两处反编译泛型
ok &= patch("net/minecraft/util/Util.java",
    ('.orElseThrow(() -> new IllegalStateException("No jar file system provider found"));', '.orElse(null);'),
    ("return key.getName((Comparable)value);", "return key.getName((T)value);"),
    ("for (K key : (Enum[])keyType.getEnumConstants()) {", "for (K key : keyType.getEnumConstants()) {"))

# 2) StringRepresentable: 父类构造改成 Object[]，消除 Enum[] / StringRepresentable[] 擦除不一致
ok &= patch("net/minecraft/util/StringRepresentable.java",
    ("""      public StringRepresentableCodec(final S[] valueArray, final Function<String, @Nullable S> nameResolver, final ToIntFunction<S> idResolver) {
         this.codec = ExtraCodecs.orCompressed(
            Codec.stringResolver(StringRepresentable::getSerializedName, nameResolver),
            ExtraCodecs.idResolverCodec(idResolver, i -> i >= 0 && i < valueArray.length ? valueArray[i] : null, -1)
         );
      }""",
     """      public StringRepresentableCodec(final Object[] rawValues, final Function<String, @Nullable S> nameResolver, final ToIntFunction<S> idResolver) {
         final S[] valueArray = (S[])rawValues;
         this.codec = ExtraCodecs.orCompressed(
            Codec.stringResolver(StringRepresentable::getSerializedName, nameResolver),
            ExtraCodecs.idResolverCodec(idResolver, i -> i >= 0 && i < valueArray.length ? valueArray[i] : null, -1)
         );
      }"""))

# 3) FileAccessProvider: java.lang.ScopedValue -> ThreadLocal (ART/libcore 没有 ScopedValue)
ok &= patch("net/minecraft/util/filefix/access/FileAccessProvider.java",
    ("   private final ScopedValue<Path> baseDirectory = ScopedValue.newInstance();",
     "   private final ThreadLocal<Path> baseDirectory = new ThreadLocal<>();"),
    ("   public ScopedValue<Path> baseDirectory() {",
     "   public ThreadLocal<Path> baseDirectory() {"))

# 4) ModifyContent: ScopedValue.where(..).run(..) -> ThreadLocal set/remove
ok &= patch("net/minecraft/util/filefix/operations/ModifyContent.java",
    ("""         ScopedValue.where(this.fileAccessProvider.baseDirectory(), baseDirectory).run(() -> {
            try {
               this.fixFunction.run(upgradeProgress);
            } catch (IOException e) {
               throw new UncheckedIOException(e);
            }
         });""",
     """         ThreadLocal<Path> tl = this.fileAccessProvider.baseDirectory();
         tl.set(baseDirectory);
         try {
            try {
               this.fixFunction.run(upgradeProgress);
            } catch (IOException e) {
               throw new UncheckedIOException(e);
            }
         } finally {
            tl.remove();
         }"""))


# 5) Language: 类路径资源在 APK 里拿不到，回退到 files/assets
ok &= patch("net/minecraft/locale/Language.java",
    ("""   private static void parseTranslations(final BiConsumer<String, String> output, final String path) {
      try (InputStream stream = Language.class.getResourceAsStream(path)) {
         loadFromJson(stream, output);
      } catch (IOException | JsonParseException e) {""",
     """   private static void parseTranslations(final BiConsumer<String, String> output, final String path) {
      InputStream stream = Language.class.getResourceAsStream(path);
      if (stream == null) {
         try {
            String root = System.getProperty("mcart.assetRoot", "/data/data/com.example.mcart/files/assets");
            java.nio.file.Path p = java.nio.file.Paths.get(root).resolve(path.startsWith("/") ? path.substring(1) : path);
            if (java.nio.file.Files.exists(p)) {
               stream = java.nio.file.Files.newInputStream(p);
            }
         } catch (Throwable ignored) {
         }
      }
      if (stream == null) {
         LOGGER.warn("Missing translation file: {}", path);
         return;
      }
      try (InputStream in = stream) {
         loadFromJson(in, output);
      } catch (IOException | JsonParseException e) {"""))


# 6) Language.getVisualOrder: 反编译后泛型/接口对不上，重写成等价的 sink 转发
ok &= patch("net/minecraft/locale/Language.java",
    ("""         public FormattedCharSequence getVisualOrder(final FormattedText logicalOrderText) {
            return outputx -> logicalOrderText.visit(
                  (style, contents) -> StringDecomposer.iterateFormatted(contents, style, output) ? Optional.empty() : FormattedText.STOP_ITERATION,
                  Style.EMPTY
               )
               .isPresent();
         }""",
     """         public FormattedCharSequence getVisualOrder(final FormattedText logicalOrderText) {
            return sink -> {
               logicalOrderText.visit(
                  (style, contents) -> StringDecomposer.iterateFormatted(contents, style,
                        (index, sty, codepoint) -> sink.accept(index, sty, codepoint)) ? Optional.empty() : FormattedText.STOP_ITERATION,
                  Style.EMPTY
               );
               return true;
            };
         }"""))


# 7) Language.loadFromJson: 兜底，避免任何 null 流（APK 里资源路径都取不到）
ok &= patch("net/minecraft/locale/Language.java",
    ("""   public static void loadFromJson(final InputStream stream, final BiConsumer<String, String> output) {
      JsonObject entries""",
     """   public static void loadFromJson(final InputStream stream, final BiConsumer<String, String> output) {
      if (stream == null) {
         LOGGER.warn("Skipping null translation stream");
         return;
      }
      JsonObject entries"""))


# 8) LinkFSPath: 显式实现 Path 的 default 方法
#    Android 与 JDK 的 Path.resolve(String) 都是 default 方法（已 javap 对比确认），
#    但 LinkFSPath 只实现了 resolve(Path)，d8 之后调用 default 版本会抛：
#      java.lang.AbstractMethodError: abstract method "java.nio.file.Path
#        java.nio.file.Path.resolve(java.lang.String)" on receiver LinkFSPath
#    MC 自身大量使用 resolve(String)，所以在这里补齐。
ok &= patch("net/minecraft/server/packs/linkfs/LinkFSPath.java",
    ("""   public LinkFSPath resolve(final Path other) {
      LinkFSPath otherLink = this.toLinkPath(other);
      return other.isAbsolute() ? otherLink : this.resolve(otherLink.pathToRoot());
   }""",
     """   public LinkFSPath resolve(final Path other) {
      LinkFSPath otherLink = this.toLinkPath(other);
      return other.isAbsolute() ? otherLink : this.resolve(otherLink.pathToRoot());
   }

   // ART 补丁：Path 接口的 default 方法在 d8 之后解析失败，这里显式实现。
   @Override
   public LinkFSPath resolve(final String other) {
      return this.resolve(this.fileSystem.getPath(other));
   }

   @Override
   public LinkFSPath resolveSibling(final Path other) {
      LinkFSPath parent = this.getParent();
      return (parent == null ? this : parent).resolve(other);
   }

   @Override
   public LinkFSPath resolveSibling(final String other) {
      return this.resolveSibling(this.fileSystem.getPath(other));
   }

   @Override
   public boolean startsWith(final String other) {
      return this.startsWith(this.fileSystem.getPath(other));
   }

   @Override
   public boolean endsWith(final String other) {
      return this.endsWith(this.fileSystem.getPath(other));
   }

   // ART 补丁：Path.iterator() 也是 default 方法，d8 后同样解析失败。
   // Guava 的 Joiner.join(path) / 各种 for-each 都会用到。
   @Override
   public java.util.Iterator<Path> iterator() {
      return new java.util.Iterator<Path>() {
         private int index = 0;

         @Override
         public boolean hasNext() {
            return this.index < LinkFSPath.this.getNameCount();
         }

         @Override
         public Path next() {
            if (this.index >= LinkFSPath.this.getNameCount()) {
               throw new java.util.NoSuchElementException();
            }
            return LinkFSPath.this.getName(this.index++);
         }
      };
   }"""))



# 9) DebugEntryMemory: Android 没有 java.lang.management（JMX），改用 Runtime 统计
ok &= patch("net/minecraft/client/gui/components/debug/DebugEntryMemory.java",
    ("import java.lang.management.GarbageCollectorMXBean;\nimport java.lang.management.ManagementFactory;\n", ""),
    ("      private static final List<GarbageCollectorMXBean> GC_MBEANS = ManagementFactory.getGarbageCollectorMXBeans();",
     "      // ART 补丁：Android 无 java.lang.management（JMX），不再统计 GC 次数\n      private static final List<Object> GC_MBEANS = java.util.List.of();"),
    ("""      private static long gcCounts() {
         long total = 0L;

         for (GarbageCollectorMXBean gcBean : GC_MBEANS) {
            total += gcBean.getCollectionCount();
         }

         return total;
      }""",
     """      private static long gcCounts() {
         return 0L;
      }"""))

# 10) DebugEntryDetailedMemory: 同上，用 Runtime 内存数据代替 MemoryMXBean
ok &= patch("net/minecraft/client/gui/components/debug/DebugEntryDetailedMemory.java",
    ("import java.lang.management.ManagementFactory;\nimport java.lang.management.MemoryMXBean;\nimport java.lang.management.MemoryUsage;\n", ""),
    ("   private final MemoryMXBean memoryBean = ManagementFactory.getMemoryMXBean();\n", ""),
    ("""      displayer.addToGroup(
         GROUP, List.of(printMemoryUsage(this.memoryBean.getHeapMemoryUsage(), "heap"), printMemoryUsage(this.memoryBean.getNonHeapMemoryUsage(), "non-heap"))
      );""",
     """      // ART 补丁：用 Runtime 代替 MemoryMXBean
      Runtime rt = Runtime.getRuntime();
      long max = rt.maxMemory(), total = rt.totalMemory(), free = rt.freeMemory();
      displayer.addToGroup(
         GROUP,
         List.of(String.format(Locale.ROOT, "Memory (heap): i=000MiB u=%03dMiB c=%03dMiB m=%03dMiB",
            bytesToMebibytes(total - free), bytesToMebibytes(total), bytesToMebibytes(max)))
      );"""),
    ("""   private static String printMemoryUsage(final MemoryUsage memoryUsage, final String type) {
      return String.format(
         Locale.ROOT,
         "Memory (%s): i=%03dMiB u=%03dMiB c=%03dMiB m=%03dMiB",
         type,
         bytesToMebibytes(memoryUsage.getInit()),
         bytesToMebibytes(memoryUsage.getUsed()),
         bytesToMebibytes(memoryUsage.getCommitted()),
         bytesToMebibytes(memoryUsage.getMax())
      );
   }

""", ""))



# 11) RealmsPersistence: Files.readString/writeString 是 Java 11 API，Android 没有
ok &= patch("com/mojang/realmsclient/util/RealmsPersistence.java",
    ("         String contents = Files.readString(file, StandardCharsets.UTF_8);",
     "         String contents = new String(Files.readAllBytes(file), StandardCharsets.UTF_8);"),
    ("         Files.writeString(file, GSON.toJson(data), StandardCharsets.UTF_8);",
     "         Files.write(file, GSON.toJson(data).getBytes(StandardCharsets.UTF_8));"))

# 12) VanillaPackResourcesBuilder.pushJarResources: Android 没有 jar FileSystemProvider，
#     Class.getResource("/assets/.mcassetsroot") 返回 jar:file:...!/... 但 Paths.get 必失败，
#     ROOT_DIR_BY_TYPE 为空 -> jar 内资源（colormap/shader/models）全读不到。
#     改为从预解压目录读取（/sdcard/.minecraft/assets-jar/assets/minecraft/...）。
ok &= patch("net/minecraft/server/packs/VanillaPackResourcesBuilder.java",
    ("""   public VanillaPackResourcesBuilder pushJarResources() {
      ROOT_DIR_BY_TYPE.forEach((packType, path) -> {
         this.pushRootPath(path.getParent());
         this.pushPathForType(packType, path);
      });
      return this;
   }""",
     """   public VanillaPackResourcesBuilder pushJarResources() {
      // ART 补丁：Android 上没有任何 jar FileSystemProvider，ROOT_DIR_BY_TYPE 恒为空。
      // 改为读取预解压到内部存储的 jar 资源根目录。
      Path artJarRoot = Paths.get("/sdcard/.minecraft/assets-jar");
      if (this.validateDirPath(artJarRoot)) {
         return this.pushUniversalPath(artJarRoot);
      }
      LOGGER.warn("ART: jar asset root {} is missing; vanilla jar resources will be unavailable", artJarRoot);
      ROOT_DIR_BY_TYPE.forEach((packType, path) -> {
         this.pushRootPath(path.getParent());
         this.pushPathForType(packType, path);
      });
      return this;
   }"""))

# 13) JvmProfiler: Runtime.class.getModule() 是 Java 9 API，Android 没有；
#     接口初始化时直接 NoSuchMethodError（Commands -> ReloadableServerResources 触发，
#     即「创建/进入世界」必崩）。直接退化成 NoOpProfiler。
ok &= patch("net/minecraft/util/profiling/jfr/JvmProfiler.java",
    ("import jdk.jfr.FlightRecorder;\n", ""),
    ("""   JvmProfiler INSTANCE = Runtime.class.getModule().getLayer().findModule("jdk.jfr").isPresent() && FlightRecorder.isAvailable()
      ? JfrProfiler.getInstance()
      : new JvmProfiler.NoOpProfiler();""",
     """   // ART 补丁：Android 没有 Class.getModule()/ModuleLayer/jdk.jfr，禁掉 JFR
   JvmProfiler INSTANCE = new JvmProfiler.NoOpProfiler();"""))

# 14) Files.readString / writeString 是 Java 11 API，Android 没有 -> readAllBytes / write
ok &= patch("net/minecraft/client/multiplayer/AccountProfileKeyPairManager.java",
    ("Files.writeString(this.profileKeyPairPath, jsonStr.toString());",
     "Files.write(this.profileKeyPairPath, jsonStr.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));"))

ok &= patch("net/minecraft/client/quickplay/QuickPlayLog.java",
    ("Files.writeString(this.path, GSON.toJson(json));",
     "Files.write(this.path, GSON.toJson(json).getBytes(java.nio.charset.StandardCharsets.UTF_8));"))

ok &= patch("net/minecraft/data/structures/SnbtDatafixer.java",
    ("String snbt = Files.readString(path);",
     "String snbt = new String(Files.readAllBytes(path), java.nio.charset.StandardCharsets.UTF_8);"))

ok &= patch("net/minecraft/util/profiling/jfr/SummaryReporter.java",
    ("Files.writeString(jsonReport, statsResult.asJson(), StandardOpenOption.CREATE);",
     "Files.write(jsonReport, statsResult.asJson().getBytes(java.nio.charset.StandardCharsets.UTF_8), StandardOpenOption.CREATE);"))

ok &= patch("net/minecraft/util/filefix/FileFixerUpper.java",
    ("Files.writeString(filePath, GSON.toJson(json), StandardOpenOption.DSYNC, StandardOpenOption.CREATE, StandardOpenOption.WRITE);",
     "Files.write(filePath, GSON.toJson(json).getBytes(java.nio.charset.StandardCharsets.UTF_8), StandardOpenOption.DSYNC, StandardOpenOption.CREATE, StandardOpenOption.WRITE);"),
    ("JsonObject json = GSON.fromJson(Files.readString(filePath), JsonObject.class);",
     "JsonObject json = GSON.fromJson(new String(Files.readAllBytes(filePath), java.nio.charset.StandardCharsets.UTF_8), JsonObject.class);"),
    # 反编译泛型残留：NbtOps 是 DynamicOps<Tag>，不能当 DynamicOps<CompoundTag> 用
    ("NbtIo.writeCompressed(dynamic.<CompoundTag>convert(NbtOps.INSTANCE).getValue(), levelDatPath);",
     "NbtIo.writeCompressed((CompoundTag)dynamic.convert(NbtOps.INSTANCE).getValue(), levelDatPath);"),
    # 反编译 lambda 捕获变量名残留（ex / exx 实际局部变量叫 e）
    ('LOGGER.warn("Failed to move outdated world folder out of the way; will try to delete instead: ", ex);',
     'LOGGER.warn("Failed to move outdated world folder out of the way; will try to delete instead: ", e);'),
    ('LOGGER.warn("Failed to delete outdated world folder: ", ex);',
     'LOGGER.warn("Failed to delete outdated world folder: ", e);'),
    ('LOGGER.warn("Failed to move in new world folder: ", exx);',
     'LOGGER.warn("Failed to move in new world folder: ", e);'),
    ("throw new FailedCleanupFileFixException(exx, tempWorldTopLevel.getFileName().toString(), fileSystemCapabilities);",
     "throw new FailedCleanupFileFixException(e, tempWorldTopLevel.getFileName().toString(), fileSystemCapabilities);"),
    ("throw new FailedCleanupFileFixException(ex, tempWorldTopLevel.getFileName().toString(), fileSystemCapabilities);",
     "throw new FailedCleanupFileFixException(e, tempWorldTopLevel.getFileName().toString(), fileSystemCapabilities);"))

# 15) Path.resolve(String, String...) 是 JDK 22+ 新增，Android 的 Path 没有。
#     Identifier.resolveAgainst 用它拼「命名空间/路径」，进入/创建世界时必炸
#     （NoSuchMethodError: resolve(Ljava/lang/String;[Ljava/lang/String;)）。
ok &= patch("net/minecraft/resources/Identifier.java",
    ("      Path resultingPath = root.resolve(this.getNamespace(), new String[]{this.getPath()});",
     "      // ART 补丁：Android 的 Path 没有 resolve(String, String...)，拆成两次 resolve\n"
     "      Path resultingPath = root.resolve(this.getNamespace()).resolve(this.getPath());"))

# 16) 同一个 API 在原生库引导里也用到
ok &= patch("com/mojang/blaze3d/platform/NativeLibrariesBootstrap.java",
    ("         Path newLibraryDir = Path.of(libraryPathString).resolve(version, new String[]{arch});",
     "         // ART 补丁：同上，Path 没有 resolve(String, String...)\n"
     "         Path newLibraryDir = Path.of(libraryPathString).resolve(version).resolve(arch);"),
    # 反编译残留：super(...) 前有局部变量赋值，Java 不允许
    ("""      public CapturingPrintStream(final OutputStream out) {
         NativeLibrariesBootstrap.CapturingStream logStream = new NativeLibrariesBootstrap.CapturingStream();
         super(new TeeOutputStream(out, logStream), false, StandardCharsets.UTF_8);
         this.collector = logStream;
      }""",
     """      private static NativeLibrariesBootstrap.CapturingStream pendingCollector;

      private static OutputStream tee(final OutputStream out) {
         NativeLibrariesBootstrap.CapturingStream s = new NativeLibrariesBootstrap.CapturingStream();
         pendingCollector = s;
         return new TeeOutputStream(out, s);
      }

      public CapturingPrintStream(final OutputStream out) {
         super(tee(out), false, StandardCharsets.UTF_8);
         this.collector = pendingCollector;
      }"""))


# 17) GameRenderer：让自定义后处理链（光影-lite）常驻运行，并支持三套预设热切换
#     资源：artshaders/post_effect/{art_soft,art_cine,art_vivid}.json
#           artshaders/shaders/post/{art_bright,art_blur,art_composite}.fsh
#           （用 ./install_artshaders.sh 装到 /sdcard/.minecraft/assets-jar）
#     MC 的 post chain 是「按 id 惰性编译 + 缓存」，所以切预设 = 切 id，不用重启。
#     按键（见补丁 18）：F11+1/2/3 切预设，F11+0 热重载。
#     开关：系统属性 art.posteffect=false（探针读 /sdcard/.minecraft/posteffect.txt）。
#     起始预设：/sdcard/.minecraft/preset.txt 内容 1/2/3。
#     这条链在 `if (shouldRenderLevel)` 里，只在世界加载后执行，主菜单/加载界面不受影响。
ok &= patch("net/minecraft/client/renderer/GameRenderer.java",
    ("""   private @Nullable Identifier postEffectId;
   private boolean effectActive;""",
     """   private @Nullable Identifier postEffectId;
   private boolean effectActive;
   // ── ART 补丁：三套后处理预设（光影-lite）──
   private static final Identifier[] ART_POST_EFFECTS = new Identifier[]{
      Identifier.withDefaultNamespace("art_soft"),
      Identifier.withDefaultNamespace("art_cine"),
      Identifier.withDefaultNamespace("art_vivid")
   };
   private static final String[] ART_PRESET_NAMES = new String[]{"柔和 soft", "电影感 cine", "浓烈 vivid"};
   private static volatile int ART_PRESET = artReadInitialPreset();
   private static final boolean ART_POST_ENABLED = !"false".equalsIgnoreCase(System.getProperty("art.posteffect", "true"));

   private static int artReadInitialPreset() {
      try {
         java.nio.file.Path p = java.nio.file.Paths.get("/sdcard/.minecraft/preset.txt");
         if (java.nio.file.Files.isRegularFile(p)) {
            String s = new String(java.nio.file.Files.readAllBytes(p), "UTF-8").trim();
            if (s.equals("1")) {
               return 0;
            }
            if (s.equals("2")) {
               return 1;
            }
            if (s.equals("3")) {
               return 2;
            }
         }
      } catch (Throwable ignored) {
      }
      return 1;
   }

   public static void artSetPreset(final int index) {
      ART_PRESET = Math.max(0, Math.min(ART_POST_EFFECTS.length - 1, index));
   }

   public static String artPresetName() {
      return ART_PRESET < 0 || ART_PRESET >= ART_PRESET_NAMES.length ? "?" : ART_PRESET_NAMES[ART_PRESET];
   }"""),
    ("""         if (this.postEffectId != null && this.effectActive) {
            PostChain postChain = this.minecraft.getShaderManager().getPostChain(this.postEffectId, LevelTargetBundle.MAIN_TARGETS);
            if (postChain != null) {
               postChain.process(this.mainRenderTarget, this.resourcePool);
            }
         }""",
     """         // ART 补丁：没有原版特效时，常驻运行自定义后处理链（取当前预设的 id）
         Identifier artPost = this.postEffectId != null
            ? this.postEffectId
            : (ART_POST_ENABLED ? ART_POST_EFFECTS[ART_PRESET] : null);
         if (artPost != null && (this.effectActive || this.postEffectId == null)) {
            PostChain postChain = this.minecraft.getShaderManager().getPostChain(artPost, LevelTargetBundle.MAIN_TARGETS);
            if (postChain != null) {
               postChain.process(this.mainRenderTarget, this.resourcePool);
            }
         }"""))

# 18) KeyboardHandler：F11 + 1/2/3 切换后处理预设；F11 + 0 热重载（重新读 json/fsh 并重编译链）
#     挂在 keyPress 的「按下」分支最前面，优先于原版 escape / F3 调试键逻辑。
ok &= patch("net/minecraft/client/KeyboardHandler.java",
    ("""   private void debugFeedback(final String message) {""",
     """   // ART 补丁：F11 组合键 —— 1/2/3 切换后处理预设，0 热重载资源（重新读 json/fsh 并重编译链）
   // 两种按法都认：① 按住 F11 再按数字 ② 先点一下 F11，1.5 秒内按数字
   private static long ART_F11_TIME = 0L;
   private static final long ART_F11_WINDOW_MS = 1500L;

   private boolean handleArtShaderKeys(final KeyEvent event) {
      int k = event.key();
      if (k == InputConstants.KEY_F11) {
         ART_F11_TIME = Util.getMillis();
         return false;   // 不吞掉 F11：原版的全屏切换照旧
      }

      boolean f11 = InputConstants.isKeyDown(this.minecraft.getWindow(), InputConstants.KEY_F11)
         || Util.getMillis() - ART_F11_TIME <= ART_F11_WINDOW_MS;
      if (!f11) {
         return false;
      }

      if (k >= InputConstants.KEY_1 && k <= InputConstants.KEY_3) {
         net.minecraft.client.renderer.GameRenderer.artSetPreset(k - InputConstants.KEY_1);
         this.debugFeedbackComponent(
            Component.literal("ART 光影预设 → " + net.minecraft.client.renderer.GameRenderer.artPresetName())
         );
         return true;
      }

      if (k == InputConstants.KEY_0) {
         this.debugFeedbackComponent(Component.literal("ART 光影：重载资源并重编译后处理链…"));
         this.minecraft.reloadResourcePacks();
         return true;
      }

      return false;
   }

   private void debugFeedback(final String message) {"""),
    ("""               boolean didDebugAction = false;
               if (handlesGlobalInput && event.isEscape()) {""",
     """               boolean didDebugAction = false;
               // ART 补丁：F11 组合键优先于原版 escape / F3 调试键
               if (this.handleArtShaderKeys(event)) {
                  didDebugAction = true;
               } else if (handlesGlobalInput && event.isEscape()) {"""))




sys.exit(0 if ok else 1)
