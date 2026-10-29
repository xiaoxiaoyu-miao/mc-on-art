package com.example.mcart;

import android.app.Activity;
import android.os.Bundle;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;

public class MuActivity extends Activity {
    private StringBuilder log = new StringBuilder();

    private void mark(String s) {
        log.append(s).append('\n');
        try {
            FileOutputStream fos = new FileOutputStream(new File(getFilesDir(), "mu-result.txt"));
            fos.write(log.toString().getBytes("UTF-8"));
            fos.close();
        } catch (Throwable ignored) { }
    }

    private void failure(String label, Throwable t) {
        StringWriter sw = new StringWriter();
        t.printStackTrace(new PrintWriter(sw));
        mark("FAIL " + label);
        for (String ln : sw.toString().split("\n")) {
            mark("    " + ln);
            if (ln.trim().startsWith("Caused by:")) break;
        }
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        mark("vm=" + System.getProperty("java.vm.name"));
        long t0 = System.currentTimeMillis();
        try {
            mark("step1 forName MemoryUtil");
            Class<?> mu = Class.forName("org.lwjgl.system.MemoryUtil");
            mark("step2 class loaded in " + (System.currentTimeMillis() - t0) + "ms");
            try {
                mu.getDeclaredMethod("nGetDirectBufferAddress", java.nio.Buffer.class);
                mark("step2a patched MemoryUtil present (nGetDirectBufferAddress found)");
            } catch (NoSuchMethodException e) {
                mark("step2a OLD MemoryUtil (no nGetDirectBufferAddress)");
            }
            long ptr = ((Long) mu.getMethod("memAllocInt", int.class).invoke(null, 4)).longValue();
            mark("step3 memAllocInt ptr=0x" + Long.toHexString(ptr));
            mu.getMethod("memPutInt", long.class, int.class).invoke(null, ptr, 123456);
            mark("step4 memPutInt");
            int back = (Integer) mu.getMethod("memGetInt", long.class).invoke(null, ptr);
            mark("step5 memGetInt=" + back);
            mu.getMethod("memFree", long.class).invoke(null, ptr);
            mark("step6 memFree");
        } catch (Throwable t) {
            failure("memory util", t);
        }
        mark("END " + (System.currentTimeMillis() - t0) + "ms");
        TextView tv = new TextView(this);
        tv.setText(log.toString());
        tv.setTextSize(11f);
        setContentView(new ScrollView(this) {{ addView(tv); }});
    }
}
