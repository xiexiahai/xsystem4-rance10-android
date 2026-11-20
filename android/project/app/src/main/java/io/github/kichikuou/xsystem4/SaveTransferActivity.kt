package io.github.kichikuou.xsystem4

import android.app.Activity
import android.app.ActivityManager
import android.app.AlertDialog
import android.content.ActivityNotFoundException
import android.content.Intent
import android.content.SharedPreferences
import android.net.Uri
import android.os.Bundle
import android.util.Log
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.IOException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.UUID

class SaveTransferActivity : Activity() {
    companion object {
        private const val EXPORT = 11
        private const val IMPORT = 12
        private const val PREVIOUS = "previous_save_profile"
        private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
        private var observer: (() -> Unit)? = null
        private var statusText = ""
        var busy = false
            private set
    }

    private val cheats = BuildConfig.RANCE10_CHEATS
    private val profileKey = SaveProfile.activeKey(cheats)
    private val preferences: SharedPreferences
        get() = getSharedPreferences(SaveProfile.PREFERENCES, MODE_PRIVATE)
    private lateinit var exportButton: Button
    private lateinit var importButton: Button
    private lateinit var previousButton: Button
    private lateinit var status: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val padding = (24 * resources.displayMetrics.density).toInt()
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(padding, padding, padding, padding)
            addView(TextView(this@SaveTransferActivity).apply {
                text = if (cheats) "兰斯10 cheat 版" else "兰斯10 普通版"
                textSize = 22f
            })
            addView(TextView(this@SaveTransferActivity).apply {
                text = "请先在游戏内保存并退出。导出的 ZIP 可放在「下载」或你选择的位置。"
                setPadding(0, padding / 2, 0, padding)
            })
            exportButton = Button(this@SaveTransferActivity).apply {
                text = "导出整套存档"
                setOnClickListener { pick(EXPORT) }
            }
            addView(exportButton)
            importButton = Button(this@SaveTransferActivity).apply {
                text = "导入存档备份"
                setOnClickListener { pick(IMPORT) }
            }
            addView(importButton)
            previousButton = Button(this@SaveTransferActivity).apply {
                text = "切换到上一套存档"
                setOnClickListener { confirmPrevious() }
            }
            addView(previousButton)
            status = TextView(this@SaveTransferActivity).apply {
                setPadding(0, padding, 0, 0)
                accessibilityLiveRegion = TextView.ACCESSIBILITY_LIVE_REGION_POLITE
            }
            addView(status)
        }
        setContentView(ScrollView(this).apply { addView(content) })
    }

    override fun onResume() {
        super.onResume()
        observer = ::render
        render()
    }

    override fun onPause() {
        observer = null
        super.onPause()
    }

    private fun render() {
        exportButton.isEnabled = !busy
        importButton.isEnabled = !busy
        previousButton.isEnabled = !busy && previousProfile() != null
        status.text = statusText
    }

    private fun pick(request: Int) {
        if (!checkGameClosed()) return
        val intent = Intent(if (request == EXPORT) Intent.ACTION_CREATE_DOCUMENT else Intent.ACTION_OPEN_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType(if (request == EXPORT) "application/zip" else "*/*")
        if (request == EXPORT) {
            val date = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(Date())
            intent.putExtra(Intent.EXTRA_TITLE, "Rance10${if (cheats) "-cheat" else ""}-${date}.zip")
        }
        try {
            startActivityForResult(intent, request)
        } catch (_: ActivityNotFoundException) {
            update("系统文件选择器不可用。")
        }
    }

    override fun onActivityResult(request: Int, result: Int, data: Intent?) {
        super.onActivityResult(request, result, data)
        val uri = data?.data ?: return
        if (result != RESULT_OK) return
        when (request) {
            EXPORT -> transfer("正在导出存档…") {
                val profile = File(filesDir, activeProfile())
                val output = contentResolver.openOutputStream(uri, "w")
                    ?: throw IOException("Document provider did not return an output stream")
                val count = output.use { SaveBackup.write(profile, it, cheats) }
                "导出完成，共 $count 个存档文件。"
            }
            IMPORT -> AlertDialog.Builder(this)
                .setTitle("导入存档备份？")
                .setMessage("导入后，下次启动会读取这套存档。原来的进度可以在此页切换回来。")
                .setNegativeButton("取消", null)
                .setPositiveButton("导入") { _, _ -> transfer("正在校验并导入存档…") { importBackup(uri) } }
                .show()
        }
    }

    private fun confirmPrevious() {
        if (!checkGameClosed()) return
        AlertDialog.Builder(this)
            .setTitle("切换到上一套存档？")
            .setMessage("下次启动将读取上一套存档。当前这套也会保留。")
            .setNegativeButton("取消", null)
            .setPositiveButton("切换") { _, _ ->
                transfer("正在切换存档…") {
                    val previous = previousProfile() ?: throw IOException("Previous profile is missing")
                    selectProfile(previous)
                    "已切换。重新打开游戏即可读取上一套存档。"
                }
            }
            .show()
    }

    private fun activeProfile(): String {
        val exists = SaveProfile.DirectoryExists { File(filesDir, it).isDirectory }
        val selected = preferences.getString(profileKey, null)
        return if (cheats) SaveProfile.selectCheat(selected, null, exists)
            else SaveProfile.select(selected, null, exists)
    }

    private fun previousProfile(): String? = preferences.getString(PREVIOUS, null)?.takeIf {
        val supported = if (cheats) SaveProfile.supportedCheat(it) else SaveProfile.supported(it)
        supported && it != activeProfile() && File(filesDir, it).isDirectory
    }

    private fun selectProfile(relative: String) {
        if (gameIsRunning()) throw SaveBackup.InvalidBackup("请先在游戏内保存并退出，再操作存档。")
        val active = activeProfile()
        val oldSelection = preferences.getString(profileKey, null)
        val oldPrevious = preferences.getString(PREVIOUS, null)
        val previous = active.takeIf { File(filesDir, it).isDirectory }
        if (!preferences.edit().putString(profileKey, relative).putString(PREVIOUS, previous).commit()) {
            preferences.edit().putString(profileKey, oldSelection).putString(PREVIOUS, oldPrevious).commit()
            throw IOException("Could not write selected save profile")
        }
    }

    private fun importBackup(uri: Uri): String {
        val id = UUID.randomUUID().toString().replace("-", "")
        val relative = SaveProfile.imported(cheats, id)
        val target = File(filesDir, relative)
        val parent = target.parentFile ?: throw IOException("No profile parent")
        if (!parent.isDirectory && !parent.mkdirs()) throw IOException("Could not create profile parent")
        val staging = File(parent, ".incoming-$id")
        if (!staging.mkdir()) throw IOException("Could not create import staging")
        var archive: File? = null
        try {
            val incoming = File.createTempFile("rance10-import-", ".zip", cacheDir)
            archive = incoming
            val input = contentResolver.openInputStream(uri) ?: throw IOException("Could not open backup")
            input.use { SaveBackup.copyArchive(it, incoming) }
            val count = SaveBackup.extract(incoming, staging, cheats)
            if (target.exists() || !staging.renameTo(target)) throw IOException("Could not publish imported profile")
            selectProfile(relative)
            return "已导入 $count 个存档文件。重新打开游戏即可读取。"
        } finally {
            archive?.delete()
            staging.listFiles()?.forEach { it.delete() }
            staging.delete()
        }
    }

    private fun gameIsRunning(): Boolean {
        val manager = getSystemService(ACTIVITY_SERVICE) as ActivityManager
        return manager.runningAppProcesses?.any { it.processName == "$packageName:game" } == true
    }

    private fun checkGameClosed(): Boolean {
        if (busy) return false
        if (!gameIsRunning()) return true
        update("请先在游戏内保存并退出，再操作存档。")
        return false
    }

    private fun transfer(progress: String, operation: () -> String) {
        if (!checkGameClosed()) return
        busy = true
        update(progress)
        scope.launch {
            val result = try {
                withContext(Dispatchers.IO) { operation() }
            } catch (e: SaveBackup.InvalidBackup) {
                e.message ?: "备份文件无效。"
            } catch (e: Exception) {
                Log.w("SaveTransfer", "Save transfer failed", e)
                "操作未完成，请确认备份文件完好、存储空间充足后重试。"
            }
            busy = false
            update(result)
        }
    }

    private fun update(message: String) {
        statusText = message
        observer?.invoke()
    }
}
