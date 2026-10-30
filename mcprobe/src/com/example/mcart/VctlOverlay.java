package com.example.mcart;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;

/**
 * 虚拟触屏按键的<b>可见浮层</b>（参照 FCL 的控制器绘制）。</p>
 *
 * 设计要点（和 FCL 一致 / 有意不同之处都写在这里）：
 * <ul>
 *   <li>本类<b>只负责画</b>，绝不参与命中判定 —— 触摸事件一律 {@link #onTouchEvent} 返回 false，
 *       落给下面的 SDL SurfaceView。所有几何与按下状态都通过 JNI 向 libglfw.so 查询，
 *       保证"画出来的框"和"真正能吃触摸的矩形"是同一套数（避免两处各算一套坐标对不上）。</li>
 *   <li>坐标是千分比 + 屏幕高基准（FCL 的 {@code percentageWidth.reference = SCREEN_HEIGHT}），
 *       换算在 native 的 {@code vctl_geo()} 里做，Java 侧只拿最终像素矩形。</li>
 *   <li>{@code IN_GAME} 语义照 FCL 的 {@code visibilityType}：光标被 grab（游戏内）时画游戏按键；
 *       进菜单（NORMAL）时只留 ESC/F3/回车/退格/攻击/使用这类菜单也用得上的键，游戏专用键隐藏。</li>
 *   <li>按下态换配色（FCL 的 {@code *Pressed} 一组样式）。</li>
 * </ul>
 */
public class VctlOverlay extends View {

    private static final String TAG = "VCTLVIEW";

    /** 状态位（与 glfw_sdl.c 的 Java_..._nativeState 严格一致） */
    private static final int BIT_IN_GAME  = 1 << 16;   // 1 = 光标 DISABLED（游戏内）
    private static final int BIT_STICK    = 1 << 17;   // 1 = 有手指按在摇杆上
    private static final int BIT_ENABLED  = 1 << 18;   // 1 = vcontrols.txt 里 enabled=1
    // bit19..bit23 = 十字键 上/左/中心/右/下 是否按下
    private static final int BIT_DPAD_MODE = 1 << 24;  // 1 = 当前是十字键模式（否则画摇杆）

    private static final int REFRESH_MS = 33;          // ≈30Hz 刷新（够跟手，又不费电）

    /**
     * 内部渲染分辨率缩放（由 GlfwProbeActivity 在建窗后设置）。
     * native 给的矩形是「buffer 像素」空间（宽高已乘过 scale），
     * 而本 View 画在「视图像素」空间（整屏），所以要乘 1/scale 还原。
     */
    private static float sRenderScale = 1.0f;
    public static void setRenderScale(float s) {
        sRenderScale = (s > 0.05f && s <= 1.0f) ? s : 1.0f;
    }
    private static float invScale() {
        return sRenderScale >= 0.995f ? 1.0f : 1.0f / sRenderScale;
    }

    private static native int    nativeCount();
    private static native String nativeName(int i);
    private static native String nativeLabel(int i);
    private static native int    nativeVis(int i);          // 0=ALWAYS 1=IN_GAME
    private static native void   nativeRect(int i, float[] out);   // {x,y,w,h} 像素
    private static native void   nativeStickRect(float[] out);
    private static native int    nativeState();
    /** 十字键某一格的像素矩形：idx 0=上 1=左 2=中心 3=右 4=下 */
    private static native void   nativeDpadRect(int idx, float[] out);
    /** 十字键 3x3 网格任意一格（col/row 0..2）：{x,y,w,h} 像素，纯贴图用 */
    private static native void   nativeDpadCellRect(int col, int row, float[] out);

    /**
     * 十字键贴图的「紧凑度」：按钮**大小不变**，只把 8 个方向贴图**向中心的「潜」字收拢**。
     * 1.0 = 原始格子位置；0.78 = 每个贴图中心朝中心点移动 22%。
     * 纯视觉参数：命中判定由 native 按角度在整个方块内算 8 向，完全不受这里影响。
     */
    private static final float DPAD_COMPACT = 0.78f;

    // ── 热键栏（快捷栏）：点第 i 槽 = native 注入 GLFW_KEY_(i+1) ──
    private static native int    nativeHotbarOn();
    private static native int    nativeHotbarCount();
    private static native int    nativeHotbarSel();     // -1 = 还没选过
    private static native void   nativeHotbarRect(int idx, float[] out);

    private static boolean sNativeOk = true;

    private final Paint mFill   = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mStroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mText   = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF mR      = new RectF();
    private final float[] mTmp  = new float[4];

    private String[] mLabels;
    private int[]    mVis;
    private int      mN = -1;
    private boolean  mLoggedOnce;

    public VctlOverlay(Context ctx) {
        super(ctx);
        // —— 绝不参与触摸/焦点：事件要落到下面的 SDL 视图 ——
        setClickable(false);
        setFocusable(false);
        setFocusableInTouchMode(false);
        setWillNotDraw(false);
        setLayerType(LAYER_TYPE_HARDWARE, null);

        mFill.setStyle(Paint.Style.FILL);
        mStroke.setStyle(Paint.Style.STROKE);
        mStroke.setStrokeWidth(3f);
        mText.setTextAlign(Paint.Align.CENTER);
        mText.setFakeBoldText(true);
    }

