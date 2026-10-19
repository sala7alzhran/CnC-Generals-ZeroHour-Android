package org.generalsx.zerohour;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public class TouchMetricsTest {

    private static final float EPS = 0.01f;

    @Test public void trustsAPlausiblePhysicalDpi() {
        // POCO F7 Pro at 1080p: ~395 dpi physical, xxhdpi (480) logical bucket.
        assertEquals(395f / 25.4f, TouchMetrics.pixelsPerMm(395f, 480), EPS);
    }

    @Test public void fallsBackToTheBucketWhenXdpiIsBogus() {
        assertEquals(480f / 25.4f, TouchMetrics.pixelsPerMm(0f, 480), EPS);      // unset
        assertEquals(480f / 25.4f, TouchMetrics.pixelsPerMm(160f, 480), EPS);    // mdpi placeholder
        assertEquals(480f / 25.4f, TouchMetrics.pixelsPerMm(4000f, 480), EPS);   // absurd
    }

    @Test public void fallsBackWhenXdpiDisagreesWithTheBucketTooMuch() {
        // 35% is the limit: 480 * 0.65 = 312 is still accepted, 300 is not.
        assertEquals(312f / 25.4f, TouchMetrics.pixelsPerMm(312f, 480), EPS);
        assertEquals(480f / 25.4f, TouchMetrics.pixelsPerMm(300f, 480), EPS);
    }

    @Test public void returnsZeroWhenNothingIsUsable() {
        assertEquals(0f, TouchMetrics.pixelsPerMm(0f, 0), EPS);
        assertEquals(0f, TouchMetrics.pixelsPerMm(50f, 50), EPS);
    }
}
