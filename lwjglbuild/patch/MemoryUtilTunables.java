/*
 * Android/ART 替换实现（REA/MC-on-ART 移植项目自建）。
 *
 * 原版 MemoryUtilTunables 在 clinit 里读 sun.misc.Unsafe.ARRAY_BYTE_BASE_OFFSET
 * 等数组基址字段，而 ART 的 sun.misc.Unsafe 根本没有这些字段（NoSuchFieldError）；
 * 同时 ART 的 Unsafe 也不支持 (Object, long) 形式的数组基址寻址。
 *
 * 这里改成：
 *   - 地址区间拷贝/填充 → 走 liblwjgl.so 内 libc 的 JNI（LibCString.nmemset / nmemcpy）
 *   - 数组 <-> native    → 走 java.nio 的 view buffer（ART 完整支持，语义明确）
 */
package org.lwjgl.system;

import java.nio.ByteBuffer;
import java.nio.DoubleBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.nio.LongBuffer;
import java.nio.ShortBuffer;

import static org.lwjgl.system.jni.JNINativeInterface.NewDirectByteBuffer;

final class MemoryUtilTunables {

    private MemoryUtilTunables() {
    }

    // --- [ 地址区间 ] ---

    static void memset(long ptr, int value, long bytes) {
        org.lwjgl.system.libc.LibCString.nmemset(ptr, value, bytes);
    }

    /** 注意语义：第一个参数是 src，第二个是 dst（与 libc memcpy 相反）。 */
    static void memcpy(long src, long dst, long bytes) {
        org.lwjgl.system.libc.LibCString.nmemcpy(dst, src, bytes);
    }

    // --- [ 数组 -> native ] ---

    static void memcpy(byte[] src, long dst, int offset, int size) {
        NewDirectByteBuffer(dst, size).put(src, offset, size);
    }

    static void memcpy(short[] src, long dst, int offset, int size) {
        NewDirectByteBuffer(dst, (long)size << 1).asShortBuffer().put(src, offset, size);
    }

    static void memcpy(int[] src, long dst, int offset, int size) {
        NewDirectByteBuffer(dst, (long)size << 2).asIntBuffer().put(src, offset, size);
    }

    static void memcpy(long[] src, long dst, int offset, int size) {
        NewDirectByteBuffer(dst, (long)size << 3).asLongBuffer().put(src, offset, size);
    }

    static void memcpy(float[] src, long dst, int offset, int size) {
        NewDirectByteBuffer(dst, (long)size << 2).asFloatBuffer().put(src, offset, size);
    }

    static void memcpy(double[] src, long dst, int offset, int size) {
        NewDirectByteBuffer(dst, (long)size << 3).asDoubleBuffer().put(src, offset, size);
    }

    // --- [ native -> 数组 ] ---

    static void memcpy(long src, byte[] dst, int offset, int size) {
        NewDirectByteBuffer(src, size).get(dst, offset, size);
    }

    static void memcpy(long src, short[] dst, int offset, int size) {
        NewDirectByteBuffer(src, (long)size << 1).asShortBuffer().get(dst, offset, size);
    }

    static void memcpy(long src, int[] dst, int offset, int size) {
        NewDirectByteBuffer(src, (long)size << 2).asIntBuffer().get(dst, offset, size);
    }

    static void memcpy(long src, long[] dst, int offset, int size) {
        NewDirectByteBuffer(src, (long)size << 3).asLongBuffer().get(dst, offset, size);
    }

    static void memcpy(long src, float[] dst, int offset, int size) {
        NewDirectByteBuffer(src, (long)size << 2).asFloatBuffer().get(dst, offset, size);
    }

    static void memcpy(long src, double[] dst, int offset, int size) {
        NewDirectByteBuffer(src, (long)size << 3).asDoubleBuffer().get(dst, offset, size);
    }
}
