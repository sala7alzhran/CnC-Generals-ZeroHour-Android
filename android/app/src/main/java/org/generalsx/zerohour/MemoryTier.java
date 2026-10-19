package org.generalsx.zerohour;

/**
 * Pure helper for the engine's graphics defaults (W3DDisplay.cpp androidGraphicsTier), kept free
 * of Android types so it can be unit-tested on the JVM. On the first run the engine picks the
 * render resolution and the detail level from the lower of two guesses, the GPU family and this
 * memory class; phones with little memory also tend to have the slower GPUs.
 */
public final class MemoryTier {
    private MemoryTier() {}

    static final int LOW = 0;
    static final int MID = 1;
    static final int HIGH = 2;

    private static final long GIB = 1L << 30;

    /**
     * Memory class from ActivityManager.MemoryInfo.totalMem, which reports less than the size on
     * the box (a "4 GB" phone shows about 3.6 GiB, a "6 GB" one about 5.5 GiB, an "8 GB" one
     * about 7.4 GiB): 4 GB or less, or a device flagged low-RAM, is LOW; about 6 GB is MID; more
     * is HIGH. An unknown size (0) counts as HIGH so nothing is taken away by mistake.
     */
    public static int fromTotalMemory(long totalBytes, boolean lowRamDevice) {
        if (lowRamDevice) return LOW;
        if (totalBytes <= 0) return HIGH;
        if (totalBytes < 9 * GIB / 2) return LOW;      // under 4.5 GiB
        if (totalBytes < 7 * GIB) return MID;
        return HIGH;
    }
}
