package io.github.kichikuou.xsystem4;

/** Select a relative app-private save profile without copying or merging files. */
final class SaveProfile {
    static final String MAIN = "saves/main";
    static final String CHEAT_MAIN = "cheat-saves/main";
    interface DirectoryExists { boolean test(String relative); }

    static boolean supported(String value) {
        return MAIN.equals(value) || (value != null &&
                value.matches("resume-probe-saves/v[1-9][0-9]{0,2}"));
    }

    static String select(String previous, String requested, DirectoryExists exists) {
        if (requested != null) return supported(requested) ? requested : MAIN;
        return supported(previous) && exists.test(previous) ? previous : MAIN;
    }

    static boolean supportedCheat(String value) {
        return CHEAT_MAIN.equals(value) || (value != null &&
                value.matches("cheat-probe-saves/v[1-9][0-9]{0,2}"));
    }

    static String selectCheat(String previous, String requested, DirectoryExists exists) {
        if (requested != null) return supportedCheat(requested) ? requested : CHEAT_MAIN;
        return supportedCheat(previous) && exists.test(previous) ? previous : CHEAT_MAIN;
    }
}
