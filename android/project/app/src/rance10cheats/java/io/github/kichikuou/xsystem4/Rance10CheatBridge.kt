package io.github.kichikuou.xsystem4

/** Only present in the separate Rance10 cheat APK; native owns the live flags. */
object Rance10CheatBridge {
    external fun nativeSetFlags(flags: Int)
    external fun nativeGetFlags(): Int
    external fun nativeGetCapabilities(): Int
}
