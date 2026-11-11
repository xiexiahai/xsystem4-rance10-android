package io.github.kichikuou.xsystem4

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.os.Bundle
import android.util.Log
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import java.io.File

/** Normal play reopens the selected play profile; instrumented probes stay separate. */
class Rance10LauncherActivity : Activity() {
    private var launchedGame = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (savedInstanceState?.getBoolean("launched_game", false) == true) {
            finish()
            return
        }
        val game = File(getExternalFilesDir(null), "game")
        val required = listOf("Rance10.ain", "AliceStart.ini") +
            listOf("OP1A.apeg", "OP1B.apeg", "OP1C.apeg", "ED2B.apeg").map { "Movie/$it" }
        val missing = required.filterNot { relative ->
            val file = File(game, relative)
            runCatching {
                file.isFile && (!file.extension.equals("apeg", ignoreCase = true) ||
                    file.inputStream().use { stream ->
                        val magic = ByteArray(4)
                        stream.read(magic) == 4 &&
                            magic.contentEquals(byteArrayOf(0, 0, 1, 0xba.toByte()))
                    })
            }.getOrDefault(false)
        }
        if (missing.isEmpty()) {
            launchProbe(game, intent.getBooleanExtra("TEXTURE_ROUNDTRIP_PROBE", false),
                intent.getBooleanExtra("RESUME_PROBE_PROFILE", false),
                intent.getBooleanExtra("RESUME_ROUNDTRIP_PROBE", false),
                intent.getBooleanExtra("TEXTBOX_NATIVE_PROBE", false),
                intent.getBooleanExtra("PERF_TRACE", false))
        } else {
            showMissingFiles(game, missing)
        }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        outState.putBoolean("launched_game", launchedGame)
        super.onSaveInstanceState(outState)
    }

    private fun showMissingFiles(game: File, missing: List<String>) {
        fun dp(value: Int) = (value * resources.displayMetrics.density).toInt()
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(24), dp(20), dp(20))
            addView(TextView(this@Rance10LauncherActivity).apply {
                text = "游戏文件不完整，无法启动。"
                textSize = 20f
                setTextColor(Color.WHITE)
            })
            addView(TextView(this@Rance10LauncherActivity).apply {
                text = "缺失或格式不正确：\n${missing.joinToString("\n")}\n\n游戏目录：\n${game.absolutePath}"
                textSize = 14f
                setTextColor(Color.WHITE)
                setTextIsSelectable(true)
                setPadding(0, dp(16), 0, dp(16))
            })
            addView(Button(this@Rance10LauncherActivity).apply {
                text = "退出"
                setOnClickListener { finish() }
            }, LinearLayout.LayoutParams(-1, -2))
        }
        setContentView(ScrollView(this).apply { addView(content) })
    }

    private fun launchProbe(game: File, textureRoundtrip: Boolean = false, resumeProfile: Boolean = false, resumeRoundtrip: Boolean = false, textboxProbe: Boolean = false, perfTrace: Boolean = false) {
        if (launchedGame) return
        val preferences = getSharedPreferences("rance10-launch", MODE_PRIVATE)
        val developmentSaves = if (BuildConfig.RANCE10_DEVELOPMENT) {
            try {
                val explicit = intent.hasExtra(DevelopmentSaveProfile.EXTRA)
                val selected = DevelopmentSaveProfile.select(filesDir, explicit,
                    intent.getStringExtra(DevelopmentSaveProfile.EXTRA),
                    if (explicit) null else preferences.getString(DevelopmentSaveProfile.PREFERENCE, null))
                if (explicit && !preferences.edit().putString(DevelopmentSaveProfile.PREFERENCE, selected.name).commit())
                    throw java.io.IOException("Could not persist development lab selection")
                selected
            } catch (e: Exception) {
                // Fail closed for invalid requests, stale profiles and persistence errors.
                Log.w("Rance10Launcher", "Development lab profile rejected; game not started", e)
                Toast.makeText(this, "开发存档目录无效或选择未能保存，游戏未启动。请重新选择有效的实验目录。", Toast.LENGTH_LONG).show()
                finish()
                return
            }
        } else null
        val profileKey = if (BuildConfig.RANCE10_CHEATS) "active_cheat_save_profile" else "active_save_profile"
        val exists = SaveProfile.DirectoryExists { File(filesDir, it).isDirectory }
        val relative = if (BuildConfig.RANCE10_DEVELOPMENT) {
            "dev-saves/main"
        } else if (textboxProbe) {
            if (BuildConfig.RANCE10_CHEATS) "cheat-textbox-probe-saves/v66" else "textbox-probe-saves/v66"
        } else if (BuildConfig.RANCE10_CHEATS) {
            SaveProfile.selectCheat(preferences.getString(profileKey, null),
                if (resumeProfile) "cheat-probe-saves/v66" else null, exists)
        } else {
            SaveProfile.select(preferences.getString(profileKey, null),
                if (resumeProfile) "resume-probe-saves/v66" else null, exists)
        }
        val saves = developmentSaves ?: File(filesDir, relative)
        check(saves.isDirectory || ((developmentSaves == null || saves.name == "main") && saves.mkdirs()))
        if (!BuildConfig.RANCE10_DEVELOPMENT && !textboxProbe && !textureRoundtrip && !resumeRoundtrip)
            preferences.edit().putString(profileKey, relative).apply()
        launchedGame = true
        startActivity(Intent(this, XSystem4Activity::class.java).apply {
            Rance10Diagnostics.forward(this@Rance10LauncherActivity, intent, this)
            addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP)
            putExtra(XSystem4Activity.EXTRA_GAME_ROOT, game.absolutePath)
            putExtra(XSystem4Activity.EXTRA_SAVE_DIR, saves.absolutePath)
            putExtra("TEXTURE_ROUNDTRIP_PROBE", textureRoundtrip)
            putExtra("RESUME_ROUNDTRIP_PROBE", resumeRoundtrip)
            putExtra("GLOBAL_GRAPH_PROBE", !textboxProbe)
            putExtra("TEXTBOX_NATIVE_PROBE", textboxProbe)
            putExtra("PERF_TRACE", perfTrace)
            putExtra("NO_DIALOG_DIAGNOSTIC", intent.getBooleanExtra("NO_DIALOG_DIAGNOSTIC", false))
            putExtra("GLYPH_CACHE_STATS", intent.getBooleanExtra("GLYPH_CACHE_STATS", false))
            putExtra("PERF_WINDOW", intent.getBooleanExtra("PERF_WINDOW", false))
            intent.getStringExtra("TEST_CLICK")?.let { putExtra("TEST_CLICK", it) }
            intent.getStringExtra("TEST_PART_CLICK")?.let { putExtra("TEST_PART_CLICK", it) }
            intent.getStringExtra("TEST_HITSCAN")?.let { putExtra("TEST_HITSCAN", it) }
            putExtra("TEST_FORCE", intent.getBooleanExtra("TEST_FORCE", false))
        })
        finish()
    }
}
