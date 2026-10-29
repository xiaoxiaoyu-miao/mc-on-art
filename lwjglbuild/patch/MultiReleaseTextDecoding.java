/*
 * Copyright LWJGL. All rights reserved.
 * License terms: https://www.lwjgl.org/license
 */
package org.lwjgl.system;

import java.nio.*;
import java.nio.charset.*;

import static org.lwjgl.system.Checks.*;
import static org.lwjgl.system.MemoryUtil.*;

/**
 * String decoding utilities.
 *
 * <p>On Java 9 different implementations are used that work better with compact strings (JEP 254).</p>
 *
 * <h2>ART patch (this copy)</h2>
 *
 * <p>原始实现里 4 处调用 UNSAFE.getByte(null, source + position++)，即 Unsafe 的
 * 双参数形式 getByte(Object, long)。ART 在 o == null 时把这个 long 当成字段偏移
 * 来处理，并做 32 位符号扩展：</p>
 *
 * <pre>
 *   source + position = 0x00000000b37e9190
 *   -> ART 实际访问     0xffffffffb37e9190   (SEGV_MAPERR)
 * </pre>
 *
 * <p>而 Unsafe.getByte(long)（单参数绝对地址版）在 ART 上是可用的
 * （同一包内的 memLengthNT1/strlen64NT1 一直用单参数版，从未崩过）。
 * 所以这里全部改成单参数版。</p>
 *
 * <p>另外加一处防御：isBogusAddress() 命中的地址（高 32 位全 1，只可能是符号扩展产物）
 * 不再解引用，而是返回可辨识串，避免「崩溃把真正原因盖住」的历史问题。</p>
 */
final class MultiReleaseTextDecoding {

    private MultiReleaseTextDecoding() {
    }

    /** @see MemoryUtil#memUTF8(ByteBuffer, int, int) */
    static String decodeUTF8(long source, int length) {
        if (length <= 0) {
            return "";
        }

        // ART: 32 位符号扩展产物必然是坏地址，报出来而不是崩掉整个进程
        if (isBogusAddress(source)) {
            return "<badptr:0x" + Long.toHexString(source) + ">";
        }

        if (DEBUG) {
            // The implementation below does no codepoint validation.
            return jdkFallback(source, length);
        }

        char[] string = length <= ARRAY_TLC_SIZE ? ARRAY_TLC_CHAR.get() : new char[length];

        int i = 0, position = 0;

        while (position < length) {
            char c;

            int b0 = UNSAFE.getByte(source + position++) & 0xFF;
            if (b0 < 0x80) {
                c = (char)b0;
            } else {
                int b1 = UNSAFE.getByte(source + position++) & 0x3F;
                if ((b0 & 0xE0) == 0xC0) {
                    c = (char)(((b0 & 0x1F) << 6) | b1);
                } else {
                    int b2 = UNSAFE.getByte(source + position++) & 0x3F;
                    if ((b0 & 0xF0) == 0xE0) {
                        c = (char)(((b0 & 0x0F) << 12) | (b1 << 6) | b2);
                    } else {
                        int b3 = UNSAFE.getByte(source + position++) & 0x3F;
                        int cp = ((b0 & 0x07) << 18) | (b1 << 12) | (b2 << 6) | b3;

                        if (i < length) {
                            string[i++] = (char)((cp >>> 10) + 0xD7C0);
                        }
                        c = (char)((cp & 0x3FF) + 0xDC00);
                    }
                }
            }
            if (i < length) {
                string[i++] = c;
            }
        }

        return new String(string, 0, Math.min(i, length));
    }

    private static String jdkFallback(long source, int length) {
        byte[] bytes = length <= ARRAY_TLC_SIZE ? ARRAY_TLC_BYTE.get() : new byte[length];
        memByteBuffer(source, length).get(bytes, 0, length);
        return new String(bytes, 0, length, StandardCharsets.UTF_8);
    }

}
