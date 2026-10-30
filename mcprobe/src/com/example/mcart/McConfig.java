package com.example.mcart;

import android.util.Log;

import org.json.JSONObject;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

/**
 * 启动配置：设置界面（SettingsActivity）写、游戏（GlfwProbeActivity）读。
 * 文件：/sdcard/.minecraft/artlauncher.json（「外部设置画质」就靠它）。
 *
 * 说明：它是「启动前」的配置。进游戏后 MC 自己的设置界面仍可改，但**退出游戏时
 * MC 会把当前值写回 options.txt**；下次启动时这里配置的项会再覆盖一次。
 */
public final class McConfig {

    public static final String PATH = "/sdcard/.minecraft/artlauncher.json";

    /** 内部渲染分辨率缩放：0.5~1.0（1.0 = 原生）。走 ANativeWindow_setBuffersGeometry，硬件拉伸。 */
    public float renderScale = 1.0f;
    /** 帧率上限（MC options.framerateLimit） */
    public int fpsLimit = 120;
    /** 低精度着色器：给所有 GLSL 注入 precision mediump（移动端通常能明显提帧，画质略降） */
    public boolean lowPrecisionShaders = false;
    /** 光影开关 + 预设（1 柔和 / 2 电影感 / 3 浓烈） */
    public boolean postEffect = true;
    public int shaderPreset = 2;

    public String graphicsPreset = "custom";   // fast / fancy / fabulous / custom
    public int renderDistance = 8;
    public int simulationDistance = 8;
    public int particles = 0;                  // 0=全部 1=减少 2=最少
    public int mipmapLevels = 4;
    public boolean entityShadows = true;
    public boolean ambientOcclusion = true;
    public int guiScale = 0;                   // 0=自动

    /** 图形后端："angle"（ANGLE→Vulkan，**深度采样可用**，能做深度雾/SSAO/阴影）或 "gles"（MobileGlues 原生） */
    public String graphicsBackend = "angle";

    /** 显示自绘帧率悬浮层（避开屏幕 R 角；MC 自带的 FPS 在左上角会被圆角切掉） */
    public boolean showFpsOverlay = true;

    /** 启动直接进世界：空字符串=最近的世界，"-"=进主菜单（不自动进） */
    public String quickPlay = "";

    public static McConfig load() {
        McConfig c = new McConfig();
        try {
            Path p = Paths.get(PATH);
            if (!Files.isRegularFile(p)) return c;
            String s = new String(Files.readAllBytes(p), StandardCharsets.UTF_8);
            JSONObject o = new JSONObject(s);
            c.renderScale = (float) o.optDouble("renderScale", c.renderScale);
            c.fpsLimit = o.optInt("fpsLimit", c.fpsLimit);
            c.lowPrecisionShaders = o.optBoolean("lowPrecisionShaders", c.lowPrecisionShaders);
            c.postEffect = o.optBoolean("postEffect", c.postEffect);
            c.shaderPreset = o.optInt("shaderPreset", c.shaderPreset);
            c.graphicsPreset = o.optString("graphicsPreset", c.graphicsPreset);
            c.renderDistance = o.optInt("renderDistance", c.renderDistance);
            c.simulationDistance = o.optInt("simulationDistance", c.simulationDistance);
            c.particles = o.optInt("particles", c.particles);
            c.mipmapLevels = o.optInt("mipmapLevels", c.mipmapLevels);
            c.entityShadows = o.optBoolean("entityShadows", c.entityShadows);
            c.ambientOcclusion = o.optBoolean("ambientOcclusion", c.ambientOcclusion);
            c.guiScale = o.optInt("guiScale", c.guiScale);
            c.graphicsBackend = o.optString("graphicsBackend", c.graphicsBackend);
            c.showFpsOverlay = o.optBoolean("showFpsOverlay", c.showFpsOverlay);
            c.quickPlay = o.optString("quickPlay", c.quickPlay);
            Log.i("MCCONFIG", "已读取配置: " + c);
        } catch (Throwable t) {
            Log.e("MCCONFIG", "读取配置失败，用默认值", t);
        }
        return c;
    }

    public void save() {
        try {
            JSONObject o = new JSONObject();
            o.put("renderScale", (double) renderScale);
            o.put("fpsLimit", fpsLimit);
            o.put("lowPrecisionShaders", lowPrecisionShaders);
            o.put("postEffect", postEffect);
            o.put("shaderPreset", shaderPreset);
            o.put("graphicsPreset", graphicsPreset);
            o.put("renderDistance", renderDistance);
            o.put("simulationDistance", simulationDistance);
            o.put("particles", particles);
            o.put("mipmapLevels", mipmapLevels);
            o.put("entityShadows", entityShadows);
            o.put("ambientOcclusion", ambientOcclusion);
            o.put("guiScale", guiScale);
            o.put("graphicsBackend", graphicsBackend);
            o.put("showFpsOverlay", showFpsOverlay);
            o.put("quickPlay", quickPlay);
            Files.write(Paths.get(PATH), o.toString(2).getBytes(StandardCharsets.UTF_8));
            Log.i("MCCONFIG", "已保存配置 -> " + PATH);
        } catch (Throwable t) {
            Log.e("MCCONFIG", "保存配置失败", t);
        }
    }

    /** MobileGlues 的配置目录（shim 里设置的 MG_DIR_PATH 指向这里） */
    public static final String MG_DIR = "/sdcard/.minecraft/mg";

    /**
     * 写 MobileGlues 的 config.json。
     * ⚠️ 实测：MLG 的布尔项必须写成整数 1/0，写 true/false 会被忽略（enableAngle 会回显 false）。
     */
    public void writeMobileGluesConfig() {
        try {
            boolean angle = !"gles".equalsIgnoreCase(graphicsBackend);
            java.nio.file.Path dir = Paths.get(MG_DIR);
            Files.createDirectories(dir);
            JSONObject o = new JSONObject();
            o.put("enableAngle", angle ? 1 : 0);
            o.put("angleDepthClearFixMode", 1);
            o.put("enableExtDirectStateAccess", 1);
            o.put("enableExtTimerQuery", 1);
            o.put("maxGlslCacheSize", 1024);
            Files.write(dir.resolve("config.json"), o.toString(2).getBytes(StandardCharsets.UTF_8));
            Log.i("MCCONFIG", "已写 MobileGlues 配置: enableAngle=" + (angle ? 1 : 0) + " -> " + MG_DIR + "/config.json");
        } catch (Throwable t) {
            Log.e("MCCONFIG", "写 MobileGlues 配置失败", t);
        }
    }

    @Override
    public String toString() {
        return "renderScale=" + renderScale + " fps=" + fpsLimit + " lowp=" + lowPrecisionShaders
                + " post=" + postEffect + "/" + shaderPreset
                + " preset=" + graphicsPreset + " rd=" + renderDistance + " sd=" + simulationDistance
                + " particles=" + particles + " mip=" + mipmapLevels
                + " shadows=" + entityShadows + " ao=" + ambientOcclusion
                + " gui=" + guiScale + " backend=" + graphicsBackend
                + " fpsOverlay=" + showFpsOverlay + " quickplay=" + quickPlay;
    }
}
