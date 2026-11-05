package io.github.kichikuou.xsystem4

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.security.MessageDigest

internal data class BundledFile(val path: String, val size: Long, val sha256: String, val inApk: Boolean = false) {
    val extractedSize: Long get() = if (inApk) 0 else size
    val marker: String get() = "APK-AFA\n$sha256\n"
}
internal data class BundledManifest(val contentId: String, val files: List<BundledFile>) {
    val totalBytes: Long get() = files.sumOf { it.extractedSize }
    val payloadBytes: Long get() = files.sumOf { it.size }

    init {
        require(contentId.matches(Regex("[0-9a-f]{64}")))
        require(files.isNotEmpty() && files.map { it.path }.toSet().size == files.size)
        files.forEach {
            require(it.path.isNotEmpty() && !it.path.startsWith('/') && '\\' !in it.path)
            require(it.path.split('/').all { p -> p.isNotEmpty() && p != "." && p != ".." && ':' !in p })
            require(it.path != ".complete" && !it.path.endsWith(".part"))
            require(it.size >= 0 && it.sha256.matches(Regex("[0-9a-f]{64}")))
            require(!it.inApk || it.path.matches(Regex("[A-Za-z0-9_-]+\\.afa")))
        }
        files.fold(0L) { n, f -> require(n <= Long.MAX_VALUE - f.size - 64L * 1024 * 1024); n + f.size }
    }
}

internal class BundledSpaceException(val required: Long, val available: Long) : IOException()

/** Installs only under [root]. Game saves live elsewhere and are never touched. */
internal class BundledGameStore(root: File) {
    // Android exposes the same private data under /data/user/0 and /data/data.
    // Keep enumeration and activation paths in one form before comparing them.
    private val root = root.canonicalFile
    fun prepare(manifest: BundledManifest, open: (String) -> InputStream,
                progress: (Long, Long) -> Unit = { _, _ -> },
                available: () -> Long = { root.usableSpace }): File {
        check(root.isDirectory || root.mkdirs()) { "Cannot create game directory" }
        val current = child(root, manifest.contentId)
        if (complete(current, manifest)) return current
        val staging = child(root, ".install-${manifest.contentId}")
        check(staging.isDirectory || staging.mkdirs()) { "Cannot create staging directory" }
        var completed = 0L
        // Keep verified files after interruption, so a retry needs only the
        // remaining space. Never accept an unfinished file merely by its size.
        val pending = manifest.files.filter { entry ->
            val file = child(staging, entry.path)
            child(staging, entry.path + ".part").let {
                if (it.exists() && !it.delete()) throw IOException("Cannot discard interrupted file")
            }
            val valid = if (entry.inApk) validMarker(file, entry)
                        else file.isFile && file.length() == entry.size && sha256(file) == entry.sha256
            if (valid) completed += entry.extractedSize
            else if (file.exists() && !file.delete()) throw IOException("Cannot discard invalid file")
            !valid
        }
        val needed = manifest.totalBytes - completed + 64L * 1024 * 1024
        val free = available()
        if (free < needed) throw BundledSpaceException(needed, free)
        progress(completed, manifest.totalBytes)
        val buffer = ByteArray(1024 * 1024)
        for (entry in pending) {
            val dest = child(staging, entry.path)
            check(dest.parentFile!!.isDirectory || dest.parentFile!!.mkdirs())
            val temp = child(staging, entry.path + ".part")
            if (entry.inApk) {
                FileOutputStream(temp).use { it.write(entry.marker.toByteArray(Charsets.US_ASCII)); it.fd.sync() }
                if (!temp.renameTo(dest)) throw IOException("Cannot finish archive marker")
                continue
            }
            val digest = MessageDigest.getInstance("SHA-256")
            var copied = 0L
            try {
                open(entry.path).use { input ->
                    FileOutputStream(temp).use { output ->
                        while (true) {
                            val n = input.read(buffer)
                            if (n < 0) break
                            if (n == 0) continue
                            copied += n
                            if (copied > entry.size) throw IOException("Oversized bundled file")
                            digest.update(buffer, 0, n)
                            output.write(buffer, 0, n)
                            progress(completed + copied, manifest.totalBytes)
                        }
                        if (copied != entry.size || hex(digest.digest()) != entry.sha256)
                            throw IOException("Bundled file checksum mismatch: ${entry.path}")
                        output.fd.sync()
                    }
                }
                if (!temp.renameTo(dest)) throw IOException("Cannot finish bundled file")
            } finally {
                temp.delete()
            }
            completed += copied
        }
        FileOutputStream(child(staging, ".complete")).use {
            it.write(manifest.contentId.toByteArray(Charsets.US_ASCII))
            it.fd.sync()
        }
        if (current.exists()) remove(current)
        if (!staging.renameTo(current)) throw IOException("Cannot activate bundled game")
        // Activated first; obsolete resources may now be reclaimed. Save data
        // is not inside root. Names outside our version scheme are untouched.
        root.listFiles()?.filter { it.canonicalFile != current &&
            it.name.matches(Regex("(?:\\.install-)?[0-9a-f]{64}")) }?.forEach { remove(it) }
        return current
    }

    private fun complete(dir: File, manifest: BundledManifest): Boolean =
        child(dir, ".complete").let { it.isFile && it.readText() == manifest.contentId } &&
            manifest.files.all { entry -> child(dir, entry.path).let { f ->
                if (entry.inApk) validMarker(f, entry) else f.isFile && f.length() == entry.size
            } }

    private fun validMarker(file: File, entry: BundledFile): Boolean =
        file.isFile && file.length() == entry.marker.length.toLong() && file.readText() == entry.marker

    private fun child(parent: File, name: String): File {
        val file = File(parent, name).canonicalFile
        check(file.path.startsWith(root.canonicalPath + File.separator)) { "Outside bundled directory" }
        return file
    }

    private fun remove(dir: File) {
        check(dir.canonicalPath.startsWith(root.canonicalPath + File.separator))
        check(dir.deleteRecursively()) { "Cannot remove obsolete resources" }
    }

    private fun sha256(file: File): String {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { stream ->
            val buffer = ByteArray(1024 * 1024)
            while (true) {
                val n = stream.read(buffer)
                if (n < 0) break
                if (n > 0) digest.update(buffer, 0, n)
            }
        }
        return hex(digest.digest())
    }

    private fun hex(bytes: ByteArray): String = bytes.joinToString("") { "%02x".format(it) }
}
