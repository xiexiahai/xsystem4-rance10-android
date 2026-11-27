package io.github.kichikuou.xsystem4

import android.content.SharedPreferences

internal object Rance10PlayMode {
    const val DEFAULT_CHEAT = "default_cheat_mode"
    const val EXTRA_CHEAT = "RANCE10_CHEAT_MODE"

    val selectable = BuildConfig.RANCE10_CHEATS && BuildConfig.RANCE10_LAUNCH_OPTIONS

    fun useCheats(preferences: SharedPreferences): Boolean =
        BuildConfig.RANCE10_CHEATS && (!selectable || preferences.getBoolean(DEFAULT_CHEAT, true))
}
