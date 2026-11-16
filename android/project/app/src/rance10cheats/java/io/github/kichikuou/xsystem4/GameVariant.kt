package io.github.kichikuou.xsystem4

import android.view.KeyEvent

object GameVariant {
    private var panel: Rance10CheatPanel? = null

    fun attach(activity: XSystem4Activity) {
        detach()
        panel = Rance10CheatPanel(activity,
            Rance10CheatBridge::nativeGetFlags,
            Rance10CheatBridge::nativeSetFlags,
            Rance10CheatBridge::nativeGetCapabilities)
    }

    fun detach() {
        panel?.detach()
        panel = null
    }

    fun handleKey(event: KeyEvent) = panel?.handleKey(event) ?: false
}
