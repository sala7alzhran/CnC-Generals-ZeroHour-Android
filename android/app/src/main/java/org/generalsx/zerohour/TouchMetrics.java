package org.generalsx.zerohour;

/**
 * Pure helpers for the engine's touch layer (SDL3GameEngine.cpp), kept free of Android types so
 * they can be unit-tested on the JVM. The engine sizes every gesture threshold in millimetres
 * (tap slop, double-tap distance, edge-pan band), so it needs the display's real pixel density.
 */
public final class TouchMetrics {
    private TouchMetrics() {}

    /** Panels outside this range are not phones; the value is treated as bogus. */
    static final float MIN_PLAUSIBLE_DPI = 100f;
    static final float MAX_PLAUSIBLE_DPI = 1000f;
    /** A physical dpi this far from the logical bucket is more likely a driver bug than a real panel. */
    static final float MAX_RELATIVE_DEVIATION = 0.35f;

    /**
     * Pixels per millimetre from DisplayMetrics.xdpi, falling back to the logical density bucket
     * when the reported physical dpi is implausible or disagrees with the bucket by more than a
     * third (some OEM panels report nonsense such as 160 or 0). Returns 0 when nothing is usable.
     */
    public static float pixelsPerMm(float xdpi, int densityDpi) {
        float dpi = xdpi;
        boolean plausible = dpi >= MIN_PLAUSIBLE_DPI && dpi <= MAX_PLAUSIBLE_DPI;
        boolean agrees = densityDpi > 0 && Math.abs(dpi - densityDpi) <= densityDpi * MAX_RELATIVE_DEVIATION;
        if (!plausible || !agrees) {
            dpi = densityDpi;
        }
        if (!(dpi >= MIN_PLAUSIBLE_DPI && dpi <= MAX_PLAUSIBLE_DPI)) {
            return 0f;
        }
        return dpi / 25.4f;
    }
}
