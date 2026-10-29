package com.example.mcart;

import android.app.Activity;
import android.os.Bundle;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;

/**
 * 逐步诊断 LWJGL MemoryUtil / MemoryStack 在 ART 上的行为。
 * 每步执行前写标记并 flush，崩溃后可看出最后走到哪一步。
 * 顺序刻意安排：真正会触发 allocator 初始化的步骤放在最后。
 */
public class DiagActivity extends Activity {

    protected final StringBuilder log = new StringBuilder();

    protected String tag() { return "diag"; }

    protected void mark(String s) {
        log.append(s).append('\n');
        try {
            FileOutputStream fos = new FileOutputStream(new File(getFilesDir(), tag() + ".txt"));
            fos.write(log.toString().getBytes("UTF-8"));
            fos.flush();
            fos.close();
        } catch (Throwable ignored) { }
        android.util.Log.i("DIAG", s);
    }

    protected void fail(String label, Throwable t) {
        StringWriter sw = new StringWriter();
        t.printStackTrace(new PrintWriter(sw));
        mark("FAIL " + label + " -> " + t);
        for (String ln : sw.toString().split("\n")) {
            mark("    " + ln);
        }
    }

    protected void dumpField(String className, String fieldName) {
        try {
            Class<?> c = Class.forName(className);
            Field f = c.getDeclaredField(fieldName);
            f.setAccessible(true);
            Object v = f.get(null);
            mark("    " + className + "." + fieldName + " = " + v
                    + (v == null ? "" : "  (" + v.getClass().getName() + ")"));
        } catch (Throwable t) {
            mark("    " + className + "." + fieldName + " -> " + t);
        }
    }

