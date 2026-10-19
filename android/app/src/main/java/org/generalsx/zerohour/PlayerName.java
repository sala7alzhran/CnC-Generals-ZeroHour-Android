package org.generalsx.zerohour;

/**
 * Pure helper for the default player name, kept free of Android types so it can be unit-tested on
 * the JVM. The engine names the local player after the machine (IPEnumeration::getMachineName,
 * gethostname), which is "localhost" on every Android phone, so two phones in one LAN lobby got
 * the same name. ZerohourActivity hands the engine the phone's own name instead.
 */
public final class PlayerName {
    private PlayerName() {}

    /** The LAN lobby's limit (g_lanPlayerNameLength in LANAPI.h). */
    static final int MAX_LENGTH = 12;
    static final String FALLBACK = "Player";

    /**
     * The phone's name as the user set it (Settings.Global "device_name", e.g. "POCO F7 Pro"),
     * else the model (Build.MODEL), else "Player". Only letters, digits, space, '-', '_' and '.'
     * of plain ASCII are kept (the game's fonts and lobby packets are built for Latin names), runs
     * of spaces collapse, and the result is cut to the lobby's 12 characters.
     */
    public static String pick(String deviceName, String model) {
        String name = clean(deviceName);
        if (name.isEmpty()) name = clean(model);
        return name.isEmpty() ? FALLBACK : name;
    }

    static String clean(String raw) {
        if (raw == null) return "";
        StringBuilder out = new StringBuilder();
        boolean pendingSpace = false;
        for (int i = 0; i < raw.length() && out.length() < MAX_LENGTH; i++) {
            char c = raw.charAt(i);
            boolean keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                    || c == '-' || c == '_' || c == '.';
            if (keep) {
                if (pendingSpace && out.length() > 0 && out.length() < MAX_LENGTH - 1) out.append(' ');
                pendingSpace = false;
                out.append(c);
            } else if (c == ' ') {
                pendingSpace = true;
            }
        }
        return out.toString().trim();
    }
}
