package io.github.kichikuou.xsystem4;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/** Copy the old flat SaveData only into an empty default profile, never merge. */
final class LegacySaveImport {
    interface Copier { void copy(File source, File target) throws IOException; }

    static boolean copyIfEmpty(File legacy, File target) throws IOException {
        return copyIfEmpty(legacy, target, LegacySaveImport::copyFile);
    }

    static synchronized boolean copyIfEmpty(File legacy, File target, Copier copier) throws IOException {
        File[] current = target.listFiles();
        if (current == null) throw new IOException("Cannot inspect selected save directory");
        // A valid profile takes precedence even if the unused legacy path is broken.
        if (current.length != 0) return false;
        if (!legacy.exists()) return false;
        if (!legacy.isDirectory()) throw new IOException("Legacy save path is not a directory");
        File sourceRoot = legacy.getCanonicalFile();
        File targetRoot = target.getCanonicalFile();
        // File.toPath/Files require newer Android APIs; keep this API21-safe.
        if (sourceRoot.equals(targetRoot) ||
                targetRoot.getPath().startsWith(sourceRoot.getPath() + File.separator) ||
                sourceRoot.getPath().startsWith(targetRoot.getPath() + File.separator)) {
            throw new IOException("Legacy and target save directories overlap");
        }
        File[] originals = legacy.listFiles();
        if (originals == null) throw new IOException("Cannot inspect legacy save directory");
        // Stage beside the destination so publication is a directory rename.
        File staging = File.createTempFile(".legacy-import-", ".tmp", target.getParentFile());
        if (!staging.delete() || !staging.mkdir()) throw new IOException("Cannot create import staging directory");
        List<File> copies = new ArrayList<>();
        try {
            for (File source : originals) {
                if (!source.isFile()) continue; // Same flat-file scope as the old importer.
                File copy = new File(staging, source.getName());
                copies.add(copy);
                copier.copy(source, copy);
            }
            if (copies.isEmpty()) return false;
            current = target.listFiles();
            if (current == null) throw new IOException("Cannot recheck selected save directory");
            if (current.length != 0) return false; // Another owner created progress: leave it intact.
            if (!target.delete()) throw new IOException("Cannot publish into selected save directory");
            if (!staging.renameTo(target)) {
                target.mkdir(); // Restore the empty profile if publication failed.
                throw new IOException("Cannot publish imported saves");
            }
            return true;
        } finally {
            // Only remove our own staging files; never originals or published saves.
            for (File copy : copies) if (copy.exists()) copy.delete();
            staging.delete();
        }
    }

    private static void copyFile(File source, File target) throws IOException {
        long expected = source.length(), copied = 0;
        if (!target.createNewFile()) throw new IOException("Import target already exists");
        try (FileInputStream input = new FileInputStream(source);
             FileOutputStream output = new FileOutputStream(target)) {
            byte[] buffer = new byte[64 * 1024];
            int count;
            while ((count = input.read(buffer)) != -1) {
                output.write(buffer, 0, count);
                copied += count;
            }
            output.getFD().sync();
        }
        if (copied != expected || source.length() != expected)
            throw new IOException("Legacy save changed while importing");
    }
}
