package io.github.kichikuou.xsystem4

import android.content.Context
import android.content.Intent
import android.util.Log
import org.json.JSONObject
import org.libsdl.app.SDLActivity
import java.io.File

/** Read once per new game process. Changing the selection needs a restart, never a new APK. */
internal object Rance10Diagnostics {
    const val EXTRA = "R10_DIAGNOSTICS"
    private val environment = mapOf(
        "memory" to "XSYSTEM4_DIAG_MEMORY",
        "save" to "XSYSTEM4_DIAG_SAVE",
        "parts" to "XSYSTEM4_DIAG_PARTS",
        "input" to "XSYSTEM4_TRACE_INPUT",
        "hll" to "XSYSTEM4_TRACE_HLL",
        "perf" to "XSYSTEM4_PERF_TRACE",
        "glyph" to "XSYSTEM4_TRACE_GLYPH_CACHE",
        "window" to "XSYSTEM4_PERF_WINDOW"
    )

    fun forward(context: Context, source: Intent, target: Intent) {
        val selection = source.getStringExtra(EXTRA) ?: developmentSelection(context)
        target.putExtra(EXTRA, valid(selection).toString())
    }

    fun apply(source: Intent) {
        val selection = valid(source.getStringExtra(EXTRA))
        // Several legacy native flags test only getenv() presence: "0" is NOT off.
        // A game Activity runs in a fresh :game process and kills it on destruction.
        for ((name, variable) in environment) {
            if (selection.has(name)) SDLActivity.nativeSetenv(variable, "1")
        }
        if (selection.toString().isNotEmpty()) Log.i("Rance10Diagnostics", "enabled=$selection")
    }

    private fun valid(text: String?): Rance10DiagnosticOptions = try {
        Rance10DiagnosticOptions.parse(text)
    } catch (e: IllegalArgumentException) {
        Log.w("Rance10Diagnostics", "Invalid diagnostic selection; disabled", e)
        Rance10DiagnosticOptions.parse(null)
    }

    private fun developmentSelection(context: Context): String? {
        // No persistent diagnostic file is read by either clean delivery variant.
        if (!BuildConfig.RANCE10_DEVELOPMENT) return null
        val file = File(context.filesDir, "diagnostics.json")
        if (!file.isFile) return null
        return try {
            require(file.length() <= 4096) { "Diagnostic configuration exceeds 4KiB" }
            val json = JSONObject(file.readText(Charsets.UTF_8))
            require(json.length() == 2 && json.getInt("format") == 1 && json.has("enabled"))
            json.getString("enabled")
        } catch (e: Exception) {
            Log.w("Rance10Diagnostics", "Invalid development diagnostic file; disabled", e)
            null
        }
    }
}
