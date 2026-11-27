package io.github.kichikuou.xsystem4

import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.net.URL
import java.security.MessageDigest
import javax.net.ssl.HttpsURLConnection

internal object SunsetSources {
    data class Release(val version: String, val date: String)
    data class Archive(val name: String, val size: Long, val sha256: String)
    private const val MIRROR = "https://alicehub.000.moe/rance10/files/mods/"
    private const val RANGE_SIZE = 32L * 1024 * 1024
    val supported = Release("0.49", "2026-11-02")
    val full = Archive("0.49-full.rar", 1914239156, "37e2ccc8b5beead87d2202d31cb10f977e61a89d48f38a86b40276a8256d1f63")
    val patch = Archive("0.49-patch.rar", 10740125, "c62edbce3b4b76529321e4f4d9485de11ff7f022b43274860205beb490854cde")

    private val releaseNumber = Regex("""^[vV]?\s*(\d{1,4}(?:\.\d{1,4}){1,4}(?:\.?[a-zA-Z][a-zA-Z0-9]{0,5})?)""")
    private val completeRange = Regex("""bytes 0-(\d+)/(\d+)""")

    fun latestMirror(): Release {
        val entries = JSONObject(readMirrorCatalog()).getJSONArray("releases")
        return (0 until entries.length()).mapNotNull { index ->
            val entry = entries.optJSONObject(index) ?: return@mapNotNull null
            if (entry.optString("group") != "日不落MOD") return@mapNotNull null
            val version = releaseNumber.find(entry.optString("version"))?.groupValues?.get(1)
                ?: return@mapNotNull null
            val date = entry.optString("uploadedAt").take(10)
            if (date.length != 10) return@mapNotNull null
            Release(version, date)
        }.maxByOrNull { it.date } ?: throw IOException("镜像目录没有日不落版本")
    }

    private fun readMirrorCatalog(): String = https(MIRROR + "index.json") { connection ->
        val limit = 64 * 1024
        connection.setRequestProperty("User-Agent", "Mozilla/5.0")
        connection.setRequestProperty("Accept", "application/json")
        connection.setRequestProperty("Range", "bytes=0-${limit - 1}")
        when (connection.responseCode) {
            200 -> Unit
            206 -> {
                val range = completeRange.matchEntire(connection.getHeaderField("Content-Range").orEmpty())
                val end = range?.groupValues?.get(1)?.toLongOrNull()
                val size = range?.groupValues?.get(2)?.toLongOrNull()
                if (end == null || size == null || size > limit || end != size - 1)
                    throw IOException("收到不完整的资源目录")
            }
            else -> throw IOException("无法读取资源目录")
        }
        connection.inputStream.bufferedReader(Charsets.UTF_8).use { reader ->
            val result = StringBuilder()
            val buffer = CharArray(4096)
            while (true) {
                val read = reader.read(buffer)
                if (read < 0) break
                if (result.length + read > limit) throw IOException("资源目录超出读取上限")
                result.append(buffer, 0, read)
            }
            result.toString()
        }
    }

    fun download(archive: Archive, destination: File, onBytes: (Long) -> Unit) {
        if (destination.length() > archive.size && !destination.delete())
            throw IOException("无法清理损坏的下载文件")
        RandomAccessFile(destination, "rw").use { output ->
            var copied = output.length()
            output.seek(copied)
            onBytes(copied)
            val buffer = ByteArray(256 * 1024)
            while (copied < archive.size) {
                val end = minOf(copied + RANGE_SIZE, archive.size) - 1
                try {
                    https(MIRROR + archive.name) { connection ->
                        connection.setRequestProperty("Range", "bytes=$copied-$end")
                        val expected = "bytes $copied-$end/${archive.size}"
                        val response = connection.responseCode
                        if (!(response == 206 && connection.getHeaderField("Content-Range") == expected) &&
                            !(response == 200 && copied == 0L && end == archive.size - 1))
                            throw IOException("镜像暂时无法继续下载")
                        connection.inputStream.use { input ->
                            while (copied <= end) {
                                val count = input.read(buffer, 0, minOf(buffer.size.toLong(), end - copied + 1).toInt())
                                if (count < 0) throw IOException("镜像数据提前结束")
                                output.write(buffer, 0, count)
                                copied += count
                                onBytes(copied)
                            }
                        }
                    }
                } catch (error: IOException) {
                    throw IOException("下载中断，再次点「下载文件」可继续。", error)
                }
            }
            output.fd.sync()
        }
        if (destination.length() != archive.size || sha256(destination) != archive.sha256) {
            destination.delete()
            throw IOException("下载文件校验失败，请重新下载。")
        }
    }

    fun sha256(file: File): String {
        val hash = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { input ->
            val buffer = ByteArray(1024 * 1024)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                hash.update(buffer, 0, count)
            }
        }
        return hash.digest().joinToString("") { "%02x".format(it) }
    }

    private inline fun <T> https(address: String, use: (HttpsURLConnection) -> T): T {
        val connection = URL(address).openConnection() as HttpsURLConnection
        try {
            connection.connectTimeout = 10_000
            connection.readTimeout = 15_000
            connection.instanceFollowRedirects = false
            connection.setRequestProperty("Accept-Encoding", "identity")
            return use(connection)
        } finally {
            connection.disconnect()
        }
    }
}

internal object SunsetArchive {
    fun unpack(archive: File, directory: File) {
        try {
            System.loadLibrary("sunsetarchive")
        } catch (error: UnsatisfiedLinkError) {
            throw IOException("当前应用缺少日不落解压组件。", error)
        }
        if (!extract(archive.absolutePath, directory.absolutePath))
            throw IOException("日不落压缩包解压失败。")
    }

    private external fun extract(archive: String, directory: String): Boolean
}
