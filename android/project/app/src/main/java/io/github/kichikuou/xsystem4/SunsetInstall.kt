package io.github.kichikuou.xsystem4

import android.content.ContentResolver
import android.net.Uri
import android.provider.DocumentsContract
import android.system.Os
import java.io.File
import java.io.IOException
import java.security.MessageDigest
import java.util.Locale

/** Local import of 日不落 0.49 with the November 2 repair patch applied. */
internal class SunsetInstall(private val root: File) {
    companion object {
        const val EXTRA = "RANCE10_SUNSET"
        const val DEFAULT_SUNSET = "default_sunset"
        const val PENDING_PHRASE = "sunset-pending-phrase"
        private const val READY_STAMP = "0.49 patched"

        private data class Asset(val name: String, val size: Long, val sha256: String)

        private val assets = listOf(
            Asset("Rance10.ain", 8983159, "e5c091e44a000687f5477a3586f6cccfc196f2306cfe92927d5e91b58074f972"),
            Asset("Rance10EX.ex", 1843471, "fc51c031503a78e7bb6dffe43d9b89c9f510094f4bb025fc0aaad182031a53c4"),
            Asset("Rance10MODCG.afa", 530142696, "b2d8221bf940a890f57a4ac7bd3acec4140261d6d1eaa1a7bd76e1434af1ca0b"),
            Asset("Rance10MODCG2.afa", 412757784, "c87a6959d238c16573259c90cb2180055d74845d9bde4cdc4f0f551671197c4b"),
            Asset("Rance10ModFlat.afa", 547496656, "061545ea5594d2be77334eb77cc93658cf3ce01f2cefad620106c3443db391f6"),
            Asset("Rance10Pact.afa", 934600, "0af4d314e3506450f5c35e5353a2948e27883bd8dea46eac90c93d2303b3f367"),
            Asset("Rance10Sound.afa", 686730488, "70352b894c3c0ea4319a8a70059cdef997966eff46a76f373cc9fa7848e29b67")
        )
        private val modNames = assets.map { it.name.lowercase(Locale.ROOT) }.toSet()
        private val archives = listOf(SunsetSources.full, SunsetSources.patch)
    }

    private val installed = File(root, "sunset-049")
    private val launchDir = File(root, "sunset-game")

    fun installedRelease(): SunsetSources.Release? {
        val stamp = File(installed, ".complete")
        return SunsetSources.supported.takeIf {
            stamp.length() == READY_STAMP.length.toLong() && stamp.readText() == READY_STAMP &&
                assets.all { File(installed, it.name).length() == it.size }
        }
    }

    fun downloadAndInstall(onProgress: (String, Int?) -> Unit) {
        if (installedRelease() != null) return
        val downloads = File(root, ".sunset-049-downloads")
        if (!downloads.isDirectory && !downloads.mkdir()) throw IOException("无法创建日不落下载目录。")
        val totalDownload = archives.sumOf { it.size }
        val remaining = archives.sumOf { it.size - File(downloads, it.name).length().coerceAtMost(it.size) }
        if (root.usableSpace < remaining + assets.sumOf { it.size })
            throw IOException("剩余空间不足，首次安装日不落需要约 4.2 GB。")
        var alreadyDownloaded = 0L
        var reported = -1
        for (archive in archives) {
            SunsetSources.download(archive, File(downloads, archive.name)) { bytes ->
                val percent = ((alreadyDownloaded + bytes) * 100 / totalDownload).toInt()
                if (percent != reported) {
                    reported = percent
                    onProgress("正在下载 · $percent%", percent)
                }
            }
            alreadyDownloaded += archive.size
        }
        installArchives(downloads, onProgress)
    }

    fun importArchives(resolver: ContentResolver, inputs: List<Uri>, onProgress: (String, Int?) -> Unit) {
        if (installedRelease() != null) return
        if (inputs.size != archives.size) throw IOException("请选择完整包和修复补丁两个压缩包。")
        val incoming = File(root, ".sunset-049-local")
        if (!incoming.deleteRecursively() || !incoming.mkdir()) throw IOException("无法创建本地导入目录。")
        try {
            if (root.usableSpace < archives.sumOf { it.size } + assets.sumOf { it.size })
                throw IOException("剩余空间不足，导入压缩包需要约 4.2 GB。")
            val pending = archives.toMutableList()
            val temporary = File(incoming, "incoming.rar")
            for ((index, uri) in inputs.withIndex()) {
                onProgress("正在读取压缩包 · ${index + 1}/${inputs.size}", null)
                val (size, hash) = copyDocument(resolver, uri, temporary, pending.maxOf { it.size })
                val matching = pending.find { it.size == size && it.sha256 == hash }
                    ?: throw IOException("压缩包与 0.49 完整包或修复补丁不匹配。可解压合并后导入目录。")
                if (!temporary.renameTo(File(incoming, matching.name))) throw IOException("无法暂存压缩包。")
                pending.remove(matching)
            }
            installArchives(incoming, onProgress)
        } finally {
            incoming.deleteRecursively()
        }
    }

