package io.github.kichikuou.xsystem4;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import java.util.zip.CRC32;
import java.util.zip.CheckedInputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

/** Portable backup of one flat Rance10 save profile. */
final class SaveBackup {
    private static final String HEADER = "rance10-backup.txt";
    private static final int MAX_FILES = 2048;
    private static final long MAX_BYTES = 2L << 30;
    private static final long MAX_ZIP_BYTES = MAX_BYTES + (4L << 20);

    static final class InvalidBackup extends IOException {
        InvalidBackup(String message) {
            super(message);
        }
    }

    static int write(File profile, OutputStream destination, boolean cheats) throws IOException {
        File[] files = profile.getCanonicalFile().listFiles();
        if (files == null || files.length == 0) throw new InvalidBackup("当前还没有可导出的存档。");
        if (files.length > MAX_FILES) throw new InvalidBackup("存档文件太多，无法导出。");
        Arrays.sort(files, (left, right) -> left.getName().compareTo(right.getName()));
        Set<String> names = new HashSet<>();
        long bytes = 0;
        boolean hasSave = false;
        for (File file : files) {
            checkName(file.getName(), names);
            if (!file.isFile() || !file.equals(file.getCanonicalFile()))
                throw new InvalidBackup("存档目录里有无法导出的文件。");
            bytes = checkSize(bytes, file.length());
            hasSave |= isSave(file.getName());
        }
        if (!hasSave) throw new InvalidBackup("当前还没有可导出的存档。");

        try (ZipOutputStream zip = new ZipOutputStream(destination)) {
            for (File file : files) {
                long size = file.length();
                long modified = file.lastModified();
                zip.putNextEntry(new ZipEntry(file.getName()));
                try (InputStream input = new BufferedInputStream(new FileInputStream(file))) {
                    if (copy(input, zip, size) != size || file.lastModified() != modified)
                        throw new IOException("档案在导出时发生变化。");
                }
                zip.closeEntry();
            }
            zip.putNextEntry(new ZipEntry(HEADER));
            zip.write(header(cheats, files.length));
            zip.closeEntry();
        }
        return files.length;
    }

    static void copyArchive(InputStream input, File destination) throws IOException {
        try (OutputStream output = new FileOutputStream(destination)) {
            copy(input, output, MAX_ZIP_BYTES);
        }
    }

    static int extract(File archive, File staging, boolean cheats) throws IOException {
        String[] contents = staging.list();
        if (contents == null || contents.length != 0) throw new IOException("导入临时目录不可用。");
        try (ZipFile zip = new ZipFile(archive)) {
            int count = zip.size() - 1;
            if (count < 1 || count > MAX_FILES) throw new InvalidBackup("备份文件数量不正确。");
            ZipEntry metadata = zip.getEntry(HEADER);
            if (metadata == null) throw new InvalidBackup("请选择《兰斯10》导出的存档备份 ZIP。");
            byte[] expectedHeader = header(cheats, count);
            try (InputStream input = zip.getInputStream(metadata)) {
                for (byte expected : expectedHeader) {
                    if (input.read() != (expected & 0xff))
                        throw new InvalidBackup("备份版本或游戏版本不匹配，请使用同一版导出的备份。");
                }
                if (input.read() != -1) throw new InvalidBackup("备份标记不正确。");
            }
            Set<String> names = new HashSet<>();
            long bytes = 0;
            boolean hasSave = false;
            for (Enumeration<? extends ZipEntry> entries = zip.entries(); entries.hasMoreElements();) {
                ZipEntry entry = entries.nextElement();
                String name = entry.getName();
                if (HEADER.equals(name)) continue;
                checkName(name, names);
                bytes = checkSize(bytes, entry.getSize());
                hasSave |= isSave(name);
            }
            if (!hasSave || names.size() != count) throw new InvalidBackup("备份的存档文件不完整。");

            for (Enumeration<? extends ZipEntry> entries = zip.entries(); entries.hasMoreElements();) {
                ZipEntry entry = entries.nextElement();
                if (HEADER.equals(entry.getName())) continue;
                File target = new File(staging, entry.getName());
                if (!target.createNewFile()) throw new IOException("无法创建导入存档。");
                CRC32 crc = new CRC32();
                try (InputStream input = new CheckedInputStream(zip.getInputStream(entry), crc);
                     FileOutputStream output = new FileOutputStream(target)) {
                    if (copy(input, output, entry.getSize()) != entry.getSize() ||
                            crc.getValue() != entry.getCrc())
                        throw new InvalidBackup("备份的存档文件已损坏。");
                    output.getFD().sync();
                }
            }
            return count;
        }
    }

    private static void checkName(String name, Set<String> names) throws InvalidBackup {
        if (!name.matches("[A-Za-z0-9_][A-Za-z0-9_.-]{0,179}") ||
                HEADER.equalsIgnoreCase(name) || !names.add(name.toLowerCase(Locale.ROOT)))
            throw new InvalidBackup("备份内的文件名无效或重复。");
    }

    private static boolean isSave(String name) {
        return name.toLowerCase(Locale.ROOT).endsWith(".asd");
    }

    private static byte[] header(boolean cheats, int count) {
        return ("RANCE10-SAVE-1\n" + (cheats ? "cheat" : "normal") + "\n" + count + "\n")
                .getBytes(StandardCharsets.US_ASCII);
    }

    private static long checkSize(long total, long next) throws InvalidBackup {
        if (next < 0 || next > MAX_BYTES - total)
            throw new InvalidBackup("存档大小超出 2 GB 的备份上限。");
        return total + next;
    }

    private static long copy(InputStream input, OutputStream output, long limit) throws IOException {
        byte[] buffer = new byte[64 * 1024];
        long bytes = 0;
        for (int size; (size = input.read(buffer)) != -1;) {
            if (size > limit - bytes) throw new InvalidBackup("存档长度不符或超过备份上限。");
            output.write(buffer, 0, size);
            bytes += size;
        }
        return bytes;
    }
}
