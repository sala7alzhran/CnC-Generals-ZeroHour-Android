package org.generalsx.zerohour;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public class PlayerNameTest {

    @Test public void usesTheNameTheUserGaveThePhone() {
        assertEquals("POCO F7 Pro", PlayerName.pick("POCO F7 Pro", "25053PC47G"));
    }

    @Test public void fallsBackToTheModelThenToPlayer() {
        assertEquals("SM-A546E", PlayerName.pick(null, "SM-A546E"));
        assertEquals("SM-A546E", PlayerName.pick("   ", "SM-A546E"));
        assertEquals("Player", PlayerName.pick(null, null));
    }

    @Test public void keepsOnlyPlainLatinCharacters() {
        assertEquals("Galaxy", PlayerName.pick("جوال Galaxy", null));
        assertEquals("SM-A546E", PlayerName.pick("جوال صالح", "SM-A546E"));   // nothing Latin left
        assertEquals("Ahmeds A55", PlayerName.pick("Ahmed's  A55", null));
    }

    @Test public void cutsToTheLobbyLimit() {
        String name = PlayerName.pick("Samsung Galaxy S24 Ultra", null);
        assertEquals("Samsung Gala", name);
        assertEquals(PlayerName.MAX_LENGTH, name.length());
    }
}
