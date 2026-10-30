package com.example.mcart;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

/**
 * 「MC 设置」入口（第二个桌面图标）。
 *
 * 这里配的东西**在游戏启动时读取一次**并应用 —— 也就是「外部设置画质」：
 *   - 内部渲染分辨率（ANativeWindow_setBuffersGeometry，硬件拉伸）
 *   - 帧率上限（MC options.framerateLimit）
 *   - 低精度着色器（GLSL 注入 precision mediump）
 *   - 画质预设 / 渲染距离 / 模拟距离 / 粒子 / mipmap / 阴影 / 平滑光照 / GUI 缩放
 *   - 光影开关与三套预设
 *
 * 配置写 /sdcard/.minecraft/artlauncher.json，游戏每次启动读它。
 */
public class SettingsActivity extends Activity {

    private static final int BG = 0xFF14161A, FG = 0xFFE8EAED, SUB = 0xFF9AA0A6, ACC = 0xFF4C9AFF;

    private McConfig cfg;
    private LinearLayout root;

    private TextView tvRenderScale, tvFps, tvRd, tvSd, tvMip, tvGui;
    private SeekBar sbRenderScale, sbFps, sbRd, sbSd, sbMip, sbGui;
    private CheckBox cbLowp, cbPost, cbShadows, cbAo, cbQuickPlay, cbOverlay;
    private RadioGroup rgPreset, rgParticles, rgShader, rgBackend;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        cfg = McConfig.load();

