package io.github.kichikuou.xsystem4

import android.view.KeyEvent

// No menu, native MOD bridge or cheat state is included in the normal variant.
object GameVariant {
    fun attach(activity: XSystem4Activity) = Unit
    fun detach() = Unit
    fun handleKey(event: KeyEvent) = false
}
