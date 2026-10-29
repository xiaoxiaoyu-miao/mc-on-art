package com.example.mcart;

import android.app.Activity;
import android.os.Bundle;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;

public class MainActivity extends Activity {

    private static final String[] TARGETS = {
        "net.minecraft.core.registries.BuiltInRegistries",
        "net.minecraft.world.item.Items",
        "net.minecraft.world.level.block.Blocks",
        "net.minecraft.world.level.levelgen.synth.PerlinNoise",
        "net.minecraft.world.entity.Entity",
        "net.minecraft.world.level.chunk.LevelChunk",
        "net.minecraft.server.MinecraftServer",
        "net.minecraft.client.Minecraft"
    };

    private StringBuilder log = new StringBuilder();

    private void mark(String s) {
        log.append(s).append('\n');
        try {
            FileOutputStream fos = new FileOutputStream(new File(getFilesDir(), "result.txt"));
            fos.write(log.toString().getBytes("UTF-8"));
            fos.close();
        } catch (Throwable ignored) { }
    }

    private void failure(String label, Throwable t) {
        StringWriter sw = new StringWriter();
        t.printStackTrace(new PrintWriter(sw));
        String[] lines = sw.toString().split("\n");
        mark("FAIL " + label);
        mark("     " + lines[0]);
        int keep = 0;
        for (String ln : lines) {
            if (keep > 0 && ln.startsWith("\t")) { mark("     " + ln.trim()); keep--; continue; }
            if (ln.trim().startsWith("Caused by:")) { mark("     " + ln.trim()); keep = 3; }
        }
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        mark("vm=" + System.getProperty("java.vm.name") + " sdk=" + android.os.Build.VERSION.SDK_INT);
        long t0 = System.currentTimeMillis();
        try {
            Class.forName("net.minecraft.SharedConstants", true, getClassLoader())
                 .getMethod("tryDetectVersion").invoke(null);
            mark("OK   SharedConstants.tryDetectVersion()");
        } catch (Throwable t) {
            failure("SharedConstants.tryDetectVersion()", t);
        }
        long t1 = System.currentTimeMillis();
        try {
            Class.forName("net.minecraft.server.Bootstrap", false, getClassLoader())
                 .getMethod("bootStrap").invoke(null);
            mark("OK   Bootstrap.bootStrap() in " + (System.currentTimeMillis() - t1) + " ms");
        } catch (Throwable t) {
            failure("Bootstrap.bootStrap()", t);
        }
        for (String n : TARGETS) {
            long s = System.currentTimeMillis();
            try {
                Class.forName(n, true, getClassLoader());
                mark("OK   " + n + " (" + (System.currentTimeMillis() - s) + " ms)");
            } catch (Throwable t) {
                failure(n, t);
            }
        }
        mark("DONE in " + (System.currentTimeMillis() - t0) + " ms");
        try {
            mark("lwjgl: step1 class load");
            Class<?> mu = Class.forName("org.lwjgl.system.MemoryUtil");
            mark("lwjgl: step2 loaded");
            long ptr = ((Long) mu.getMethod("memAllocInt", int.class).invoke(null, 4)).longValue();
            mark("lwjgl: step3 memAllocInt ptr=0x" + Long.toHexString(ptr));
            mu.getMethod("memPutInt", long.class, int.class).invoke(null, ptr, 123456);
            mark("lwjgl: step4 memPutInt");
            int back = (Integer) mu.getMethod("memGetInt", long.class).invoke(null, ptr);
            mark("lwjgl: step5 memGetInt=" + back);
            mu.getMethod("memFree", long.class).invoke(null, ptr);
            mark("lwjgl: step6 memFree ok");
            Class<?> ms = Class.forName("org.lwjgl.system.MemoryStack");
            Object stack = ms.getMethod("stackPush").invoke(null);
            mark("lwjgl: step7 stackPush");
            Object buf = ms.getMethod("mallocInt", int.class).invoke(stack, 8);
            mark("lwjgl: step8 mallocInt " + buf.getClass().getSimpleName());
            ms.getMethod("pop").invoke(stack);
            mark("lwjgl: step9 pop ok");
        } catch (Throwable t) {
            failure("LWJGL memory classes", t);
        }
        try {
            Class<?> mu = Class.forName("org.lwjgl.system.MemoryUtil");
            long ptr = ((Long) mu.getMethod("memAllocInt", int.class).invoke(null, 4)).longValue();
            mu.getMethod("memPutInt", long.class, int.class).invoke(null, ptr, 123456);
            int back = (Integer) mu.getMethod("memGetInt", long.class).invoke(null, ptr);
            mark("LWJGL MemoryUtil: alloc=" + ptr + " roundtrip=" + back);
            mu.getMethod("memFree", long.class).invoke(null, ptr);
            Class<?> ms = Class.forName("org.lwjgl.system.MemoryStack");
            Object stack = ms.getMethod("stackPush").invoke(null);
            Object buf = ms.getMethod("mallocInt", int.class).invoke(stack, 8);
            ms.getMethod("pop").invoke(stack);
            mark("LWJGL MemoryStack: ok (buffer=" + buf.getClass().getSimpleName() + ")");
        } catch (Throwable t) {
            failure("LWJGL memory classes", t);
        }
        TextView tv = new TextView(this);
        tv.setText(log.toString());
        tv.setTextSize(9f);
        tv.setPadding(12, 36, 12, 12);
        setContentView(new ScrollView(this) {{ addView(tv); }});
    }
}
