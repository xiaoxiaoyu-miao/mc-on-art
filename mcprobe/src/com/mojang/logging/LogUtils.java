package com.mojang.logging;

import java.util.function.Supplier;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.slf4j.Marker;
import org.slf4j.MarkerFactory;
import org.slf4j.event.Level;

/**
 * {@code com.mojang:logging 1.7.12} 的兼容替代实现。
 *
 * <p>原版 {@code LogUtils.getLogger()} 用 {@code java.lang.StackWalker} 找调用者类，
 * 而 StackWalker 只在较新的 Android（API 34+）才有。在更老的设备上，任何 "静态字段
 * {@code LOGGER = LogUtils.getLogger()}" 的类都会在初始化时炸：
 * <pre>
 * Caused by: java.lang.ClassNotFoundException: Didn't find class "java.lang.StackWalker$Option"
 * </pre>
 * 于是 {@code net.minecraft.util.Util} 之类的核心类全部初始化失败。</p>
 *
 * <p>本实现改用 {@link Thread#getStackTrace()}（所有 API 版本都有）推断调用者类名，
 * 其余公开 API 语义保持不变。放在探针工程里（dex 顺序最前）以遮蔽库里的原版。</p>
 */
public final class LogUtils {

    public static final String FATAL_MARKER_ID = "FATAL";
    public static final Marker FATAL_MARKER;

    static {
        Marker m = null;
        try {
            m = MarkerFactory.getMarker(FATAL_MARKER_ID);
        } catch (Throwable ignored) {
            // 极端情况下 slf4j 绑定缺失：给个空 marker 也胜过类初始化失败
        }
        FATAL_MARKER = m;
        try {
            android.util.Log.i("GLFWPROBE", "LogUtils 兼容实现已生效（无 StackWalker，API 34 以下也能跑）");
        } catch (Throwable ignored) {
        }
    }

    private LogUtils() {
    }

    public static boolean isLoggerActive() {
        return true;
    }

    /** 原版转成 log4j 的 Level；这里交给 log4j2 配置文件，保留空实现 */
    public static void configureRootLoggingLevel(Level level) {
    }

    public static Object defer(Supplier<Object> supplier) {
        return supplier == null ? null : supplier.get();
    }

    /** 替代 StackWalker 的调用者探测：跳过本类自己的帧，取第一个外部类 */
    public static Logger getLogger() {
        return LoggerFactory.getLogger(callerName());
    }

    private static String callerName() {
        try {
            StackTraceElement[] st = Thread.currentThread().getStackTrace();
            for (int i = 0; i < st.length; i++) {
                String cn = st[i].getClassName();
                if (cn == null) continue;
                if (cn.equals(LogUtils.class.getName())) continue;
                if (cn.equals("java.lang.Thread")) continue;
                return cn;
            }
        } catch (Throwable ignored) {
        }
        return "unknown";
    }
}
