package io.github.kichikuou.xsystem4

import android.app.Activity
import android.app.AlertDialog
import android.content.ActivityNotFoundException
import android.content.Intent
import android.content.res.ColorStateList
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.system.Os
import android.text.InputFilter
import android.text.InputType
import android.view.View
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.RadioButton
import android.widget.TextView
import android.widget.Toast
import java.io.File
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class Rance10LaunchOptionsActivity : Activity() {
    private lateinit var install: SunsetInstall
    private lateinit var original: RadioButton
    private lateinit var sunset: RadioButton
    private lateinit var normalMode: RadioButton
    private lateinit var cheatMode: RadioButton
    private lateinit var downloadButton: Button
    private lateinit var importButton: Button
    private lateinit var modTitle: TextView
    private lateinit var modState: TextView
    private lateinit var status: TextView
    private lateinit var progress: ProgressBar
    private lateinit var phrasePanel: LinearLayout
    private lateinit var phraseState: TextView
    private val supportsSunset = BuildConfig.RANCE10_LAUNCH_OPTIONS
    private val preferences by lazy { getSharedPreferences(SaveProfile.PREFERENCES, MODE_PRIVATE) }
    private val pendingPhrase by lazy { File(noBackupFilesDir, SunsetInstall.PENDING_PHRASE) }
    private var currentCheck: Job? = null
    private var mirrorStatus = "正在检查 AliceHub 镜像…"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        registerRance10Shortcuts()
        install = SunsetInstall(noBackupFilesDir)
        val ui = Rance10ManagementUi(this)
        val edition = applicationInfo.loadLabel(packageManager).toString()
        val page = ui.page("启动与管理", edition)
        page.isFocusableInTouchMode = true
        page.requestFocus()

        if (Rance10PlayMode.selectable) {
            val modes = ui.card()
            ui.add(modes, ui.text("默认游戏模式", 18, bold = true), 0)
            normalMode = modeOption(ui, "普通模式", cheats = false)
            cheatMode = modeOption(ui, "cheat 模式", cheats = true)
            ui.add(modes, ui.row(normalMode, cheatMode), 4)
            ui.add(modes, ui.text("下次启动生效，两种模式各用自己的存档。", 13, Rance10ManagementUi.MUTED), 4)
            ui.add(page, modes, 20)
        }

        val baseCard = ui.card()
        ui.add(baseCard, ui.text("原版", 21, bold = true), 0)
        ui.add(baseCard, ui.text("使用当前原版档案", 14, Rance10ManagementUi.MUTED), 4)
        original = defaultOption(ui, "原版", mod = false)
        ui.add(baseCard, original, 4)

        val modCard = ui.card()
        modTitle = ui.text("日不落", 21, bold = true)
        ui.add(modCard, modTitle, 0)
        modState = ui.text("", 14, Rance10ManagementUi.MUTED)
        ui.add(modCard, modState, 4)
        sunset = defaultOption(ui, "日不落", mod = true)
        ui.add(modCard, sunset, 4)
        downloadButton = ui.button("下载文件", primary = true) {
            val installer = install
            startInstall("准备下载…", installer::downloadAndInstall)
        }
        ui.add(modCard, downloadButton, 12)
        importButton = ui.button("本地导入") { chooseImport() }
        ui.add(modCard, importButton)
        progress = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { max = 100 }
        ui.add(modCard, progress, 10)
        status = ui.status()
        ui.add(modCard, status, 6)
        val wide = resources.configuration.screenWidthDp >= 600 && resources.configuration.fontScale <= 1.2f
        if (wide && supportsSunset) {
            ui.add(page, ui.row(baseCard, modCard), 20)
        } else {
            ui.add(page, baseCard, 20)
            if (supportsSunset) ui.add(page, modCard, 12)
        }

        phrasePanel = ui.card()
        val phraseTitle = ui.text("神秘口令", 18, bold = true)
        val phraseInput = EditText(this).apply {
            id = View.generateViewId()
            hint = "输入解锁主播模式的口令"
            setSingleLine(true)
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            imeOptions = EditorInfo.IME_ACTION_DONE
            filters = arrayOf(InputFilter.LengthFilter(256))
            isSaveEnabled = false
            textSize = 15f
            minimumHeight = ui.dp(48)
            setTextColor(Rance10ManagementUi.INK)
            setHintTextColor(Rance10ManagementUi.MUTED)
            setPadding(ui.dp(12), 0, ui.dp(12), 0)
            background = ui.controlBackground(false)
            setOnEditorActionListener { _, action, _ ->
                if (action != EditorInfo.IME_ACTION_DONE) false
                else {
                    savePhrase(this)
                    true
                }
            }
        }
        phraseTitle.labelFor = phraseInput.id
        phraseState = ui.status()
        ui.add(phrasePanel, phraseTitle, 0)
        ui.add(phrasePanel, phraseInput, 8)
        ui.add(phrasePanel, ui.button("下次启动时提交") { savePhrase(phraseInput) })
        ui.add(phrasePanel, phraseState)
        if (supportsSunset) ui.add(page, phrasePanel, 12)

        val tools = ui.card()
        ui.add(tools, ui.text("工具", 18, bold = true), 0)
        val saves = ui.tool(getString(R.string.rance10_save_transfer), "备份、恢复和 Windows 存档") {
            startActivity(Intent(this, SaveTransferActivity::class.java))
        }
        val controller = ui.tool(getString(R.string.rance10_gamepad_mapping), "调整实体键与 Ctrl 快进") {
            startActivity(Intent(this, GamepadMappingActivity::class.java))
        }
        if (wide) {
            ui.add(tools, ui.row(saves, controller), 12)
        } else {
            ui.add(tools, saves, 12)
            ui.add(tools, controller)
        }
        ui.add(page, tools, 12)
        render()
        checkForUpdates()
    }

    override fun onStart() {
        super.onStart()
        observer = ::render
        render()
    }

    override fun onStop() {
        observer = null
        super.onStop()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        checkForUpdates()
    }

    override fun onDestroy() {
        currentCheck?.cancel()
        super.onDestroy()
    }

    private fun defaultOption(ui: Rance10ManagementUi, name: String, mod: Boolean) =
        launchOption(ui, "默认启动", "默认启动$name", SunsetInstall.DEFAULT_SUNSET, mod)

    private fun modeOption(ui: Rance10ManagementUi, label: String, cheats: Boolean) =
        launchOption(ui, label, label, Rance10PlayMode.DEFAULT_CHEAT, cheats)

    private fun launchOption(ui: Rance10ManagementUi, label: String, description: String,
                             key: String, selected: Boolean) =
        RadioButton(this).apply {
            text = label
            contentDescription = description
            minimumHeight = ui.dp(48)
            setTextColor(Rance10ManagementUi.INK)
            buttonTintList = ColorStateList.valueOf(Rance10ManagementUi.ACCENT)
            setOnClickListener {
                if (!preferences.edit().putBoolean(key, selected).commit())
                    Toast.makeText(context, "保存默认启动项失败。", Toast.LENGTH_SHORT).show()
                render()
            }
        }

    private fun checkForUpdates() {
        if (!supportsSunset) return
        currentCheck?.cancel()
        mirrorStatus = "正在检查 AliceHub 镜像…"
        render()
        currentCheck = scope.launch {
            mirrorStatus = try {
                val release = withContext(Dispatchers.IO) { SunsetSources.latestMirror() }
                "目前镜像下载版本 ${release.version}"
            } catch (cancelled: kotlinx.coroutines.CancellationException) {
                throw cancelled
            } catch (_: Exception) {
                "暂时无法读取 AliceHub 镜像目录"
            }
            render()
        }
    }

    private fun render() {
        val installed = install.installedRelease()
        if (Rance10PlayMode.selectable) {
            val cheats = Rance10PlayMode.useCheats(preferences)
            normalMode.isChecked = !cheats
            cheatMode.isChecked = cheats
        }
        val modDefault = preferences.getBoolean(SunsetInstall.DEFAULT_SUNSET, false)
        original.isChecked = !modDefault
        sunset.isChecked = modDefault
        sunset.visibility = if (installed != null) View.VISIBLE else View.GONE
        phrasePanel.visibility = sunset.visibility
        renderPhraseState()
        downloadButton.visibility = if (installed == null) View.VISIBLE else View.GONE
        downloadButton.isEnabled = !busy
        importButton.visibility = downloadButton.visibility
        importButton.isEnabled = !busy
        modTitle.text = installed?.let { "日不落 ${it.version}" } ?: "日不落"
        progress.visibility = if (busy) View.VISIBLE else View.GONE
        progress.isIndeterminate = transferPercent == null
        transferPercent?.let { progress.progress = it }
        modState.text = if (busy) transferMessage else mirrorStatus
        status.text = resultMessage
        status.visibility = if (resultMessage.isEmpty()) View.GONE else View.VISIBLE
    }

    private fun renderPhraseState() {
        phraseState.text = "口令已保存，将在下次启动日不落时校验。"
        phraseState.visibility = if (pendingPhrase.isFile) View.VISIBLE else View.GONE
    }

    private fun savePhrase(input: EditText) {
        val phrase = input.text.toString()
        val bytes = phrase.toByteArray(Charsets.UTF_8)
        if (phrase.isBlank() || bytes.size > 256 || '\u0000' in phrase) {
            input.error = "请输入有效的口令。"
            return
        }
        try {
            val staged = File.createTempFile("sunset-phrase-", ".tmp", noBackupFilesDir)
            try {
                staged.writeBytes(bytes)
                Os.rename(staged.path, pendingPhrase.path)
            } finally {
                staged.delete()
            }
        } catch (_: Exception) {
            input.error = "口令保存失败，请重试。"
            return
        }
        getSystemService(InputMethodManager::class.java)?.hideSoftInputFromWindow(input.windowToken, 0)
        input.text.clear()
        input.clearFocus()
        renderPhraseState()
    }

    private fun chooseImport() {
        if (busy) return
        AlertDialog.Builder(this)
            .setTitle("导入日不落 0.49")
            .setItems(arrayOf("选取完整包与 11 月 2 日补丁", "选择已解压并合并补丁的目录")) { _, index ->
                val archive = index == 0
                val intent = if (archive) {
                    Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                        .setType("*/*").putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
                } else {
                    Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
                }
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
                try {
                    startActivityForResult(intent, if (archive) IMPORT_ARCHIVES else IMPORT_DIRECTORY)
                } catch (_: ActivityNotFoundException) {
                    Toast.makeText(this, "系统文件选择器不可用。", Toast.LENGTH_LONG).show()
                }
            }
            .setNegativeButton("取消", null)
            .show()
    }

    @Deprecated("Platform activity result for the file picker")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (!supportsSunset || busy || resultCode != RESULT_OK || data == null ||
            requestCode !in IMPORT_ARCHIVES..IMPORT_DIRECTORY) return
        val archive = requestCode == IMPORT_ARCHIVES
        val clip = data.clipData
        val uris = if (archive && clip != null) {
            (0 until clip.itemCount).mapNotNull { clip.getItemAt(it).uri }.distinct()
        } else {
            listOfNotNull(data.data)
        }
        if (uris.size != (if (archive) 2 else 1)) {
            resultMessage = if (archive) "请在文件选择器里同时选择完整包与补丁。" else "请选择解压目录。"
            render()
            return
        }
        val resolver = applicationContext.contentResolver
        val existing = resolver.persistedUriPermissions.filter { it.isReadPermission }.map { it.uri }.toSet()
        val acquired = mutableListOf<Uri>()
        fun release() {
            for (uri in acquired) runCatching {
                resolver.releasePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
        }
        try {
            for (uri in uris.filterNot(existing::contains)) {
                resolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
                acquired.add(uri)
            }
        } catch (_: SecurityException) {
            release()
            resultMessage = "无法读取所选内容。请先将文件保存到手机，再通过系统文件选择器导入。"
            render()
            return
        }
        val installer = install
        startInstall("准备导入…") { report ->
            try {
                if (archive) installer.importArchives(resolver, uris, report)
                else installer.importDirectory(resolver, uris.single(), report)
            } finally {
                release()
            }
        }
    }

    private fun startInstall(initialMessage: String, run: ((String, Int?) -> Unit) -> Unit) {
        if (busy || !supportsSunset) return
        busy = true
        transferMessage = initialMessage
        transferPercent = null
        resultMessage = ""
        render()
        scope.launch {
            try {
                withContext(Dispatchers.IO) {
                    run { message, percent ->
                        main.post {
                            transferMessage = message
                            transferPercent = percent
                            observer?.invoke()
                        }
                    }
                }
                resultMessage = "安装完成，可将日不落设为默认启动项。"
            } catch (e: Exception) {
                resultMessage = e.localizedMessage ?: "日不落安装失败，请重试。"
            } finally {
                busy = false
                observer?.invoke()
            }
        }
    }

    companion object {
        private const val IMPORT_ARCHIVES = 1
        private const val IMPORT_DIRECTORY = 2
        private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
        private val main = Handler(Looper.getMainLooper())
        private var observer: (() -> Unit)? = null
        private var busy = false
        private var transferPercent: Int? = null
        private var transferMessage = ""
        private var resultMessage = ""
    }
}
