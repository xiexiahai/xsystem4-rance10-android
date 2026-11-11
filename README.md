# xsystem4 — Rance 03 / Rance 10 engine support (Android fork)

An Android (ARM64) fork derived from
[kichikuou/xsystem4](https://github.com/kichikuou/xsystem4), the open-source
reimplementation of AliceSoft's System 4 / AIN engine.

Upstream's compatibility table lists **"All games released after Rance 01" as
Unsupported**. This fork's main work is engine support for two of those titles:

* **Rance 10** (2018, AIN v12 era)
* **Rance 03** (2015; separate adapter build flag)

The Android application and self-contained APK packaging are secondary: they
make the result installable, but they are not the substance of the fork.

Game data is **not** included. You need your own copy of the game.

## Engine work

Compared with upstream, the tree adds (file names are illustrative):

**AIN v12 runtime**
* VM intrinsics and value model (`ain12_vm.inc`)
* Array family: concat, erase-all / erase-set, extrema, first/last, order, where
* String regex / replace
* Message restore path

**Saves and global state**
* R10GS1 save format (`ain12_save.inc`)
* Global graph: decode / envelope / binding / frame binding / install / walk,
  JSON form and program cache (`global_graph_*`, `global_save_graph.inc`)
* Resume snapshots and comment cache (`resume12.inc`, `g12_comment_program_cache.h`)

**ADV / message window / parts**
* PACT activity system (`parts/activity.c`, `activity_load.c`, `activity_save.inc`)
* Message window (`message_window.c`, `pact_message_window.inc`,
  `message_window_api.inc`, `message_window_save.inc`)
* Textbox / listbox / scroll / spin-form widgets and their PACT parsers
* Flat graphics preparation, layout, numeral/gauge layout, movie parts,
  texture lifecycle

**HLL modules missing upstream**
ADVEngine, AnteaterADVLogList, AFAFactory, Array, CalcTable, Clipboard, Discord,
DrawNumeral, FileDialog, HTTPDownloader, InstallInfo, MADLoader, MusicSystem,
P3 map/square sprites, TextFile, TextSurfaceManager, Sys43VM, ValueEncryption,
mainex12 snapshots, and others.

**Title-specific adapters**
* `rance03_mod.c` — Rance 03 gameplay hooks (compile-time flag)
* `rance10_cheats.c` — optional cheat module for Rance 10 (separate package)

See [docs/compatibility.md](docs/compatibility.md) for the status table.

## Status

Playable for both titles. Current Android release: **1.0.4 (versionCode 144,
2026-11-11)**.

Fixed in 1.0.4 (relative to earlier public snapshots):

* Dialogue MENU can be opened repeatedly (layers erased while the menu was open
  are restored when the menu/window is re-created).
* Party-change / shuffle now randomises correctly when the script passes a
  negative seed.
* Save/load: auto-save size limit raised, save-profile directories fixed
  (`saves/main`, `cheat-saves/main`), global-graph load flag forwarded by the
  launcher, and layer/route state restored after loading.
* Save/load of composed textures no longer loses final pixels (snapshot v29 for
  non-replayable images; ordinary snapshots stay byte-identical to v28).
* Auto-save false rejection for weak receiver/closure states fixed.
* Resource budget back-pressure for CG preparation and texture restore
  (reduces load stalls under memory pressure).
* Touch controls: long-press no longer triggers Ctrl; tapping the left/right
  letterbox toggles an on-screen Ctrl button instead (button placement is
  right-side in 1.0.4; moving to the left is planned).
* Launcher icon updated to the official Rance 10 cover.

Known issues:

* On long sessions some devices still get progressively slower battle
  pre-loading, and may eventually crash/ANR. Save and resource paths have been
  optimised, but the root cause is not fully confirmed.
* Map route display may occasionally be missing after loading a save; not
  reproduced reliably.
* A texture-parking benchmark mode is off by default (`XSYSTEM4_PARK_SOURCE=1`
  re-enables it).

## Android packaging (secondary)

`android/project/app` contains the Android application. Two launch paths exist:

* `Rance10LauncherActivity` — development mode; reads the game from
  `/sdcard/Android/data/<package>/files/game`.
* `BundledLauncherActivity` — self-contained mode; the game payload is packaged
  into the APK and prepared on first launch.

Bundled mode is content-addressed and incremental: `.afa` archives are mapped
directly out of the APK (fd slice) instead of being copied, the remaining files
are extracted into `<app>/no_backup/bundled-game/<content-id>` with per-file
SHA-256 verification and resumable `.part` files. Saves live in
`<app>/files/saves/main` (normal) and `<app>/files/cheat-saves/main` (cheat
variant); an older `SaveData` directory is only read through the migration
path.

## Building

Requirements: Python 3.10+, Meson + Ninja, CMake 3.22, Android NDK r26
(`26.3.11579264`), SDK platform 34, JDK 17, Gradle 8.x.

### Desktop

```sh
python tools/build_port.py            # meson setup/build/install into dist/
```

`MSYS2_ROOT` (default `C:/msys64`) points at the MSYS2 installation.

### Android engine

```sh
export XSYSTEM4_ANDROID_TOOLS=/path/to/tools/android
python tools/build_android_native_local.py            # xsystem4 engine
python tools/build_android_native.py --cheats         # cheat variant
```

`ANDROID_SDK_ROOT` / `ANDROID_NDK_HOME` / `JAVA_HOME` take precedence when set.

### Bundled payload + APK

```sh
python tools/prepare-bundled-game.py --game "/path/to/Rance10" --movies /path/to/movies
cd android/project
./gradlew assembleDebug -PBUNDLED_GAME       # self-contained APK
./gradlew assembleDebug                      # dev build (external files/game)
```

Bundled assets are read from `$BUNDLED_ASSETS` (default `work/standalone-assets`).

## Layout

```
engine/                 engine sources (C, meson)
engine/subprojects/     bundled libsys4 / cglm
android/project/        Android application (Kotlin + CMake/JNI)
android/deps/           prebuilt third-party ARM64 libraries
tools/                  build and packaging scripts
docs/                   compatibility notes
```

## Licence

GPL-2.0-or-later, see `COPYING`. Third-party components are listed in `NOTICE`
and under `android/project/app/src/main/assets/licenses/`.

This project is not affiliated with AliceSoft. Rance 03 / Rance 10 and their
assets are copyright AliceSoft; nothing here grants rights to game data.
