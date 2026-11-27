package io.github.kichikuou.xsystem4;

/** Select a relative app-private save profile without copying or merging files. */
final class SaveProfile {
    static final String PREFERENCES = "rance10-launch";
    static final String MAIN = "saves/main";
    static final String CHEAT_MAIN = "cheat-saves/main";
    interface DirectoryExists { boolean test(String relative); }

    static String activeKey(boolean cheats) {
        return cheats ? "active_cheat_save_profile" : "active_save_profile";
    }

    static String activeKey(boolean cheats, boolean sunset) {
        return sunset ? activeKey(cheats) + "_sunset_049" : activeKey(cheats);
    }

    static String sunset(boolean cheats) {
        return cheats ? "cheat-saves/sunset-049" : "saves/sunset-049";
    }

    static String imported(boolean cheats, String id) {
        return (cheats ? "cheat-saves" : "saves") + "/imported-" + id;
    }

    static String imported(boolean cheats, boolean sunset, String id) {
        return sunset ? sunset(cheats) + "-imported-" + id : imported(cheats, id);
    }

    static boolean supportedForGame(String value, boolean cheats, boolean sunset) {
        if (!sunset) return cheats ? supportedCheat(value) : supported(value);
        String base = sunset(cheats);
        return base.equals(value) || (value != null && value.matches(base + "-imported-[a-f0-9]{32}"));
    }

    static String selectForGame(String previous, boolean cheats, boolean sunset, DirectoryExists exists) {
        if (!sunset) return cheats ? selectCheat(previous, null, exists) : select(previous, null, exists);
        return supportedForGame(previous, cheats, true) && exists.test(previous) ? previous : sunset(cheats);
    }

    static boolean supported(String value) {
        return MAIN.equals(value) || (value != null &&
                value.matches("resume-probe-saves/v[1-9][0-9]{0,2}|saves/imported-[a-f0-9]{32}"));
    }

    static String select(String previous, String requested, DirectoryExists exists) {
        if (requested != null) return supported(requested) ? requested : MAIN;
        return supported(previous) && exists.test(previous) ? previous : MAIN;
    }

    static boolean supportedCheat(String value) {
        return CHEAT_MAIN.equals(value) || (value != null &&
                value.matches("cheat-probe-saves/v[1-9][0-9]{0,2}|cheat-saves/imported-[a-f0-9]{32}"));
    }

    static String selectCheat(String previous, String requested, DirectoryExists exists) {
        if (requested != null) return supportedCheat(requested) ? requested : CHEAT_MAIN;
        return supportedCheat(previous) && exists.test(previous) ? previous : CHEAT_MAIN;
    }
}
