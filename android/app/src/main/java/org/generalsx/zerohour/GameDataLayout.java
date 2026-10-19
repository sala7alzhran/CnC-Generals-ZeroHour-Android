package org.generalsx.zerohour;

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * @port Android — which of the player's Zero Hour files the app imports, and whether a folder
 * (or the inside of a .zip) looks like a complete install. Plain Java so it can be unit-tested.
 *
 * Paths are relative, '/'-separated and compared case-insensitively, because the player's copy
 * comes from a Windows install. The imported layout mirrors the install folder: every *.big at
 * the root, the whole Data/ tree, and Steam's ZH_Generals/ (base-Generals archives), which the
 * engine looks for relative to its working directory (see registry.cpp tryRelativeGeneralsPath).
 */
final class GameDataLayout {

    /** Zero Hour archives present in every language version. */
    static final String[] ZH_ARCHIVES = {
        "INIZH.big", "W3DZH.big", "TexturesZH.big", "TerrainZH.big", "MapsZH.big",
        "WindowZH.big", "AudioZH.big", "SpeechZH.big", "MusicZH.big",
    };
    /** One of these must exist; the engine picks the game language from it (registry.cpp). */
    static final String[] LANGUAGE_ARCHIVES = {
        "EnglishZH.big", "BrazilianZH.big", "GermanZH.big", "FrenchZH.big",
        "SpanishZH.big", "ChineseZH.big", "KoreanZH.big", "PolishZH.big",
    };
    /** Base-Generals archives Zero Hour builds on: at the root, or in ZH_Generals/ on Steam. */
    static final String[] BASE_ARCHIVES = { "W3D.big", "Textures.big", "Terrain.big" };

    static final String MARKER_FILE = "INIZH.big";
    static final String DATA_DIR = "Data";
    static final String BASE_DIR = "ZH_Generals";

    private GameDataLayout() {}

    /**
     * Required files missing from {@code files} (paths relative to the install root). Each entry
     * is a display name such as "INIZH.big" or "Data/". Empty means the install looks complete.
     */
    static List<String> findMissing(Collection<String> files) {
        Set<String> lower = new HashSet<>();
        boolean hasData = false;
        for (String f : files) {
            String l = f.toLowerCase(Locale.ROOT);
            lower.add(l);
            if (l.startsWith("data/")) hasData = true;
        }
        List<String> missing = new ArrayList<>();
        for (String a : ZH_ARCHIVES) {
            if (!lower.contains(a.toLowerCase(Locale.ROOT))) missing.add(a);
        }
        boolean hasLanguage = false;
        for (String a : LANGUAGE_ARCHIVES) {
            hasLanguage |= lower.contains(a.toLowerCase(Locale.ROOT));
        }
        if (!hasLanguage) missing.add(LANGUAGE_ARCHIVES[0]);
        for (String a : BASE_ARCHIVES) {
            String l = a.toLowerCase(Locale.ROOT);
            String inBaseDir = BASE_DIR.toLowerCase(Locale.ROOT) + "/" + l;
            if (!lower.contains(l) && !lower.contains(inBaseDir)) missing.add(a);
        }
        if (!hasData) missing.add(DATA_DIR + "/");
        return missing;
    }

    /** True if any of the missing names is a base-Generals archive (worth an extra hint). */
    static boolean missesBaseArchives(List<String> missing) {
        for (String a : BASE_ARCHIVES) {
            if (missing.contains(a)) return true;
        }
        return false;
    }

    /** Whether a file at {@code rel} (relative to the install root) is copied into the app. */
    static boolean shouldImport(String rel) {
        String l = rel.toLowerCase(Locale.ROOT);
        String[] parts = l.split("/");
        if (parts.length == 1) return l.endsWith(".big") || l.equals("dxvk.conf");
        if (parts[0].equals("data")) return true;
        return parts.length == 2 && parts[0].equals(BASE_DIR.toLowerCase(Locale.ROOT))
                && l.endsWith(".big");
    }

    /**
     * Whether a zip entry could be part of the import before the install root is known: any
     * .big, anything below a Data/ folder, or dxvk.conf. Skips the .exe/.dll files of an install.
     */
    static boolean mayImportFromArchive(String rel) {
        String l = rel.toLowerCase(Locale.ROOT);
        return l.endsWith(".big") || l.equals("dxvk.conf") || l.endsWith("/dxvk.conf")
                || l.startsWith("data/") || l.contains("/data/");
    }

    /**
     * The install root inside a set of paths: the shallowest directory holding INIZH.big
     * ("" for the top level, otherwise "Some Folder/"), or null if there is none.
     */
    static String findRoot(Collection<String> paths) {
        String best = null;
        String marker = MARKER_FILE.toLowerCase(Locale.ROOT);
        for (String p : paths) {
            int slash = p.lastIndexOf('/');
            String name = p.substring(slash + 1).toLowerCase(Locale.ROOT);
            if (!name.equals(marker)) continue;
            String dir = p.substring(0, slash + 1);
            if (dir.toLowerCase(Locale.ROOT).endsWith("data/ini/")) continue; // duplicate copy some SKUs ship
            if (best == null || depth(dir) < depth(best)) best = dir;
        }
        return best;
    }

    private static int depth(String dir) {
        int n = 0;
        for (int i = 0; i < dir.length(); i++) {
            if (dir.charAt(i) == '/') n++;
        }
        return n;
    }

    /**
     * A zip entry name as a safe relative path ('/'-separated, no leading slash), or null if
     * it is a directory entry, empty, absolute, or tries to leave the target folder via "..".
     */
    static String safeRelativePath(String name) {
        String n = name.replace('\\', '/');
        if (n.isEmpty() || n.endsWith("/") || n.startsWith("/") || n.contains(":")) return null;
        StringBuilder out = new StringBuilder();
        for (String part : n.split("/")) {
            if (part.isEmpty() || part.equals(".")) continue;
            if (part.equals("..")) return null;
            if (out.length() > 0) out.append('/');
            out.append(part);
        }
        return out.length() == 0 ? null : out.toString();
    }
}
