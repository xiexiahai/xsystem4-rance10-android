package io.github.kichikuou.xsystem4

import android.content.Context
import android.view.KeyEvent

object GameVariant {
    private const val FLAGS = "enabled_flags"
    private var panel: Rance10CheatPanel? = null

    fun attach(activity: XSystem4Activity) {
        detach()
        if (!activity.cheatsEnabled) return
        val preferences = activity.getSharedPreferences("rance10_cheats", Context.MODE_PRIVATE)
        Rance10CheatBridge.nativeSetFlags(preferences.getInt(FLAGS, 0))
        panel = Rance10CheatPanel(activity,
            Rance10CheatBridge::nativeGetFlags,
            { flags ->
                val supported = Rance10CheatBridge.nativeGetCapabilities()
                val previous = preferences.getInt(FLAGS, 0)
                val updated = (previous and supported.inv()) or (flags and supported)
                Rance10CheatBridge.nativeSetFlags(updated)
                preferences.edit().putInt(FLAGS, updated).apply()
            },
            Rance10CheatBridge::nativeGetCapabilities,
            Rance10CheatBridge::nativeFoodRerollReady,
            Rance10CheatBridge::nativeRequestFoodReroll)
    }

    fun detach() {
        panel?.detach()
        panel = null
    }

    fun handleKey(event: KeyEvent) = panel?.handleKey(event) ?: false
}
