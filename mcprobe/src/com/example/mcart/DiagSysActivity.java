package com.example.mcart;

/**
 * 和 DiagActivity 一样，但先把 LWJGL 的分配器强制成 "system"（走我们自己的
 * LibCStdlib JNI），绕开 jemalloc 的循环初始化。
 */
public class DiagSysActivity extends DiagActivity {

    @Override
    protected String tag() { return "diagsys"; }

    @Override
    protected void runDiag() {
        System.setProperty("org.lwjgl.system.allocator", "system");
        mark("preset org.lwjgl.system.allocator=system");
        super.runDiag();
    }
}
