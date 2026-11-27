package io.github.kichikuou.xsystem4;

import java.io.DataInputStream;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.util.Locale;
import java.util.zip.InflaterInputStream;
import java.util.zip.ZipException;

/** The Windows port's resumable quick snapshots and machine settings are separate. */
final class WindowsSaveImport {
    private static final int MAX_SAVE_SIZE = 256 * 1024 * 1024;
    private static final String[] COMMON_FILES = {
        "GlobalSave.asd", "Collection.asd", "AFInfo.asd", "AFCommon.asd"
    };

    static String targetName(String source) {
        for (String name : COMMON_FILES)
            if (name.equalsIgnoreCase(source)) return name;
        String lower = source.toLowerCase(Locale.ROOT);
        if (lower.matches("localsave[0-9]{2,6}\\.asd"))
            return "LocalSave" + lower.substring(9);
        if (lower.matches("thumb[0-9]{4,6}\\.qnt"))
            return "Thumb" + lower.substring(5);
        return null;
    }

    static boolean isSlot(String name) {
        return name.startsWith("LocalSave");
    }

    static void validate(InputStream source, String name) throws IOException {
        try (DataInputStream file = new DataInputStream(source)) {
            if (name.endsWith(".qnt")) {
                if (file.readInt() == 0x514e5400) return;
                throw invalid(name);
            }
            if (file.readInt() != 0x47440101) throw invalid(name);
            int size = Integer.reverseBytes(file.readInt());
            byte[] key = name.equals("AFInfo.asd") || name.equals("AFCommon.asd")
                    ? new byte[] {(byte) 0x81, (byte) 0xa2, (byte) 0xa5, (byte) 0xad,
                                  (byte) 0xa9, 0x60, (byte) 0xc3, (byte) 0xfb, 0}
                    : new byte[] {'r', 'a', 'n', 'c', 'e', '1', '0', 0};
            if (size < key.length + 64 || size > MAX_SAVE_SIZE) throw invalid(name);
            try (DataInputStream data = new DataInputStream(new InflaterInputStream(file))) {
                for (byte value : key)
                    if (data.readByte() != value) throw invalid(name);
                data.readInt(); // Header marker.
                if (Integer.reverseBytes(data.readInt()) != 8) throw invalid(name);
                byte[] buffer = new byte[64 * 1024];
                int remaining = size - key.length - 8;
                while (remaining > 0) {
                    int count = data.read(buffer, 0, Math.min(remaining, buffer.length));
                    if (count < 0) throw invalid(name);
                    remaining -= count;
                }
                if (data.read() != -1) throw invalid(name);
            }
        } catch (EOFException | ZipException malformed) {
            throw invalid(name);
        }
    }

    private static SaveBackup.InvalidBackup invalid(String name) {
        return new SaveBackup.InvalidBackup(name + " 不是有效的 Windows《兰斯10》存档文件，或文件已损坏。");
    }
}
