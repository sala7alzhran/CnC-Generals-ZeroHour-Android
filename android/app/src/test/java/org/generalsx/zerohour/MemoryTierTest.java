package org.generalsx.zerohour;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public class MemoryTierTest {

    private static final long MIB = 1L << 20;

    @Test public void phonesByTheMemoryTheyReport() {
        assertEquals(MemoryTier.LOW, MemoryTier.fromTotalMemory(1850 * MIB, false));   // "2 GB"
        assertEquals(MemoryTier.LOW, MemoryTier.fromTotalMemory(3700 * MIB, false));   // "4 GB"
        assertEquals(MemoryTier.MID, MemoryTier.fromTotalMemory(5600 * MIB, false));   // "6 GB"
        assertEquals(MemoryTier.HIGH, MemoryTier.fromTotalMemory(7500 * MIB, false));  // "8 GB"
        assertEquals(MemoryTier.HIGH, MemoryTier.fromTotalMemory(11300 * MIB, false)); // "12 GB"
    }

    @Test public void lowRamFlagWins() {
        assertEquals(MemoryTier.LOW, MemoryTier.fromTotalMemory(8000 * MIB, true));
    }

    @Test public void unknownSizeTakesNothingAway() {
        assertEquals(MemoryTier.HIGH, MemoryTier.fromTotalMemory(0, false));
    }
}
