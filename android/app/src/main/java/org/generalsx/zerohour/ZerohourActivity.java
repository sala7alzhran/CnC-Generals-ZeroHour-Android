
package org.generalsx.zerohour;

import android.app.ActivityManager;
import android.content.Context;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.util.DisplayMetrics;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

public class ZerohourActivity extends SDLActivity
{
    // @port Android: LAN multiplayer discovery uses UDP broadcast/multicast on
    // the local WiFi. Android's WiFi driver drops multicast/broadcast packets
    // that are not addressed to this device unless a MulticastLock is held, so
    // without this the two phones cannot see each other's hosted LAN games.
    private WifiManager.MulticastLock mMulticastLock;

    // @port Android: the game must own the whole screen. SDLActivity hides the system bars with
    // the legacy View.SYSTEM_UI_FLAG_* flags and re-hides them from a
    // setOnSystemUiVisibilityChangeListener callback; both are deprecated since API 30 and on
    // Android 15/16 (edge-to-edge enforced for targetSdk 35) the re-hide no longer fires, so one
    // swipe, dialog or soft-keyboard round trip left the status bar (top 121 px) and the
    // navigation bar (right 133 px) painted over the game for the rest of the session. We hide
    // the bars with WindowInsetsController, let a swipe show them only transiently, and re-apply
    // whenever the window gains focus, resumes, or the insets report a bar visible again.
    private final Handler mUiHandler = new Handler(Looper.getMainLooper());
    private final Runnable mRehideSystemBars = this::applyImmersiveMode;
    private static final long REHIDE_DELAY_MS = 1500;   // let a deliberate swipe-reveal be seen first

    private void applyImmersiveMode() {
        Window window = getWindow();
        if (window == null) {
            return;
        }
        View decor = window.getDecorView();
        WindowManager.LayoutParams lp = window.getAttributes();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false);
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
            }
        } else {
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
        window.setAttributes(lp);
    }

    private void scheduleRehideSystemBars() {
        mUiHandler.removeCallbacks(mRehideSystemBars);   // one pending re-hide at a time
        mUiHandler.postDelayed(mRehideSystemBars, REHIDE_DELAY_MS);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        applyImmersiveMode();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // The decor view sees every insets change; SDLSurface keeps its own listener for the
            // safe-area it reports to native, and this one does not consume anything.
            getWindow().getDecorView().setOnApplyWindowInsetsListener((v, insets) -> {
                if (insets.isVisible(WindowInsets.Type.statusBars())
                        || insets.isVisible(WindowInsets.Type.navigationBars())) {
                    scheduleRehideSystemBars();
                }
                return v.onApplyWindowInsets(insets);
            });
        }
        try {
            WifiManager wifi = (WifiManager) getApplicationContext()
                    .getSystemService(Context.WIFI_SERVICE);
            if (wifi != null) {
                mMulticastLock = wifi.createMulticastLock("generalsx-lan");
                mMulticastLock.setReferenceCounted(true);
                mMulticastLock.acquire();
            }
        } catch (Exception e) {
            // Non-fatal: LAN discovery may be less reliable without the lock.
        }
    }

    // @port Android: the engine creates a resizable SDL window and sets no
    // SDL_HINT_ORIENTATIONS, so SDL replaces the manifest's landscape lock with
    // FULL_USER. A phone lying flat or held upright then rotates the surface to
    // portrait while DXVK is creating its swapchain, and the swapchain cannot be
    // recreated ("native_window_api_connect() failed ... already connected"), so
    // the game exits on startup. Always request landscape, either way up.
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        super.setOrientationBis(w, h, resizable, "LandscapeLeft LandscapeRight");
    }

    @Override
    protected void onResume() {
        super.onResume();
        applyImmersiveMode();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            // Coming back from a system dialog, the folder picker or the keyboard: the bars may
            // have been restored meanwhile.
            applyImmersiveMode();
        }
    }

    // ---- Touch-controls bridge: called from the engine's Android touch layer over JNI ----
    // (SDL3GameEngine.cpp looks these up by name on the running activity's class.)

    /** Set to false to silence the gesture haptics (a settings toggle will drive this later). */
    public static volatile boolean sHapticsEnabled = true;

    /**
     * Physical pixels per millimetre of this display, so the engine can size tap/drag thresholds
     * in millimetres instead of pixels. 0 when unknown; the engine then falls back to SDL's scale.
     */
    public static float getPixelsPerMm() {
        Context ctx = getContext();
        if (ctx == null) {
            return 0f;
        }
        DisplayMetrics dm = ctx.getResources().getDisplayMetrics();
        return TouchMetrics.pixelsPerMm(dm.xdpi, dm.densityDpi);
    }

    /**
     * Memory class of this phone for the engine's first-run graphics defaults: 0 = 4 GB or less,
     * 1 = about 6 GB, 2 = more (see MemoryTier). Called over JNI from W3DDisplay.cpp.
     */
    public static int getMemoryTier() {
        Context ctx = getContext();
        if (ctx == null) {
            return MemoryTier.HIGH;
        }
        ActivityManager am = (ActivityManager) ctx.getSystemService(Context.ACTIVITY_SERVICE);
        if (am == null) {
            return MemoryTier.HIGH;
        }
        ActivityManager.MemoryInfo info = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(info);
        return MemoryTier.fromTotalMemory(info.totalMem, am.isLowRamDevice());
    }

    /**
     * Short haptic for a gesture state change. kind: 0 = tick (long-press armed),
     * 1 = click (selection / order acknowledged), 2 = heavy click (error). Best effort.
     */
    public static void hapticTick(int kind) {
        if (!sHapticsEnabled) {
            return;
        }
        Context ctx = getContext();
        if (ctx == null) {
            return;
        }
        try {
            Vibrator vibrator = (Vibrator) ctx.getSystemService(Context.VIBRATOR_SERVICE);
            if (vibrator == null || !vibrator.hasVibrator()) {
                return;
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                int effect = kind == 2 ? VibrationEffect.EFFECT_HEAVY_CLICK
                           : kind == 1 ? VibrationEffect.EFFECT_CLICK
                           : VibrationEffect.EFFECT_TICK;
                vibrator.vibrate(VibrationEffect.createPredefined(effect));
            } else {
                long ms = kind == 2 ? 40 : kind == 1 ? 20 : 10;
                vibrator.vibrate(VibrationEffect.createOneShot(ms, VibrationEffect.DEFAULT_AMPLITUDE));
            }
        } catch (Exception e) {
            // Haptics are a nicety; never let them disturb the game.
        }
    }

    @Override
    protected void onDestroy() {
        try {
            if (mMulticastLock != null && mMulticastLock.isHeld()) {
                mMulticastLock.release();
            }
        } catch (Exception e) {
            // ignore
        }
        super.onDestroy();
    }
}
