package io.github.kichikuou.xsystem4;

import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.Locale;
import java.util.Set;

/** Observation-only choices; no input injection, forced state, or save profile switches. */
final class Rance10DiagnosticOptions {
    private static final Set<String> SUPPORTED;
    static {
        Set<String> supported = new LinkedHashSet<>();
        Collections.addAll(supported, "memory", "save", "parts", "input", "hll",
                "perf", "glyph", "window");
        SUPPORTED = Collections.unmodifiableSet(supported);
    }

    private final Set<String> enabled;

    private Rance10DiagnosticOptions(Set<String> enabled) {
        this.enabled = Collections.unmodifiableSet(enabled);
    }

    static Rance10DiagnosticOptions parse(String text) {
        Set<String> selected = new LinkedHashSet<>();
        if (text == null || text.trim().isEmpty()) return new Rance10DiagnosticOptions(selected);
        if (text.length() > 256) throw new IllegalArgumentException("Diagnostic selection exceeds 256 characters");
        for (String raw : text.split(",", -1)) {
            String name = raw.trim().toLowerCase(Locale.ROOT);
            if (!SUPPORTED.contains(name)) throw new IllegalArgumentException("Unsupported diagnostic: " + name);
            selected.add(name);
        }
        return new Rance10DiagnosticOptions(selected);
    }

    boolean has(String name) { return enabled.contains(name); }

    @Override public String toString() {
        StringBuilder text = new StringBuilder();
        for (String name : enabled) {
            if (text.length() != 0) text.append(',');
            text.append(name);
        }
        return text.toString();
    }
}
