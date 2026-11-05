package io.github.kichikuou.xsystem4

import android.os.Bundle
import android.os.Build
import android.os.Process
import android.util.Log
import android.view.KeyEvent
import org.libsdl.app.SDLActivity

// Intent for this activity must have the following extras:
// - EXTRA_GAME_ROOT (string): A path to the game installation.
// - EXTRA_SAVE_DIR (string): A path to a directory where save files are stored.
class XSystem4Activity : SDLActivity() {
    companion object {
        const val EXTRA_GAME_ROOT = "GAME_ROOT"
        const val EXTRA_SAVE_DIR = "SAVE_DIR"
        const val EXTRA_APK_ARCHIVES = "APK_ARCHIVES"
        const val COMMAND_OPEN_PLAYING_MANUAL = 0x8000  // xsystem4/src/hll/SystemService.c
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // Workaround for https://github.com/libsdl-org/SDL/issues/8995
        SDLActivity.setWindowStyle(true)
        GameVariant.attach(this)
    }

    override fun getLibraries(): Array<String> {
        return arrayOf("SDL2", "xsystem4")
    }

    override fun getArguments(): Array<String> {
        // Offline build: online features stay disabled. Interactive game
        // confirmations remain enabled unless an explicit headless probe opts out.
        SDLActivity.nativeSetenv("XSYSTEM4_TEST_REJECT_NETWORK", "1")
        if (intent.getBooleanExtra("NO_DIALOG_DIAGNOSTIC", false))
            SDLActivity.nativeSetenv("XSYSTEM4_NO_DIALOG", "1")
        // Explicit diagnostic launch only; ordinary play does no timing/logging.
        SDLActivity.nativeSetenv("XSYSTEM4_PERF_TRACE",
            if (intent.getBooleanExtra("PERF_TRACE", false)) "1" else "0")
        SDLActivity.nativeSetenv("XSYSTEM4_TRACE_GLYPH_CACHE",
            if (intent.getBooleanExtra("GLYPH_CACHE_STATS", false)) "1" else "0")
        SDLActivity.nativeSetenv("XSYSTEM4_PERF_WINDOW",
            if (intent.getBooleanExtra("PERF_WINDOW", false)) "1" else "0")
        // Keep the dedicated Rance10 Chinese fallback inside its half-width cells.
        SDLActivity.nativeSetenv("XSYSTEM4_CJK_FIXED_CELLS", "1")
        if (intent.getBooleanExtra("PERF_TRACE", false)) {
            SDLActivity.nativeSetenv("XSYSTEM4_TRACE_TEXTURES", "1")
        }


        // Process-local click sequence in game coordinates, e.g. "5:960:719;9:120:900".
        // Lets automated on-device runs drive the game without host input injection.
        intent.getStringExtra("TEST_CLICK")?.let {
            SDLActivity.nativeSetenv("XSYSTEM4_TEST_CLICK", it)
        }
        intent.getStringExtra("TEST_PART_CLICK")?.let {
            SDLActivity.nativeSetenv("XSYSTEM4_TEST_PART_CLICK", it)
        }
        intent.getStringExtra("TEST_HITSCAN")?.let {
            SDLActivity.nativeSetenv("XSYSTEM4_TEST_HITSCAN", it)
        }
        if (intent.getBooleanExtra("TEST_FORCE", false))
            SDLActivity.nativeSetenv("XSYSTEM4_TEST_FORCE", "1")
        if (intent.getBooleanExtra("TEXTURE_ROUNDTRIP_PROBE", false)) {
            SDLActivity.nativeSetenv("XSYSTEM4_TEST_TEXTURE_ROUNDTRIP", "1")
        }
        if (intent.getBooleanExtra("RESUME_ROUNDTRIP_PROBE", false)) {
            SDLActivity.nativeSetenv("XSYSTEM4_TEST_RESUME_ROUNDTRIP", "45000")
        }
        if (intent.getBooleanExtra("GLOBAL_GRAPH_PROBE", false)) {
            // "1" writes R10GS1 images (turn/time/comment metadata); loading
            // dispatches by file magic, so classic GSAVE files still load.
            SDLActivity.nativeSetenv("XSYSTEM4_AIN12_GLOBAL_GRAPH", "1")
            if (intent.getBooleanExtra("PERF_TRACE", false))
                SDLActivity.nativeSetenv("XSYSTEM4_TRACE_EX_SNAPSHOT", "1")
        }
        if (intent.getBooleanExtra("TEXTBOX_NATIVE_PROBE", false)) {
            SDLActivity.nativeSetenv("XSYSTEM4_TEST_TEXTBOX_NATIVE", "1")
        }
        // x86 Android emulators translate this arm64 build. MuMu can leave
        // AAudio permanently STARTING with its first write blocked. Use SDL's
        // Java AudioTrack backend there; ARM devices retain the default driver.
        if (Build.SUPPORTED_ABIS.any { it == "x86_64" || it == "x86" }) {
            SDLActivity.nativeSetenv("SDL_AUDIODRIVER", "android")
            Log.i("xsystem4", "Using AudioTrack on x86 Android host")
        }
        val saveFolder = intent.getStringExtra(EXTRA_SAVE_DIR)!!
        val gameRoot = intent.getStringExtra(EXTRA_GAME_ROOT)!!
        val archives = intent.getStringArrayExtra(EXTRA_APK_ARCHIVES) ?: emptyArray()
        require(archives.all { it.matches(Regex("[A-Za-z0-9_-]+\\.afa")) })
        SDLActivity.nativeSetenv("XSYSTEM4_APK_AFA", archives.joinToString(";"))
        return arrayOf("--save-folder", saveFolder, "--save-format=rsm", gameRoot)
    }

    @Suppress("DEPRECATION")
    override fun onBackPressed() {
        // Let the VM's close hook show its confirmation while this window is
        // alive. Finishing first makes SDL join a VM waiting for that dialog.
        SDLActivity.nativeSendQuit()
    }

    override fun onDestroy() {
        GameVariant.detach()
        try {
            // Let SDL stop and join its native thread before terminating the game process.
            super.onDestroy()
        } finally {
            Process.killProcess(Process.myPid())
        }
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        return GameVariant.handleKey(event) || super.dispatchKeyEvent(event)
    }

    override fun onUnhandledMessage(command: Int, param: Any): Boolean {
        when (command) {
            COMMAND_OPEN_PLAYING_MANUAL -> {
                val gameRoot = intent.getStringExtra(EXTRA_GAME_ROOT)!!
                val manualDir = java.io.File(gameRoot, "Manual")
                if (manualDir.isDirectory) {
                    val intent = android.content.Intent(this, ManualActivity::class.java).apply {
                        val url = "file://${manualDir.absolutePath}/index.html"
                        putExtra(ManualActivity.EXTRA_URL, url)
                    }
                    startActivity(intent)
                }
                return true
            }
        }
        return super.onUnhandledMessage(command, param)
    }
}
