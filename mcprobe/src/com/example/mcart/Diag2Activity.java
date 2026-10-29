package com.example.mcart;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;

/**
 * 探明 ART 上到底哪种「绝对地址内存访问」可用。
 * 只看现象，不做结论。
 */
public class Diag2Activity extends DiagActivity {

    @Override
    protected String tag() { return "diag2"; }

    private void probeUnsafe(String cls) {
        try {
            Class<?> uc = Class.forName(cls);
            String[] want = {
                "getInt:long", "putInt:long,int",
                "getLong:long", "putLong:long,long",
                "getByte:long", "putByte:long,byte",
                "allocateMemory:long", "freeMemory:long",
                "setMemory:long,long,byte", "copyMemory:long,long,long"
            };
            for (String w : want) {
                String name = w.substring(0, w.indexOf(':'));
                String[] args = w.substring(w.indexOf(':') + 1).split(",");
                Class<?>[] types = new Class<?>[args.length];
                for (int i = 0; i < args.length; i++) {
                    types[i] = argType(args[i]);
                }
                try {
                    uc.getMethod(name, types);
                    mark("  " + cls + "." + w + " : 有");
                } catch (Throwable t) {
                    mark("  " + cls + "." + w + " : 无 (" + t.getClass().getSimpleName() + ")");
                }
            }
        } catch (Throwable t) {
            fail("probeUnsafe " + cls, t);
        }
    }

    private static Class<?> argType(String s) {
        if (s.equals("long")) return long.class;
        if (s.equals("int")) return int.class;
        if (s.equals("byte")) return byte.class;
        if (s.equals("Object")) return Object.class;
        throw new IllegalArgumentException(s);
    }

    @Override
    protected void runDiag() {
        mark("vm=" + System.getProperty("java.vm.name"));

        // ---- A. 有哪些 Unsafe 变体可用 ----
        mark("A ---- Unsafe 方法表 ----");
        probeUnsafe("sun.misc.Unsafe");
        probeUnsafe("jdk.internal.misc.Unsafe");

        Object unsafe = null;
        try {
            Class<?> uc = Class.forName("sun.misc.Unsafe");
            Field uf = uc.getDeclaredField("theUnsafe");
            uf.setAccessible(true);
            unsafe = uf.get(null);
            mark("theUnsafe = " + unsafe);
        } catch (Throwable t) {
            fail("get theUnsafe", t);
            return;
        }

        ByteBuffer bb = ByteBuffer.allocateDirect(64);
        long addr = 0;
        try {
            Class<?> mu = Class.forName("org.lwjgl.system.MemoryUtil");
            addr = (Long) mu.getMethod("memAddress", java.nio.ByteBuffer.class).invoke(null, bb);
            mark("buf addr = 0x" + Long.toHexString(addr));
        } catch (Throwable t) {
            fail("memAddress", t);
            return;
        }

        // ---- B. 单参数 getInt(long) ----
        mark("B ---- Unsafe.getInt(long) 绝对地址 ----");
        try {
            Method putInt = Class.forName("sun.misc.Unsafe").getMethod("putInt", long.class, int.class);
            Method getInt = Class.forName("sun.misc.Unsafe").getMethod("getInt", long.class);
            putInt.invoke(unsafe, addr, 0x11223344);
            mark("B1 putInt(long,int) ok, getInt(long)=0x"
                    + Integer.toHexString((Integer) getInt.invoke(unsafe, addr)));
        } catch (Throwable t) {
            fail("B Unsafe.putInt(long,int)", t);
        }

        // ---- C. putInt(long, long) 的 64 位处理 ----
        mark("C ---- Unsafe.putLong(long,long) ----");
        try {
            Method putLong = Class.forName("sun.misc.Unsafe").getMethod("putLong", long.class, long.class);
            Method getLong = Class.forName("sun.misc.Unsafe").getMethod("getLong", long.class);
            putLong.invoke(unsafe, addr + 8, 0xAAAABBBBCCCCDDDL);
            mark("C1 getLong = 0x" + Long.toHexString((Long) getLong.invoke(unsafe, addr + 8)));
        } catch (Throwable t) {
            fail("C Unsafe.putLong(long,long)", t);
        }

        // ---- D. MemoryAccessJNI（JNI 实现） ----
        mark("D ---- MemoryAccessJNI ----");
        try {
            Class<?> mac = Class.forName("org.lwjgl.system.MemoryAccessJNI");
            Method nputInt = mac.getDeclaredMethod("nputInt", long.class, int.class);
            Method ngetInt = mac.getDeclaredMethod("ngetInt", long.class);
            nputInt.setAccessible(true);
            ngetInt.setAccessible(true);
            nputInt.invoke(null, addr, 0x55667788);
            mark("D1 nputInt/ngetInt = 0x"
                    + Integer.toHexString((Integer) ngetInt.invoke(null, addr)));
            Method nputLong = mac.getDeclaredMethod("nputLong", long.class, long.class);
            Method ngetLong = mac.getDeclaredMethod("ngetLong", long.class);
            nputLong.setAccessible(true);
            ngetLong.setAccessible(true);
            nputLong.invoke(null, addr + 8, 0x1234567890ABCDEFL);
            mark("D2 ngetLong = 0x" + Long.toHexString((Long) ngetLong.invoke(null, addr + 8)));
        } catch (Throwable t) {
            fail("D MemoryAccessJNI", t);
        }

        // ---- E. 原路 memPutInt（已知会崩，放最后确认） ----
        mark("E ---- MemoryUtil.memPutInt（对照） ----");
        try {
            Class<?> mu = Class.forName("org.lwjgl.system.MemoryUtil");
            mu.getMethod("memPutInt", long.class, int.class).invoke(null, addr, 0x99);
            mark("E1 memPutInt ok（意外，说明绝对地址可用）");
        } catch (Throwable t) {
            fail("E memPutInt", t);
        }

        mark("END");
    }
}