    /** 明确不吃事件：即使有人给本 View 设了 listener，也能保证返回 false。 */
    @Override
    public boolean onTouchEvent(MotionEvent e) {
        return false;
    }

    @Override
    public boolean onCheckIsTextEditor() {
        return false;
    }

    /** 一次性把名字/可见性取回来（布局变了会重建，按 count 变化判断） */
    private boolean loadMeta(int n) {
        try {
            String[] labels = new String[n];
            int[] vis = new int[n];
            for (int i = 0; i < n; i++) {
                String s = nativeLabel(i);
                if (s == null) s = nativeName(i);
                labels[i] = (s == null) ? "" : s;
                vis[i] = nativeVis(i);
            }
            mLabels = labels;
            mVis = vis;
            mN = n;
            return true;
        } catch (Throwable t) {
            Log.w(TAG, "读取按钮表失败", t);
            return false;
        }
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (!sNativeOk) return;

        final int n, st;
        try {
            n = nativeCount();
            if (n <= 0) {                       // 窗口还没建好 / enabled=0
                postInvalidateDelayed(REFRESH_MS);
                return;
            }
            if (n != mN && !loadMeta(n)) { postInvalidateDelayed(REFRESH_MS); return; }
            st = nativeState();
        } catch (UnsatisfiedLinkError e) {
            sNativeOk = false;
            Log.e(TAG, "libglfw.so 里没有 VctlOverlay 的 JNI 符号，浮层关闭", e);
            return;
        } catch (Throwable t) {
            Log.w(TAG, "native 查询失败（忽略本帧）", t);
            postInvalidateDelayed(REFRESH_MS);
            return;
        }

        if ((st & BIT_ENABLED) == 0) {          // 配置里关掉了
            postInvalidateDelayed(REFRESH_MS);
            return;
        }

        final boolean inGame  = (st & BIT_IN_GAME) != 0;
        final boolean stickOn = (st & BIT_STICK) != 0;

        int drawn = 0;
        for (int i = 0; i < n; i++) {
            // IN_GAME 的键在菜单里隐藏（FCL 的 visibilityType 语义）
            if (mVis[i] == 1 && !inGame) continue;
            nativeRect(i, mTmp);
            final float k = invScale();
            mTmp[0] *= k; mTmp[1] *= k; mTmp[2] *= k; mTmp[3] *= k;
            float w = mTmp[2], h = mTmp[3];
            if (w <= 1f || h <= 1f) continue;   // 已禁用（w/h=0）
            final boolean pressed = (st & (1 << i)) != 0;
            drawButton(canvas, mTmp[0], mTmp[1], w, h, mLabels[i], pressed);
            drawn++;
        }

        // 移动控件：十字键(D-pad) 或 摇杆，只在游戏内显示
        final float k2 = invScale();
        if (inGame) {
            if ((st & BIT_DPAD_MODE) != 0) {
                // 十字键贴图：3x3 全画 —— 四条主向 + **四个斜角** + 中心潜行。
                // 判定不在这里：命中由 native 按角度在整个方块内算 8 向，
                // 所以贴图可以往各自中心内缩（DPAD_GLYPH_INSET）留出空隙，判定区不受影响。
                final String[][] glyph = {
                        { "↖", "▲", "↗" },
                        { "◀", "潜", "▶" },
                        { "↙", "▼", "↘" }
                };
                final boolean dUp    = (st & (1 << 19)) != 0;
                final boolean dLeft  = (st & (1 << 20)) != 0;
                final boolean dCtr   = (st & (1 << 21)) != 0;
                final boolean dRight = (st & (1 << 22)) != 0;
                final boolean dDown  = (st & (1 << 23)) != 0;

                // 先用中心格（col=1,row=1 的「潜」）求出网格中心，后面所有贴图都朝它收拢
                float cx = 0f, cy = 0f;
                final float ks = invScale();
                nativeDpadCellRect(1, 1, mTmp);
                mTmp[0] *= ks; mTmp[1] *= ks; mTmp[2] *= ks; mTmp[3] *= ks;
                cx = mTmp[0] + mTmp[2] * 0.5f;
                cy = mTmp[1] + mTmp[3] * 0.5f;

                for (int row = 0; row < 3; row++) {
                    for (int col = 0; col < 3; col++) {
                        nativeDpadCellRect(col, row, mTmp);
                        mTmp[0] *= ks; mTmp[1] *= ks; mTmp[2] *= ks; mTmp[3] *= ks;
                        float w = mTmp[2], h = mTmp[3];
                        if (w <= 1f || h <= 1f) continue;      // 格子太小/未启用

                        // 斜角格的"按下"= 相邻两个主向同时按下（斜向跑动时两键是同时按的）
                        boolean pressed;
                        if (row == 1 && col == 1)      pressed = dCtr;
                        else if (row == 0 && col == 1) pressed = dUp;
                        else if (row == 2 && col == 1) pressed = dDown;
                        else if (row == 1 && col == 0) pressed = dLeft;
                        else if (row == 1 && col == 2) pressed = dRight;
                        else if (row == 0 && col == 0) pressed = dUp && dLeft;
                        else if (row == 0 && col == 2) pressed = dUp && dRight;
                        else if (row == 2 && col == 0) pressed = dDown && dLeft;
                        else                           pressed = dDown && dRight;

                        // 贴图位置：把该格中心朝网格中心收拢（按钮尺寸 w/h 不变）
                        float gcx = mTmp[0] + w * 0.5f;
                        float gcy = mTmp[1] + h * 0.5f;
                        float dx = cx + (gcx - cx) * DPAD_COMPACT - w * 0.5f;
                        float dy = cy + (gcy - cy) * DPAD_COMPACT - h * 0.5f;

                        // 斜角格画淡一点，避免视觉上和主向抢
                        boolean diag = (row != 1 && col != 1);
                        drawDpadCell(canvas, dx, dy, w, h, glyph[row][col], pressed, diag);
                        drawn++;
                    }
                }
            } else {
                nativeStickRect(mTmp);
                mTmp[0] *= k2; mTmp[1] *= k2; mTmp[2] *= k2; mTmp[3] *= k2;
                float w = mTmp[2], h = mTmp[3];
                if (w > 1f && h > 1f) {
                    drawStick(canvas, mTmp[0], mTmp[1], w, h, stickOn);
                    drawn++;
                }
            }
        }

        // 热键栏（快捷栏）：槽位与命中判定都在 native，这里只负责画
        if (inGame && nativeHotbarOn() != 0) {
            final int hn = nativeHotbarCount();
            final int sel = nativeHotbarSel();
            for (int i = 0; i < hn; i++) {
                nativeHotbarRect(i, mTmp);
                mTmp[0] *= k2; mTmp[1] *= k2; mTmp[2] *= k2; mTmp[3] *= k2;
                float w = mTmp[2], h = mTmp[3];
                if (w <= 1f || h <= 1f) continue;      // 该槽不可用
                drawButton(canvas, mTmp[0], mTmp[1], w, h, String.valueOf(i + 1), i == sel);
                drawn++;
            }
        }

        if (!mLoggedOnce && drawn > 0) {
            mLoggedOnce = true;
            Log.i(TAG, "浮层已开始绘制：按钮 " + drawn + " 个，窗口 "
                    + getWidth() + "x" + getHeight() + "，游戏内=" + inGame);
        }

        postInvalidateDelayed(REFRESH_MS);      // 自驱动刷新（≈30Hz）
    }

