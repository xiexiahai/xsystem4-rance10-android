package io.github.kichikuou.xsystem4

import android.app.Activity
import android.app.ActivityManager
import android.app.AlertDialog
import android.content.ActivityNotFoundException
import android.content.Intent
import android.content.SharedPreferences
import android.net.Uri
import android.os.Bundle
import android.provider.DocumentsContract
import android.util.Log
import android.widget.Button
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
        private const val IMPORT_WINDOWS = 13
        private const val PREVIOUS = "previous_save_profile"
        private const val TARGET_SUNSET = "target_sunset"
        private const val TARGET_CHEATS = "target_cheats"
        private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
        private var observer: (() -> Unit)? = null
        private var statusText = ""
        private var statusSunset = false
        private var statusCheats = false
        var busy = false
            private set
    }

    private var cheats = false
    private var sunset = false
    private val profileKey get() = SaveProfile.activeKey(cheats, sunset)
    private val previousKey get() = buildString {
        append(PREVIOUS)
        if (Rance10PlayMode.selectable && !cheats) append("_normal")
        if (sunset) append("_sunset_049")
    }
    private val targetName get() = if (sunset) "日不落 0.49" else "原版"
    private val preferences: SharedPreferences
        get() = getSharedPreferences(SaveProfile.PREFERENCES, MODE_PRIVATE)
    private lateinit var exportButton: Button
    private lateinit var importButton: Button
    private lateinit var windowsButton: Button
    private lateinit var previousButton: Button
    private lateinit var status: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val defaultCheats = Rance10PlayMode.useCheats(preferences)
        cheats = savedInstanceState?.getBoolean(TARGET_CHEATS, defaultCheats) ?: defaultCheats
        sunset = BuildConfig.RANCE10_LAUNCH_OPTIONS && (savedInstanceState?.getBoolean(TARGET_SUNSET)
            ?: preferences.getBoolean(SunsetInstall.DEFAULT_SUNSET, false))
        val ui = Rance10ManagementUi(this)
        val edition = when {
            Rance10PlayMode.selectable && cheats -> "cheat 模式"
            Rance10PlayMode.selectable -> "普通模式"
            cheats -> "兰斯10 cheat"
            else -> "兰斯10"
        }
        val page = ui.page(getString(R.string.rance10_save_transfer), "$edition · ${targetName}档案")
        val backups = ui.card()
        ui.add(backups, ui.text("导入和导出跟随「启动项」里的默认选择。本页操作的是${targetName}档案。请先在游戏中保存并退出。",
            14, Rance10ManagementUi.MUTED), 0)
        exportButton = ui.tool("导出整套存档", "保存为 ZIP 备份") { pick(EXPORT) }
        importButton = ui.tool("导入存档备份", "选择之前导出的 ZIP") { pick(IMPORT) }
        windowsButton = ui.tool("导入 Windows 原版存档", "选择 Windows 的 SaveData 文件夹") {
            pick(IMPORT_WINDOWS)
        }
        ui.add(backups, exportButton, 12)
        ui.add(backups, importButton)
        if (!sunset) ui.add(backups, windowsButton)
        ui.add(page, backups, 20)

        val selection = ui.card()
        previousButton = ui.button("切换到上一套存档") { confirmPrevious() }
        ui.add(selection, previousButton, 0)
        ui.add(page, selection, 12)
        status = ui.status()
        ui.add(page, status, 12)
    }

    override fun onSaveInstanceState(outState: Bundle) {
        outState.putBoolean(TARGET_SUNSET, sunset)
        outState.putBoolean(TARGET_CHEATS, cheats)
        super.onSaveInstanceState(outState)
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
        windowsButton.isEnabled = !busy
        previousButton.isEnabled = !busy && previousProfile() != null
        status.text = if (statusSunset == sunset && statusCheats == cheats) statusText else ""
    }

    private fun pick(request: Int) {
        if ((request == IMPORT_WINDOWS && sunset) || !checkGameClosed()) return
        val intent = when (request) {
            IMPORT_WINDOWS -> Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            EXPORT -> Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("application/zip")
            else -> Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
        }
        if (request == EXPORT) {
            val date = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(Date())
            val edition = if (cheats) "-cheat" else ""
            val mode = if (sunset) "-sunset-049" else ""
            intent.putExtra(Intent.EXTRA_TITLE, "Rance10$edition$mode-$date.zip")
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
        if (result != RESULT_OK || (request == IMPORT_WINDOWS && sunset)) return
        when (request) {
            EXPORT -> transfer("正在导出存档…") {
                val profile = File(filesDir, activeProfile())
                val output = contentResolver.openOutputStream(uri, "w")
                    ?: throw IOException("Document provider did not return an output stream")
                val count = output.use { SaveBackup.write(profile, it, cheats, sunset) }
                "导出完成，共 $count 个存档文件。"
            }
            IMPORT -> AlertDialog.Builder(this)
                .setTitle("导入存档备份？")
                .setMessage("导入后，下次启动${targetName}时会读取这套存档。原来的进度可以在此页切换回来。")
                .setNegativeButton("取消", null)
                .setPositiveButton("导入") { _, _ -> transfer("正在校验并导入存档…") { importBackup(uri) } }
                .show()
            IMPORT_WINDOWS -> AlertDialog.Builder(this)
                .setTitle("导入 Windows 原版存档？")
                .setMessage("请选择存有 LocalSave 文件的 SaveData 文件夹。导入会启用一套独立存档。")
                .setNegativeButton("取消", null)
                .setPositiveButton("导入") { _, _ -> transfer("正在校验 Windows 存档…") { importWindows(uri) } }
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
        return SaveProfile.selectForGame(selected, cheats, sunset, exists)
    }

    private fun previousProfile(): String? = preferences.getString(previousKey, null)?.takeIf {
        SaveProfile.supportedForGame(it, cheats, sunset) && it != activeProfile() && File(filesDir, it).isDirectory
    }

    private fun selectProfile(relative: String) {
        if (gameIsRunning()) throw SaveBackup.InvalidBackup("请先在游戏内保存并退出，再操作存档。")
        val active = activeProfile()
        val oldSelection = preferences.getString(profileKey, null)
        val oldPrevious = preferences.getString(previousKey, null)
        val previous = active.takeIf { File(filesDir, it).isDirectory }
        if (!preferences.edit().putString(profileKey, relative).putString(previousKey, previous).commit()) {
            preferences.edit().putString(profileKey, oldSelection).putString(previousKey, oldPrevious).commit()
            throw IOException("Could not write selected save profile")
        }
    }

    private fun importBackup(uri: Uri): String {
        val incoming = File.createTempFile("rance10-import-", ".zip", cacheDir)
        try {
            val input = contentResolver.openInputStream(uri) ?: throw IOException("Could not open backup")
            input.use { SaveBackup.copyArchive(it, incoming) }
            return importProfile { SaveBackup.extract(incoming, it, cheats, sunset) }
        } finally {
            incoming.delete()
        }
    }

    private fun importWindows(tree: Uri): String = importProfile { staging ->
        val id = DocumentsContract.getTreeDocumentId(tree)
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id)
        val columns = arrayOf(DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE)
        val files = linkedMapOf<String, Uri>()
        contentResolver.query(children, columns, null, null, null)?.use { rows ->
            while (rows.moveToNext()) {
                if (rows.getString(2) == DocumentsContract.Document.MIME_TYPE_DIR) continue
                val name = rows.getString(1)?.let(WindowsSaveImport::targetName) ?: continue
                val file = DocumentsContract.buildDocumentUriUsingTree(tree, rows.getString(0))
                if (files.put(name, file) != null || files.size > SaveBackup.MAX_FILES)
                    throw SaveBackup.InvalidBackup("存档文件名重复或文件数量超出上限。")
            }
        } ?: throw IOException("Could not read Windows save directory")
        if (files.keys.none(WindowsSaveImport::isSlot))
            throw SaveBackup.InvalidBackup("请选中含 LocalSave*.asd 的 Windows SaveData 文件夹。")
        var copied = 0L
        for ((name, uri) in files) {
            val target = File(staging, name)
            val source = contentResolver.openInputStream(uri) ?: throw IOException("Could not open $name")
            copied = source.use { SaveBackup.copySave(it, target, copied) }
            WindowsSaveImport.validate(target.inputStream(), name)
        }
        files.size
    }

    private fun importProfile(extract: (File) -> Int): String {
        val id = UUID.randomUUID().toString().replace("-", "")
        val relative = SaveProfile.imported(cheats, sunset, id)
        val target = File(filesDir, relative)
        val parent = target.parentFile ?: throw IOException("No profile parent")
        if (!parent.isDirectory && !parent.mkdirs()) throw IOException("Could not create profile parent")
        val staging = File(parent, ".incoming-$id")
        if (!staging.mkdir()) throw IOException("Could not create import staging")
        try {
            val count = extract(staging)
            if (target.exists() || !staging.renameTo(target)) throw IOException("Could not publish imported profile")
            selectProfile(relative)
            return "已导入 $count 个存档文件。重新打开游戏即可读取。"
        } finally {
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
        statusSunset = sunset
        statusCheats = cheats
        statusText = message
        observer?.invoke()
    }
}
