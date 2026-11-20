package io.github.kichikuou.xsystem4

import android.app.Activity
import android.content.Intent
import android.content.pm.ShortcutInfo
import android.content.pm.ShortcutManager
import android.graphics.drawable.Icon
import android.os.Build
import android.util.Log

internal fun Activity.registerRance10Shortcuts() {
    if (!BuildConfig.RANCE10_SAVE_TRANSFER_ENABLED || Build.VERSION.SDK_INT < 25) return
    val manager = getSystemService(ShortcutManager::class.java) ?: return
    try {
        val present = (manager.manifestShortcuts + manager.dynamicShortcuts).map { it.id }.toSet()
        val entries = listOf(
            Triple("rance10-save-transfer", R.string.rance10_save_transfer, SaveTransferActivity::class.java),
            Triple("rance10-gamepad", R.string.rance10_gamepad_mapping, GamepadMappingActivity::class.java)
        )
        val shortcuts = entries.filter { it.first !in present }.map { (id, label, target) ->
            ShortcutInfo.Builder(this, id)
                .setActivity(componentName)
                .setShortLabel(getString(label))
                .setIcon(Icon.createWithResource(this, R.mipmap.ic_launcher))
                .setIntent(Intent(this, target).setAction(Intent.ACTION_VIEW))
                .build()
        }
        if (shortcuts.isNotEmpty()) manager.addDynamicShortcuts(shortcuts)
    } catch (e: RuntimeException) {
        Log.w("Rance10Shortcuts", "Could not register launcher shortcuts", e)
    }
}