    /** 十字键格子：斜角格画得淡一些（alpha 略低），主向/中心用常规样式 */
    private void drawDpadCell(Canvas c, float x, float y, float w, float h,
                              String label, boolean pressed, boolean diagonal) {
        int save = c.saveLayerAlpha(x, y, x + w, y + h,
                diagonal && !pressed ? 0xB4 : 0xFF);   // 斜角格约 70% 不透明
        drawButton(c, x, y, w, h, label, pressed);
        c.restoreToCount(save);
    }

    /** 圆角矩形 + 描边 + 居中文字；按下态换配色 */
    private void drawButton(Canvas c, float x, float y, float w, float h,
                            String label, boolean pressed) {
        float radius = Math.min(w, h) * 0.28f;
        mR.set(x, y, x + w, y + h);

        mFill.setColor(pressed ? 0x66FFFFFF : 0x40000000);
        c.drawRoundRect(mR, radius, radius, mFill);

        mStroke.setColor(pressed ? 0xFFFFFFFF : 0xA6FFFFFF);
        c.drawRoundRect(mR, radius, radius, mStroke);

        if (label != null && label.length() > 0) {
            mText.setTextSize(Math.min(w, h) * 0.44f);
            mText.setColor(pressed ? 0xFF101010 : 0xF0FFFFFF);
            mText.setShadowLayer(2.5f, 0f, 0f, 0xFF000000);
            Paint.FontMetrics fm = mText.getFontMetrics();
            float baseline = y + h / 2f - (fm.ascent + fm.descent) / 2f;
            c.drawText(label, x + w / 2f, baseline, mText);
        }
    }

    /** 摇杆：外圈 + 内点（按住时高亮） */
    private void drawStick(Canvas c, float x, float y, float w, float h, boolean held) {
        float cx = x + w / 2f, cy = y + h / 2f;
        float r  = Math.min(w, h) / 2f;

        mFill.setColor(held ? 0x33FFFFFF : 0x2A000000);
        c.drawCircle(cx, cy, r, mFill);

        mStroke.setColor(held ? 0xFFFFFFFF : 0x8CFFFFFF);
        c.drawCircle(cx, cy, r, mStroke);

        mFill.setColor(held ? 0xCCFFFFFF : 0x66FFFFFF);
        c.drawCircle(cx, cy, r * 0.16f, mFill);
    }
}
