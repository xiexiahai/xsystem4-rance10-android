package io.github.kichikuou.xsystem4

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.res.AssetManager
import android.os.Bundle
import android.util.Log
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import kotlinx.coroutines.*
import org.json.JSONObject
import java.io.File

private sealed class BundledState {
    object Idle : BundledState()
    data class Working(val done: Long, val total: Long) : BundledState()
    data class Ready(val dir: File, val apkArchives: Array<String>) : BundledState()
    data class Failed(val message: String) : BundledState()
}

/** Application-scoped work survives Activity recreation; a killed process resumes from files. */
private object BundledPreparation {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    var state: BundledState = BundledState.Idle
        private set
    var observer: ((BundledState) -> Unit)? = null

    fun start(context: Context) {
        if (state is BundledState.Working || state is BundledState.Ready) return
        update(BundledState.Working(0, 0))
        scope.launch {
            try {
                val prepared = withContext(Dispatchers.IO) {
                    val json = context.assets.open("bundled-game.json").bufferedReader().use {
                        JSONObject(it.readText())
                    }
                    require(json.getInt("format") == 1)
                    val entries = json.getJSONArray("files")
                    val manifest = BundledManifest(json.getString("contentId"),
                        (0 until entries.length()).map { i -> entries.getJSONObject(i).let {
                            BundledFile(it.getString("path"), it.getLong("size"), it.getString("sha256"), it.optBoolean("inApk"))
                        } })
                    require(manifest.payloadBytes == json.getLong("totalBytes"))
                    val direct = manifest.files.filter { it.inApk }
                    // Stored ZIP entries provide an fd slice: reject accidental
                    // recompression instead of allocating a gigabyte-sized buffer.
                    direct.forEach { entry -> context.assets.openFd("game/${entry.path}").use {
                        require(it.declaredLength == entry.size)
                    } }
                    var lastUpdate = 0L
                    val dir = BundledGameStore(File(context.noBackupFilesDir, "bundled-game")).prepare(manifest,
                        { context.assets.open("game/$it", AssetManager.ACCESS_STREAMING) },
                        { done, total ->
                            val now = System.nanoTime()
                            if (done == total || now - lastUpdate >= 150_000_000) {
                                lastUpdate = now
                                scope.launch { update(BundledState.Working(done, total)) }
                            }
                        })
                    Pair(dir, direct.map { it.path }.toTypedArray())
                }
                update(BundledState.Ready(prepared.first, prepared.second))
            } catch (e: Exception) {
                Log.e("BundledGame", "Preparation failed", e)
                val message = if (e is BundledSpaceException)
                    "空间不足，还需 ${mib(e.required)} MB，当前可用 ${mib(e.available)} MB。请清理空间后重试。"
                else "游戏准备未完成。请确认存储空间充足后重试；已完成的文件会保留。"
                update(BundledState.Failed(message))
            }
        }
    }

    private fun update(value: BundledState) { state = value; observer?.invoke(value) }
    private fun mib(n: Long): Long = (n + 1024 * 1024 - 1) / (1024 * 1024)
}

class BundledLauncherActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var progress: ProgressBar
    private lateinit var action: Button
    private var launched = false
    private var content: LinearLayout? = null
    private var uiShown = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val padding = (24 * resources.displayMetrics.density).toInt()
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER
            setPadding(padding, padding, padding, padding)
        }
        layout.addView(TextView(this).apply {
            text = applicationInfo.loadLabel(packageManager); textSize = 28f; gravity = Gravity.CENTER
        })
        status = TextView(this).apply { textSize = 17f; setPadding(0, padding, 0, padding) }
        layout.addView(status)
        progress = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { max = 1000 }
        layout.addView(progress, LinearLayout.LayoutParams(-1, -2))
        action = Button(this)
        layout.addView(action)
        layout.addView(Button(this).apply {
            text = "开源许可"
            setOnClickListener { startActivity(Intent(this@BundledLauncherActivity, LicensesMenuActivity::class.java)) }
        })
        // Kept detached: the window stays invisible while files are already
        // prepared. Only first-time extraction or an error attaches it.
        content = layout
    }

    private fun showUi() {
        if (uiShown) return
        uiShown = true
        content?.let { setContentView(it) }
    }

    override fun onResume() {
        super.onResume()
        BundledPreparation.observer = { render(it) }
        if (BundledPreparation.state is BundledState.Idle) BundledPreparation.start(applicationContext)
        else render(BundledPreparation.state)
    }

    override fun onPause() {
        BundledPreparation.observer = null
        super.onPause()
    }

    private fun render(state: BundledState) {
        action.isEnabled = state is BundledState.Ready || state is BundledState.Failed
        progress.isIndeterminate = state is BundledState.Idle || state is BundledState.Working && state.total == 0L
        when (state) {
            BundledState.Idle -> status.text = "正在检查游戏文件…"
            is BundledState.Working -> {
                if (state.total > 0) showUi()
                val percent = if (state.total > 0) state.done * 100 / state.total else 0
                status.text = "首次启动正在准备游戏：$percent%\n无需联网，请稍候。"
                progress.progress = (percent * 10).toInt()
                action.text = "准备中…"
            }
            is BundledState.Failed -> {
                showUi()
                status.text = state.message; action.visibility = android.view.View.VISIBLE
                action.text = "重试"
                action.setOnClickListener { launched = false; BundledPreparation.start(applicationContext) }
            }
            is BundledState.Ready -> {
                // Enter the game directly: the preparation screen is only a
                // first-launch step, never a menu between the user and the game.
                status.text = "正在启动…"; progress.progress = 1000
                action.visibility = android.view.View.GONE
                if (!launched) {
                    launched = true
                    launchGame(state.dir, state.apkArchives)
                }
            }
        }
    }

    private fun launchGame(dir: File, apkArchives: Array<String>) {
        // Same save-profile selection as the standard launcher: the cheat build
        // uses cheat-saves/main, the normal build uses saves/main. The bundled
        // launcher used to hard-code SaveData, which made existing saves look
        // lost after an upgrade.
        val preferences = getSharedPreferences("rance10-launch", MODE_PRIVATE)
        val profileKey = if (BuildConfig.RANCE10_CHEATS) "active_cheat_save_profile" else "active_save_profile"
        val exists = SaveProfile.DirectoryExists { File(filesDir, it).isDirectory }
        val relative = if (BuildConfig.RANCE10_CHEATS)
            SaveProfile.selectCheat(preferences.getString(profileKey, null), null, exists)
        else
            SaveProfile.select(preferences.getString(profileKey, null), null, exists)
        val save = File(filesDir, relative)
        if (!save.isDirectory && !save.mkdirs()) {
            status.text = "无法创建存档目录，请清理空间后重试。"
            return
        }
        preferences.edit().putString(profileKey, relative).apply()
        // Migrate the temporary SaveData used by older bundled builds, but only
        // when the selected profile is still empty (never overwrite progress).
        val legacy = File(filesDir, "SaveData")
        if (legacy.isDirectory && (save.list()?.isEmpty() != false)) {
            legacy.listFiles()?.forEach { file ->
                if (file.isFile) runCatching { file.copyTo(File(save, file.name), overwrite = false) }
            }
        }
        startActivity(Intent(this, XSystem4Activity::class.java).apply {
            Rance10Diagnostics.forward(this@BundledLauncherActivity, intent, this)
            putExtra(XSystem4Activity.EXTRA_GAME_ROOT, dir.path)
            putExtra(XSystem4Activity.EXTRA_SAVE_DIR, save.path)
            putExtra(XSystem4Activity.EXTRA_APK_ARCHIVES, apkArchives)
            // Same save format as the standard launcher: without this flag the
            // engine falls back to classic GSAVE, and R10GS1 saves written by a
            // previous build are rejected at load time (auto-save -> title).
            putExtra("GLOBAL_GRAPH_PROBE", true)
            addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT)
        })
    }
}
