package com.example.mcart;

import java.lang.reflect.Method;
import java.nio.Buffer;
import java.nio.ByteBuffer;
import java.nio.IntBuffer;

/** 关键验证：NewDirectByteBuffer 包的 malloc 内存，会不会被 GC 回收掉。 */
public class Diag3Activity extends DiagActivity {

    @Override
    protected String tag() { return "diag3"; }

    private Class<?> mu;
    private Method memAddress, memGetInt, memGetByte, memSet;

    private static String hex(int v) { return "0x" + Integer.toHexString(v & 0xff); }

    @Override
    protected void runDiag() {
        mark("vm=" + System.getProperty("java.vm.name"));
        try {
            mu = Class.forName("org.lwjgl.system.MemoryUtil");
            memAddress = mu.getMethod("memAddress", Buffer.class);
            memGetInt = mu.getMethod("memGetInt", long.class);
            memGetByte = mu.getMethod("memGetByte", long.class);
            memSet = mu.getMethod("memSet", long.class, int.class, long.class);
            Method nmemAlloc = mu.getMethod("nmemAlloc", long.class);
            Method nmemFree = mu.getMethod("nmemFree", long.class);
            Class<?> jni = Class.forName("org.lwjgl.system.jni.JNINativeInterface");
            Method ndbb = jni.getMethod("NewDirectByteBuffer", long.class, long.class);

            // ---- 1. malloc 一块，写特征值 ----
            long p = (Long) nmemAlloc.invoke(null, 256L);
            memSet.invoke(null, p, 0x5A, 256L);
            mark("1 nmemAlloc(256)=0x" + Long.toHexString(p) + " 写 0x5A -> byte0="
                    + hex((Byte) memGetByte.invoke(null, p)));

            // ---- 2. 用 NewDirectByteBuffer 包它，然后丢掉引用 ----
            ByteBuffer b = (ByteBuffer) ndbb.invoke(null, p, 256L);
            mark("2 包装后 b.get(0)=" + hex(b.get(0)) + " b.addr=0x"
                    + Long.toHexString((Long) memAddress.invoke(null, (Buffer) b)));
            b = null;

            // ---- 3. 强制 GC ----
            for (int i = 0; i < 5; i++) {
                System.gc();
                System.runFinalization();
            }
            Thread.sleep(200);
            System.gc();
            mark("3 GC 之后 byte0=" + hex((Byte) memGetByte.invoke(null, p))
                    + "（若仍为 0x5a 说明没被 cleaner 释放）");

            // ---- 4. 再 malloc 一批小块，看会不会踩到 p ----
            long[] ps = new long[64];
            for (int i = 0; i < 64; i++) {
                ps[i] = (Long) nmemAlloc.invoke(null, 256L);
            }
            mark("4 再 malloc 64 块，p 处 byte0=" + hex((Byte) memGetByte.invoke(null, p)));
            for (long q : ps) {
                nmemFree.invoke(null, q);
            }

            // ---- 5. 释放 ----
            nmemFree.invoke(null, p);
            mark("5 nmemFree(p) ok");

            // ---- 6. 循环稳定性：地址读写 vs 视图读写 ----
            Method memAllocInt = mu.getMethod("memAllocInt", int.class);
            Method memFreeBuf = mu.getMethod("memFree", Buffer.class);
            Method memPutInt = mu.getMethod("memPutInt", long.class, int.class);
            int bad = 0;
            long firstAddr = 0;
            for (int i = 0; i < 40; i++) {
                IntBuffer ib = (IntBuffer) memAllocInt.invoke(null, 4);
                long a = (Long) memAddress.invoke(null, (Buffer) ib);
                if (i == 0) firstAddr = a;
                ib.put(0, i * 1000 + 7);
                int viaView = ib.get(0);
                int viaAddr = (Integer) memGetInt.invoke(null, a);
                if (viaView != viaAddr) {
                    bad++;
                    if (bad <= 3) {
                        mark("!! 不一致 i=" + i + " addr=0x" + Long.toHexString(a)
                                + " 视图=" + viaView + " 地址=" + viaAddr);
                    }
                }
                // 反向
                memPutInt.invoke(null, a, i * 1000 + 9);
                if (ib.get(0) != i * 1000 + 9) {
                    bad++;
                    if (bad <= 3) mark("!! 反向不一致 i=" + i + " 视图=" + ib.get(0));
                }
                memFreeBuf.invoke(null, (Buffer) ib);
            }
            mark("6 40 轮 memAllocInt 一致性检查：不一致次数=" + bad
                    + "（首次地址 0x" + Long.toHexString(firstAddr) + "）");

        } catch (Throwable t) {
            fail("Diag3", t);
        }
        mark("END");
    }
}
