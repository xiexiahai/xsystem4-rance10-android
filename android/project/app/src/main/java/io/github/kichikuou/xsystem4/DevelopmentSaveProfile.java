package io.github.kichikuou.xsystem4;

import java.io.File;
import java.io.IOException;

/** Validates development selection; persistence is handled by the launcher. */
final class DevelopmentSaveProfile {
    static final String EXTRA = "DEVELOPMENT_LAB_PROFILE";

    static final String PREFERENCE = "active_development_lab_profile";

    static File select(File filesDir, boolean present, String requested, String remembered) throws IOException {
        boolean lab = present || remembered != null;
        String name = present ? requested : remembered;
        if (lab && (name == null || !name.matches("lab-[A-Za-z0-9_-]{1,48}")))
            throw new IllegalArgumentException("Expected lab-[A-Za-z0-9_-]{1,48}");
        if (!lab) name = "main";
        File expectedRoot = new File(filesDir.getCanonicalFile(), "dev-saves");
        File root = expectedRoot.getCanonicalFile();
        if (!root.equals(expectedRoot))
            throw new IOException("Development save root redirects outside its canonical location");
        File expected = new File(root, name);
        File selected = expected.getCanonicalFile();
        if (!selected.equals(expected) || !root.equals(selected.getParentFile()))
            throw new IOException("Development profile redirects outside its canonical location");
        if (lab && !selected.isDirectory())
            throw new IOException("Selected development lab directory is missing or not a directory");
        return selected;
    }
}