    fun importDirectory(resolver: ContentResolver, tree: Uri, onProgress: (String, Int?) -> Unit) {
        if (installedRelease() != null) return
        val treeId = DocumentsContract.getTreeDocumentId(tree)
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, treeId)
        val columns = arrayOf(DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE)
        val files = mutableMapOf<String, Uri>()
        resolver.query(children, columns, null, null, null)?.use { rows ->
            while (rows.moveToNext()) {
                val name = rows.getString(1)?.lowercase(Locale.ROOT) ?: continue
                if (name !in modNames || rows.getString(2) == DocumentsContract.Document.MIME_TYPE_DIR) continue
                val uri = DocumentsContract.buildDocumentUriUsingTree(tree, rows.getString(0))
                if (files.put(name, uri) != null) throw IOException("目录有重复的 MOD 文件名。")
            }
        }
        if (files.keys != modNames) throw IOException("请选择已合并完整包和修复补丁的解压目录。")
        if (root.usableSpace < assets.sumOf { it.size }) throw IOException("剩余空间不足，导入目录需要约 2.2 GB。")
        installAssets(onProgress) { staging ->
            for ((index, asset) in assets.withIndex()) {
                onProgress("正在导入并校验 · ${index + 1}/${assets.size}", null)
                val uri = files.getValue(asset.name.lowercase(Locale.ROOT))
                val (size, hash) = copyDocument(resolver, uri, File(staging, asset.name), asset.size)
                if (size != asset.size || hash != asset.sha256) throw wrongVersion(asset)
            }
        }
    }

    private fun copyDocument(resolver: ContentResolver, uri: Uri, to: File, limit: Long): Pair<Long, String> {
        val digest = MessageDigest.getInstance("SHA-256")
        val input = resolver.openInputStream(uri) ?: throw IOException("无法打开所选文件。")
        var total = 0L
        input.use {
            to.outputStream().use { output ->
                val buffer = ByteArray(1024 * 1024)
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    total += count
                    if (total > limit) throw IOException("所选文件大于受支持的 0.49 文件。")
                    output.write(buffer, 0, count)
                    digest.update(buffer, 0, count)
                }
                output.fd.sync()
            }
        }
        return total to digest.digest().joinToString("") { "%02x".format(it) }
    }

    private fun installArchives(directory: File, onProgress: (String, Int?) -> Unit) {
        installAssets(onProgress) { staging ->
            onProgress("正在解压并应用修复补丁…", null)
            // Extract the patch first so the full archive only fills missing files.
            SunsetArchive.unpack(File(directory, SunsetSources.patch.name), staging)
            SunsetArchive.unpack(File(directory, SunsetSources.full.name), staging)
            onProgress("正在校验安装文件…", null)
            for (asset in assets) {
                val file = File(staging, asset.name)
                if (file.length() != asset.size || SunsetSources.sha256(file) != asset.sha256)
                    throw wrongVersion(asset)
            }
        }
    }

    private fun wrongVersion(asset: Asset) =
        IOException("${asset.name} 与 0.49 修复版不匹配，请检查是否已合并 11 月 2 日补丁。")

    private fun installAssets(onProgress: (String, Int?) -> Unit, populate: (File) -> Unit) {
        val staging = File(root, ".sunset-049-import")
        if (!staging.deleteRecursively() || !staging.mkdir())
            throw IOException("无法创建日不落安装目录。")
        try {
            populate(staging)
            onProgress("正在启用日不落…", null)
            File(staging, ".complete").writeText(READY_STAMP)
            if (!installed.deleteRecursively() || !staging.renameTo(installed))
                throw IOException("无法启用日不落安装文件。")
            File(root, ".sunset-049-downloads").deleteRecursively()
        } finally {
            staging.deleteRecursively()
        }
    }

    fun prepareGame(base: File, apkArchives: Array<String>): Pair<File, Array<String>> {
        if (installedRelease() == null) throw IOException("请先在「启动项」下载安装日不落。")
        val stamp = File(launchDir, ".base")
        if (!stamp.isFile || stamp.readText() != base.name) {
            val staging = File(root, ".sunset-game-build")
            if (!staging.deleteRecursively() || !staging.mkdir())
                throw IOException("无法准备日不落启动目录。")
            try {
                // Keep the bundled AliceStart.ini. It selects the GBK base
                // game's compatibility settings even with the mod's AIN.
                val baseFiles = base.listFiles() ?: throw IOException("无法读取原版游戏目录。")
                for (entry in baseFiles) {
                    if (entry.name.startsWith('.') || entry.name.lowercase(Locale.ROOT) in modNames) continue
                    Os.symlink(entry.absolutePath, File(staging, entry.name).absolutePath)
                }
                for (asset in assets) {
                    Os.symlink(File(installed, asset.name).absolutePath, File(staging, asset.name).absolutePath)
                }
                File(staging, ".base").writeText(base.name)
                // File.deleteRecursively follows directory symlinks, so remove
                // just the directory entries that this method created.
                if (!clearLinks(launchDir) || !staging.renameTo(launchDir))
                    throw IOException("无法启用日不落启动目录。")
            } finally {
                clearLinks(staging)
            }
        }
        val shared = apkArchives.filter { it.lowercase(Locale.ROOT) !in modNames }.toTypedArray()
        return launchDir to shared
    }

    private fun clearLinks(dir: File): Boolean {
        if (!dir.exists()) return true
        val files = dir.listFiles() ?: return false
        return files.all { it.delete() } && dir.delete()
    }
}
