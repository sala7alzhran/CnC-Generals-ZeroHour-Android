
package org.generalsx.zerohour;

import android.content.Context;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.util.DisplayMetrics;

import org.libsdl.app.SDLActivity;

public class ZerohourActivity extends SDLActivity
{
    // @port Android: LAN multiplayer discovery uses UDP broadcast/multicast on
    // the local WiFi. Android's WiFi driver drops multicast/broadcast packets
    // that are not addressed to this device unless a MulticastLock is held, so
    // without this the two phones cannot see each other's hosted LAN games.
    private WifiManager.MulticastLock mMulticastLock;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
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
