package org.generalsx.zerohour;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import org.junit.Test;

public class GameDataLayoutTest {

    /** A Steam-style install: ZH archives at the root, base Generals in ZH_Generals/. */
    private static List<String> steamInstall() {
        List<String> files = new ArrayList<>(Arrays.asList(GameDataLayout.ZH_ARCHIVES));
        files.add("EnglishZH.big");
        files.add("generals.exe");
        files.add("ZH_Generals/W3D.big");
        files.add("ZH_Generals/textures.big");   // case differs from the Windows name list
        files.add("ZH_Generals/Terrain.big");
        files.add("Data/INI/INIZH.big");
        files.add("Data/Movies/sizzle_review.bik");
        return files;
    }

    @Test public void completeSteamInstallHasNothingMissing() {
        assertEquals(new ArrayList<String>(), GameDataLayout.findMissing(steamInstall()));
    }

    @Test public void namesEveryMissingFile() {
        List<String> files = steamInstall();
        files.remove("INIZH.big");
        files.remove("ZH_Generals/W3D.big");
        files.removeIf(f -> f.startsWith("Data/"));
        List<String> missing = GameDataLayout.findMissing(files);
        assertEquals(Arrays.asList("INIZH.big", "W3D.big", "Data/"), missing);
        assertTrue(GameDataLayout.missesBaseArchives(missing));
    }

    @Test public void anyLanguageArchiveIsEnough() {
        List<String> files = steamInstall();
        files.remove("EnglishZH.big");
        assertEquals(Arrays.asList("EnglishZH.big"), GameDataLayout.findMissing(files));
        files.add("GermanZH.big");
        assertTrue(GameDataLayout.findMissing(files).isEmpty());
    }

    @Test public void baseArchivesMayAlsoSitAtTheRoot() {
        List<String> files = new ArrayList<>(Arrays.asList(GameDataLayout.ZH_ARCHIVES));
        files.addAll(Arrays.asList("EnglishZH.big", "W3D.big", "Textures.big", "Terrain.big", "Data/INI/x.ini"));
        assertTrue(GameDataLayout.findMissing(files).isEmpty());
    }

    @Test public void importsOnlyGameData() {
        assertTrue(GameDataLayout.shouldImport("INIZH.big"));
        assertTrue(GameDataLayout.shouldImport("dxvk.conf"));
        assertTrue(GameDataLayout.shouldImport("Data/Movies/a.bik"));
        assertTrue(GameDataLayout.shouldImport("ZH_Generals/W3D.big"));
        assertFalse(GameDataLayout.shouldImport("generals.exe"));
        assertFalse(GameDataLayout.shouldImport("ZH_Generals/Data/Movies/a.bik"));
        assertFalse(GameDataLayout.shouldImport("Other/INIZH.big"));
    }

    @Test public void findsTheShallowestInstallRoot() {
        List<String> zip = Arrays.asList(
                "Zero Hour/Data/INI/INIZH.big", "Zero Hour/INIZH.big", "Zero Hour/W3DZH.big",
                "Zero Hour/Backup/Old/INIZH.big");
        assertEquals("Zero Hour/", GameDataLayout.findRoot(zip));
        assertEquals("", GameDataLayout.findRoot(Arrays.asList("inizh.BIG", "Data/a")));
        assertNull(GameDataLayout.findRoot(Arrays.asList("Data/INI/INIZH.big", "readme.txt")));
    }

    @Test public void rejectsUnsafeZipPaths() {
        assertNull(GameDataLayout.safeRelativePath("../../shared_prefs/x.xml"));
        assertNull(GameDataLayout.safeRelativePath("Data/../../x"));
        assertNull(GameDataLayout.safeRelativePath("/data/data/x"));
        assertNull(GameDataLayout.safeRelativePath("C:\\Games\\INIZH.big"));
        assertNull(GameDataLayout.safeRelativePath("Data/"));
        assertEquals("Data/INI/a.ini", GameDataLayout.safeRelativePath("Data\\INI\\a.ini"));
        assertEquals("Zero Hour/INIZH.big", GameDataLayout.safeRelativePath("./Zero Hour//INIZH.big"));
    }
}
