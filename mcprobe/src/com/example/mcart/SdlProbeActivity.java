package com.example.mcart;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import org.libsdl.app.SDLActivity;

/**
 * 验证 SDL3 在 app 进程里能否 init + 建窗 + 建 GLES 上下文。
 *
 * 关键点：
 *  - getLibraries() 只返回 SDL3（不要 SDL 的 libmain.so）
 *  - main() 必须阻塞，否则 SDLMain 线程会在 main() 返回后 finish() 掉 Activity
 */
public class SdlProbeActivity extends SDLActivity {

    static {
        System.loadLibrary("glfw");
    }

    private static native int nProbe();
    private static native void nQuit();

    private volatile boolean mRunning = true;

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3" };
    }

    /** 不使用 SDL 的 native main；阻塞住，让 Activity 保持存活。 */
    @Override
    protected void main() {
        Log.i("SDLPROBE", "main() 被调用 -> 阻塞，保持 Activity");
        while (mRunning) {
            try {
                Thread.sleep(500);
            } catch (InterruptedException e) {
                break;
            }
        }
        Log.i("SDLPROBE", "main() 退出");
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Log.i("SDLPROBE", "onCreate done，3 秒后启动 SDL3 探针");
        new Handler(Looper.getMainLooper()).postDelayed(() -> {
            try {
                int rc = nProbe();
                Log.i("SDLPROBE", "nProbe 返回 " + rc);
            } catch (Throwable t) {
                Log.e("SDLPROBE", "nProbe 失败", t);
            }
        }, 3000);
    }

    @Override
    protected void onDestroy() {
        mRunning = false;
        super.onDestroy();
    }
}