        ScrollView sc = new ScrollView(this);
        sc.setBackgroundColor(BG);
        root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(20), dp(20), dp(32));
        sc.addView(root);
        setContentView(sc);

        title("Minecraft on ART · 设置");
        note("这些设置写 " + McConfig.PATH + "，游戏启动时读取。进游戏改设置也没问题，"
                + "但退出时 MC 会写回 options.txt，下次启动这里会再覆盖一次。");

        // ── 显示 ──
        section("显示");
        tvRenderScale = label("内部渲染分辨率：100%");
        sbRenderScale = seek(50, 100, Math.round(cfg.renderScale * 100), p -> {
            cfg.renderScale = p / 100f;
            tvRenderScale.setText("内部渲染分辨率：" + p + "%"
                    + (p >= 100 ? "（原生）" : "（" + (int) (2378 * p / 100f) + "×" + (int) (1080 * p / 100f) + "，硬件拉伸）"));
        });
        tvFps = label("帧率上限：");
        sbFps = seek(20, 260, cfg.fpsLimit, p -> {
            int v = roundTo(p, 10);
            cfg.fpsLimit = v;
            tvFps.setText("帧率上限：" + (v >= 260 ? "不限（跟随屏幕刷新率）" : v + " fps"));
        });
        tvGui = label("界面缩放：");
        sbGui = seek(0, 4, cfg.guiScale, p -> {
            cfg.guiScale = p;
            tvGui.setText("界面缩放：" + (p == 0 ? "自动" : String.valueOf(p)));
        });

        // ── 图形后端 ──
        section("图形后端");
        rgBackend = new RadioGroup(this);
        for (String[] opt : new String[][]{
                {"angle", "ANGLE（Vulkan）· 深度采样可用，可做深度雾/SSAO/阴影"},
                {"gles",  "原生 GLES（MobileGlues 直接翻译）· 更省电"}}) {
            RadioButton rb = radio(opt[1]);
            rb.setTag(opt[0]);
            rb.setChecked(opt[0].equals(cfg.graphicsBackend));
            rgBackend.addView(rb);
        }
        rgBackend.setOnCheckedChangeListener((g, id) ->
                cfg.graphicsBackend = (String) ((RadioButton) g.findViewById(id)).getTag());
        root.addView(rgBackend);
        note("切换后端会重载所有着色器；ANGLE 下帧率与原生基本持平（实测 RD=12/0.7 分辨率：113~120）。");

        // ── 画质 ──
        section("画质预设（会影响下面几项）");
        rgPreset = new RadioGroup(this);
        for (String[] opt : new String[][]{
                {"fast", "流畅"}, {"fancy", "均衡"}, {"fabulous", "精美"}, {"custom", "自定义"}}) {
            RadioButton rb = radio(opt[1]);
            rb.setTag(opt[0]);
            rb.setChecked(opt[0].equals(cfg.graphicsPreset));
            rgPreset.addView(rb);
        }
        rgPreset.setOnCheckedChangeListener((g, id) -> {
            RadioButton rb = g.findViewById(id);
            cfg.graphicsPreset = (String) rb.getTag();
            applyPresetToSliders(cfg.graphicsPreset);
        });
        root.addView(rgPreset);

        tvRd = label("渲染距离：8 区块");
        sbRd = seek(2, 16, cfg.renderDistance, p -> {
            cfg.renderDistance = p;
            tvRd.setText("渲染距离：" + p + " 区块");
        });
        tvSd = label("模拟距离：8 区块");
        sbSd = seek(5, 32, cfg.simulationDistance, p -> {
            cfg.simulationDistance = p;
            tvSd.setText("模拟距离：" + p + " 区块");
        });

        section("粒子效果");
        rgParticles = new RadioGroup(this);
        for (String[] opt : new String[][]{{"0", "全部"}, {"1", "减少"}, {"2", "最少"}}) {
            RadioButton rb = radio(opt[1]);
            rb.setTag(Integer.parseInt(opt[0]));
            rb.setChecked(Integer.parseInt(opt[0]) == cfg.particles);
            rgParticles.addView(rb);
        }
        rgParticles.setOnCheckedChangeListener((g, id) ->
                cfg.particles = (Integer) ((RadioButton) g.findViewById(id)).getTag());
        root.addView(rgParticles);

        tvMip = label("Mipmap 级别：4");
        sbMip = seek(0, 4, cfg.mipmapLevels, p -> {
            cfg.mipmapLevels = p;
            tvMip.setText("Mipmap 级别：" + p);
        });

        cbShadows = check("实体阴影", cfg.entityShadows, b -> cfg.entityShadows = b);
        cbAo = check("平滑光照（AO）", cfg.ambientOcclusion, b -> cfg.ambientOcclusion = b);

        // ── 光影 ──
        section("光影（后处理）");
        cbPost = check("启用光影后处理链", cfg.postEffect, b -> cfg.postEffect = b);
        rgShader = new RadioGroup(this);
        for (String[] opt : new String[][]{{"1", "1 柔和"}, {"2", "2 电影感"}, {"3", "3 浓烈"}}) {
            RadioButton rb = radio(opt[1]);
            rb.setTag(Integer.parseInt(opt[0]));
            rb.setChecked(Integer.parseInt(opt[0]) == cfg.shaderPreset);
            rgShader.addView(rb);
        }
        rgShader.setOnCheckedChangeListener((g, id) ->
                cfg.shaderPreset = (Integer) ((RadioButton) g.findViewById(id)).getTag());
        root.addView(rgShader);
        note("进游戏后也能热切换：F11+1/2/3 换预设，F11+0 重载着色器文件。");

        // ── 性能 ──
        section("性能");
        cbOverlay = check("显示自绘帧率悬浮层（避开 R 角）", cfg.showFpsOverlay, b -> cfg.showFpsOverlay = b);

        cbLowp = check("低精度着色器（GLSL 注入 precision mediump，通常能提帧，画质略降）",
                cfg.lowPrecisionShaders, b -> cfg.lowPrecisionShaders = b);

        // ── 启动行为 ──
        section("启动");
        cbQuickPlay = check("启动直接进最近的世界（不走主菜单）",
                !"-".equals(cfg.quickPlay), b -> {});
        note("想指定世界：把世界目录名写进 /sdcard/.minecraft/quickplay.txt。");

        // ── 按钮 ──
        LinearLayout btns = new LinearLayout(this);
        btns.setOrientation(LinearLayout.HORIZONTAL);
        btns.setPadding(0, dp(18), 0, 0);
        Button bSave = button("保存");
        bSave.setOnClickListener(v -> { collect(); cfg.save(); cfg.writeMobileGluesConfig(); toast("已保存"); });
        Button bPlay = button("保存并启动游戏");
        bPlay.setOnClickListener(v -> {
            collect();
            cfg.save();
            cfg.writeMobileGluesConfig();
            startActivity(new Intent(this, GlfwProbeActivity.class));
            finish();
        });
        btns.addView(bSave, lp());
        btns.addView(bPlay, lp());
        root.addView(btns);

        // 初始化显示文本
        int rp = Math.round(cfg.renderScale * 100);
        sbRenderScale.setProgress(rp);
        tvRenderScale.setText("内部渲染分辨率：" + rp + "%" + (rp >= 100 ? "（原生）" : ""));
        tvFps.setText("帧率上限：" + (cfg.fpsLimit >= 260 ? "不限（跟随屏幕刷新率）" : cfg.fpsLimit + " fps"));
        tvGui.setText("界面缩放：" + (cfg.guiScale == 0 ? "自动" : String.valueOf(cfg.guiScale)));
        tvRd.setText("渲染距离：" + cfg.renderDistance + " 区块");
        tvSd.setText("模拟距离：" + cfg.simulationDistance + " 区块");
        tvMip.setText("Mipmap 级别：" + cfg.mipmapLevels);

        note("配置：" + McConfig.PATH + "\n当前：" + cfg);
    }

    /** 预设 → 同步到滑条（用户仍可再手改，改成别的值就等于 custom） */
    private void applyPresetToSliders(String preset) {
        switch (preset) {
            case "fast":
                setSeek(sbRd, tvRd, 6, "渲染距离：%d 区块");
                setSeek(sbMip, tvMip, 2, "Mipmap 级别：%d");
                setSeek(sbSd, tvSd, 6, "模拟距离：%d 区块");
                break;
            case "fancy":
                setSeek(sbRd, tvRd, 10, "渲染距离：%d 区块");
                setSeek(sbMip, tvMip, 4, "Mipmap 级别：%d");
                setSeek(sbSd, tvSd, 10, "模拟距离：%d 区块");
                break;
            case "fabulous":
                setSeek(sbRd, tvRd, 12, "渲染距离：%d 区块");
                setSeek(sbMip, tvMip, 4, "Mipmap 级别：%d");
                setSeek(sbSd, tvSd, 12, "模拟距离：%d 区块");
                break;
            default:
                break;
        }
    }

    private interface IntSetter { void accept(int v); }
    private interface BoolSetter { void accept(boolean v); }

    /** 从控件收集当前值（控件回调已经写进 cfg，这里补 quickPlay） */
    private void collect() {
        cfg.quickPlay = cbQuickPlay.isChecked() ? "" : "-";
        if (cfg.renderScale < 0.5f) cfg.renderScale = 0.5f;
        if (cfg.renderScale > 1.0f) cfg.renderScale = 1.0f;
        if (cfg.fpsLimit < 20) cfg.fpsLimit = 20;
        if (cfg.fpsLimit > 260) cfg.fpsLimit = 260;
        if (cfg.shaderPreset < 1) cfg.shaderPreset = 1;
        if (cfg.shaderPreset > 3) cfg.shaderPreset = 3;
        if (!"custom".equals(cfg.graphicsPreset) && !"fast".equals(cfg.graphicsPreset)
                && !"fancy".equals(cfg.graphicsPreset) && !"fabulous".equals(cfg.graphicsPreset)) {
            cfg.graphicsPreset = "custom";
        }
    }

    // ── 小工具：全程序化 UI（不依赖 res/ 布局）──
    private int dp(int v) { return Math.round(getResources().getDisplayMetrics().density * v); }

    private LinearLayout.LayoutParams lp() {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        p.setMargins(dp(4), 0, dp(4), 0);
        return p;
    }

    private void title(String t) {
        TextView tv = new TextView(this);
        tv.setText(t);
        tv.setTextColor(FG);
        tv.setTextSize(22);
        tv.setPadding(0, 0, 0, dp(10));
        root.addView(tv);
    }

    private void section(String t) {
        TextView tv = new TextView(this);
        tv.setText(t);
        tv.setTextColor(ACC);
        tv.setTextSize(16);
        tv.setPadding(0, dp(18), 0, dp(6));
        root.addView(tv);
    }

    private void note(String t) {
        TextView tv = new TextView(this);
        tv.setText(t);
        tv.setTextColor(SUB);
        tv.setTextSize(12);
        tv.setPadding(0, dp(4), 0, dp(6));
        root.addView(tv);
    }

    private TextView label(String t) {
        TextView tv = new TextView(this);
        tv.setText(t);
        tv.setTextColor(FG);
        tv.setTextSize(14);
        tv.setPadding(0, dp(10), 0, 0);
        root.addView(tv);
        return tv;
    }

    private SeekBar seek(int min, int max, int val, IntSetter on) {
        SeekBar sb = new SeekBar(this);
        sb.setMax(max - min);
        sb.setProgress(Math.max(0, Math.min(max - min, val - min)));
        sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int progress, boolean fromUser) {
                on.accept(min + progress);
            }
            @Override public void onStartTrackingTouch(SeekBar s) {}
            @Override public void onStopTrackingTouch(SeekBar s) {}
        });
        root.addView(sb);
        // 触发一次回调，把初值展示出来
        on.accept(val);
        return sb;
    }

    private void setSeek(SeekBar sb, TextView tv, int value, String fmt) {
        sb.setProgress(value - (sb.getMax() == 100 ? 50 : sb.getMax() == 14 ? 2 : 0));
        tv.setText(String.format(fmt, value));
    }

    private CheckBox check(String t, boolean val, BoolSetter on) {
        CheckBox cb = new CheckBox(this);
        cb.setText(t);
        cb.setTextColor(FG);
        cb.setTextSize(14);
        cb.setChecked(val);
        cb.setOnCheckedChangeListener((CompoundButton b, boolean c) -> on.accept(c));
        root.addView(cb);
        return cb;
    }

    private RadioButton radio(String t) {
        RadioButton rb = new RadioButton(this);
        rb.setText(t);
        rb.setTextColor(FG);
        rb.setTextSize(14);
        rb.setId(View.generateViewId());
        return rb;
    }

    private Button button(String t) {
        Button b = new Button(this);
        b.setText(t);
        b.setAllCaps(false);
        b.setTextColor(Color.WHITE);
        b.setBackgroundColor(ACC);
        return b;
    }

    private void toast(String m) {
        android.widget.Toast.makeText(this, m, android.widget.Toast.LENGTH_SHORT).show();
    }

    private static int roundTo(int v, int step) { return Math.round((float) v / step) * step; }
}
