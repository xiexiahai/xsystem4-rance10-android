package io.github.kichikuou.xsystem4

import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.ShortcutInfo
import android.content.pm.ShortcutManager
import android.graphics.drawable.Icon
import android.os.Build
import android.util.Log
import java.util.concurrent.Executors

private val shortcutUpdates = Executors.newSingleThreadExecutor()
private const val ICON_REVISION = 1
private const val ICON_REVISION_KEY = "launch_options_icon_revision"

class Rance10ShortcutUpgradeReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_MY_PACKAGE_REPLACED) return
        val pending = goAsync()
        context.registerRance10Shortcuts { pending.finish() }
    }
}

internal fun Context.registerRance10Shortcuts(onComplete: (() -> Unit)? = null) {
    if (!BuildConfig.RANCE10_SAVE_TRANSFER_ENABLED || Build.VERSION.SDK_INT < 25) {
        onComplete?.invoke()
        return
    }
    val app = applicationContext
    shortcutUpdates.execute {
        try {
            app.updateRance10Shortcuts()
        } finally {
            onComplete?.invoke()
        }
    }
}

private fun Context.updateRance10Shortcuts() {
    val manager = getSystemService(ShortcutManager::class.java) ?: return
    try {
        val retired = setOf("rance10-save-transfer", "rance10-gamepad")
        val dynamic = manager.dynamicShortcuts
        val obsolete = dynamic.filter { it.id in retired }.map { it.id }
        if (obsolete.isNotEmpty()) manager.removeDynamicShortcuts(obsolete)
        val oldPinned = manager.pinnedShortcuts.filter { it.id in retired && !it.isImmutable && it.isEnabled }
        if (oldPinned.isNotEmpty()) {
            manager.disableShortcuts(oldPinned.map { it.id }, "请长按游戏图标，选择「启动项」。")
        }
        val id = "rance10-launch-options"
        if (manager.manifestShortcuts.any { it.id == id }) return
        val existing = dynamic.any { it.id == id }
        val preferences = getSharedPreferences("rance10_launcher_shortcuts", Context.MODE_PRIVATE)
        if (existing && preferences.getInt(ICON_REVISION_KEY, 0) == ICON_REVISION) return

        val launcher = if (BuildConfig.RANCE10_LAUNCH_OPTIONS) BundledLauncherActivity::class.java
            else Rance10LauncherActivity::class.java
        val shortcut = ShortcutInfo.Builder(this, id)
            .setActivity(ComponentName(this, launcher))
            .setShortLabel(getString(R.string.rance10_launch_options))
            .setIcon(Icon.createWithResource(this, R.drawable.ic_rance10_launch_options))
            .setIntent(Intent(this, Rance10LaunchOptionsActivity::class.java).setAction(Intent.ACTION_VIEW))
            .build()
        val updated = if (existing) manager.updateShortcuts(listOf(shortcut))
            else manager.addDynamicShortcuts(listOf(shortcut))
        if (updated) preferences.edit().putInt(ICON_REVISION_KEY, ICON_REVISION).apply()
    } catch (e: RuntimeException) {
        Log.w("Rance10Shortcuts", "Could not register launcher shortcuts", e)
    }
}