    protected void runDiag() {
        mark("vm=" + System.getProperty("java.vm.name")
                + " sdk=" + android.os.Build.VERSION.SDK_INT);

        // --- 0. Configuration 默认值 ---
        try {
            Class<?> cfg = Class.forName("org.lwjgl.system.Configuration");
            String[] names = {"MEMORY_ALLOCATOR", "DEBUG_STACK", "DEBUG_MEMORY_ALLOCATOR", "STACK_SIZE"};
            for (String n : names) {
                Object o = cfg.getField(n).get(null);
                mark("cfg " + n + "=" + o.getClass().getMethod("get").invoke(o));
            }
        } catch (Throwable t) {
            fail("Configuration", t);
        }

        // --- 1. GetDirectBufferAddress + 实际读写 ---
        try {
            ByteBuffer bb = ByteBuffer.allocateDirect(64);
            Class<?> mu = Class.forName("org.lwjgl.system.MemoryUtil");
            Method ndba = mu.getDeclaredMethod("nGetDirectBufferAddress", java.nio.Buffer.class);
            ndba.setAccessible(true);
            long raw = (Long) ndba.invoke(null, bb);
            mark("1 nGetDirectBufferAddress(allocateDirect(64)) = 0x" + Long.toHexString(raw));
            long addr = (Long) mu.getMethod("memAddress", java.nio.ByteBuffer.class).invoke(null, bb);
            mark("1b memAddress = 0x" + Long.toHexString(addr));
            mu.getMethod("memPutInt", long.class, int.class).invoke(null, addr, 0x11223344);
            Object got = mu.getMethod("memGetInt", long.class).invoke(null, addr);
            mark("1c memPutInt/memGetInt = 0x" + Integer.toHexString((Integer) got));
            mu.getMethod("memPutLong", long.class, long.class).invoke(null, addr + 8, 0xAAAABBBBCCCCDDDDL);
            mark("1d memGetLong = 0x" + Long.toHexString(
                    (Long) mu.getMethod("memGetLong", long.class).invoke(null, addr + 8)));
            Class<?> uc = Class.forName("sun.misc.Unsafe");
            Field uf = uc.getDeclaredField("theUnsafe");
            uf.setAccessible(true);
            Object unsafe = uf.get(null);
            Method putByte = uc.getMethod("putByte", long.class, byte.class);
            Method getByte = uc.getMethod("getByte", long.class);
            putByte.invoke(unsafe, addr + 16, (byte) 0x5A);
            mark("1e Unsafe.putByte/getByte = 0x"
                    + Integer.toHexString((Byte) getByte.invoke(unsafe, addr + 16)));
        } catch (Throwable t) {
            fail("step1", t);
        }

        // --- 2. 手工造 MemoryStack（不碰 TLS / allocator） ---
        Class<?> ms = null;
        try {
            ms = Class.forName("org.lwjgl.system.MemoryStack");
            Class<?> mu2 = Class.forName("org.lwjgl.system.MemoryUtil");
            ByteBuffer container = ByteBuffer.allocateDirect(4096);
            long base = (Long) mu2.getMethod("memAddress", java.nio.ByteBuffer.class).invoke(null, container);
            Object stack = ms.getMethod("create", java.nio.ByteBuffer.class).invoke(null, container);
            mark("2 MemoryStack.create(bb) base=0x" + Long.toHexString(base));
            mark("2a getAddress=0x" + Long.toHexString((Long) ms.getMethod("getAddress").invoke(stack)));
            mark("2b getSize=" + ms.getMethod("getSize").invoke(stack));
            mark("2c getPointer=" + ms.getMethod("getPointer").invoke(stack));
            long sp = (Long) ms.getMethod("nmalloc", int.class, int.class).invoke(stack, 8, 6);
            mark("2d nmalloc(8,6) = 0x" + Long.toHexString(sp));
            mark("2e getPointer after=" + ms.getMethod("getPointer").invoke(stack));
            mu2.getMethod("memPutInt", long.class, int.class).invoke(null, sp, 0x55667788);
            mark("2f memGetInt(stack ptr) = 0x"
                    + Integer.toHexString((Integer) mu2.getMethod("memGetInt", long.class).invoke(null, sp)));
        } catch (Throwable t) {
            fail("step2", t);
        }

        // --- 3. TLS 里那个 stack（LWJGL 真正用的），复现 UTF8 路径 ---
        try {
            Class<?> mu3 = Class.forName("org.lwjgl.system.MemoryUtil");
            Object st = ms.getMethod("stackGet").invoke(null);
            mark("3 stackGet()=" + st);
            mark("3a getAddress=0x" + Long.toHexString((Long) ms.getMethod("getAddress").invoke(st)));
            mark("3b getSize=" + ms.getMethod("getSize").invoke(st));
            mark("3c getPointer=" + ms.getMethod("getPointer").invoke(st));
            Object bb1 = ms.getMethod("UTF8", CharSequence.class, boolean.class).invoke(st, "hello", true);
            long a1 = (Long) mu3.getMethod("memAddress", java.nio.ByteBuffer.class).invoke(null, bb1);
            mark("3d UTF8(\"hello\") addr=0x" + Long.toHexString(a1)
                    + " -> " + mu3.getMethod("memUTF8", long.class).invoke(null, a1));
            Object bb2 = ms.getMethod("UTF8", CharSequence.class, boolean.class).invoke(st, "lwjgl", true);
            long a2 = (Long) mu3.getMethod("memAddress", java.nio.ByteBuffer.class).invoke(null, bb2);
            mark("3e UTF8(\"lwjgl\") addr=0x" + Long.toHexString(a2)
                    + " -> " + mu3.getMethod("memUTF8", long.class).invoke(null, a2));
            mark("3f getPointer final=" + ms.getMethod("getPointer").invoke(st));
        } catch (Throwable t) {
            fail("step3", t);
        }

        // --- 4. nmemAlloc（会触发 LazyInit / allocator） ---
        try {
            Class<?> mu = Class.forName("org.lwjgl.system.MemoryUtil");
            long p = (Long) mu.getMethod("nmemAlloc", long.class).invoke(null, 16L);
            mark("4 nmemAlloc(16) = 0x" + Long.toHexString(p));
            mu.getMethod("nmemFree", long.class).invoke(null, p);
            mark("4a nmemFree ok");
        } catch (Throwable t) {
            fail("step4 nmemAlloc", t);
        }

        // --- 5. 完整 buffer 用例（memAllocInt 返回的是 IntBuffer，不是地址） ---
        try {
            Class<?> mu = Class.forName("org.lwjgl.system.MemoryUtil");
            Object buf = mu.getMethod("memAllocInt", int.class).invoke(null, 4);
            mark("5 memAllocInt -> " + buf);
            long ptr = (Long) mu.getMethod("memAddress", java.nio.Buffer.class).invoke(null, buf);
            mark("5a memAddress(IntBuffer)=0x" + Long.toHexString(ptr));
            mu.getMethod("memPutInt", long.class, int.class).invoke(null, ptr, 123456);
            mark("5b memPutInt ok");
            mark("5c memGetInt=" + mu.getMethod("memGetInt", long.class).invoke(null, ptr));
            mark("5d IntBuffer.get(0)=" + ((java.nio.IntBuffer) buf).get(0));
            mu.getMethod("memFree", java.nio.Buffer.class).invoke(null, buf);
            mark("5e memFree(Buffer) ok");
        } catch (Throwable t) {
            fail("step5 memAllocInt", t);
        }

        // --- 5f. memSet / memCopy 快捷路径 ---
        try {
            Class<?> mu = Class.forName("org.lwjgl.system.MemoryUtil");
            Object b = mu.getMethod("memAlloc", int.class).invoke(null, 512);
            long p = (Long) mu.getMethod("memAddress", java.nio.Buffer.class).invoke(null, b);
            mu.getMethod("memSet", long.class, int.class, long.class).invoke(null, p, 0xAB, 512L);
            mark("5f memSet(512) byte0=0x"
                    + Integer.toHexString((Byte) mu.getMethod("memGetByte", long.class).invoke(null, p) & 0xff)
                    + " byte511=0x" + Integer.toHexString(
                    (Byte) mu.getMethod("memGetByte", long.class).invoke(null, p + 511) & 0xff));
            Object b2 = mu.getMethod("memAlloc", int.class).invoke(null, 512);
            long p2 = (Long) mu.getMethod("memAddress", java.nio.Buffer.class).invoke(null, b2);
            mu.getMethod("memCopy", long.class, long.class, long.class).invoke(null, p, p2, 512L);
            mark("5g memCopy -> byte511=0x" + Integer.toHexString(
                    (Byte) mu.getMethod("memGetByte", long.class).invoke(null, p2 + 511) & 0xff));
            mu.getMethod("memFree", java.nio.Buffer.class).invoke(null, b);
            mu.getMethod("memFree", java.nio.Buffer.class).invoke(null, b2);
            mark("5h free ok");
        } catch (Throwable t) {
            fail("step5f memSet/memCopy", t);
        }

        // --- 6. allocator 到底是谁（最后做） ---
        mark("6 ---- LazyInit ----");
        dumpField("org.lwjgl.system.MemoryUtil$LazyInit", "ALLOCATOR");
        dumpField("org.lwjgl.system.MemoryUtil$LazyInit", "ALLOCATOR_IMPL");

        mark("END");
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        long t0 = System.currentTimeMillis();
        try {
            runDiag();
        } catch (Throwable t) {
            fail("runDiag", t);
        }
        mark("TOTAL " + (System.currentTimeMillis() - t0) + "ms");
        TextView tv = new TextView(this);
        tv.setText(log.toString());
        tv.setTextSize(10f);
        setContentView(new ScrollView(this) {{ addView(tv); }});
    }
}
