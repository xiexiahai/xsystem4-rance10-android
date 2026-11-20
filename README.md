# xsystem4 — Rance 03 / Rance 10 engine support (Android fork)

An Android (ARM64) fork derived from
[kichikuou/xsystem4](https://github.com/kichikuou/xsystem4), the open-source
reimplementation of AliceSoft's System 4 / AIN engine.

## 兰斯10 安卓版 1.0.6（151）

普通版和 cheat 版同步更新。新增桌面长按「存档迁移」与「手柄映射」，
修复战斗 LOG 重复、部队编制误选或卡住、部分音效漏播的问题，
并为重新战斗读档失败增加了诊断编号。[查看 1.0.6 更新说明](docs/release-1.0.6.md)

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

Both titles are playable.

### [1.0.5](docs/release-1.0.5.md)

* Fix simple-battle defeat/retry resume, overlapping settings controls, and
  click-through during rapidly opened/closed party-formation transitions.
* Fix shared condition parsing used by some special battle mechanics, Part 2
  character-setup label spacing, and incomplete legacy-save imports.
* Improve safe release of preparation data, framebuffer reuse, and source-image
  caching with a 4 MiB per-batch budget and cleanup after last use/failure.
* Connect gamepad input to the original supported menus/dialogues: **right stick
  cursor**, **left stick navigation/scrolling**, A confirm/click/drag, B cancel,
  L1 held Ctrl, R1 precision movement. Some pages still use the cursor.
* Retain the touch Ctrl button on the **right**, with improved layout, accidental
  touch protection and cancellation/release handling.

Earlier 1.0.4 fixes retained:

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
  letterbox toggles an on-screen Ctrl button instead (right-side placement).
* Launcher icon updated to the official Rance 10 cover.

Known issues:

* Full on-device acceptance of rapid party-formation transitions, physical
  controllers and late-game special battles remains open; targeted local tests
  are not a complete playthrough.
* The correct activation/assignment of the route-completion display and Alice's
  mansion content remain under investigation.
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
mkdir logs
python tools/build_port.py            # Windows/MSYS2: build/install into dist/
```

`MSYS2_ROOT` (default `C:/msys64`) points at the MSYS2 installation.

### Android engine

```sh
mkdir logs
# Windows/MSYS2 helpers; configure XSYSTEM4_ANDROID_TOOLS / ANDROID_* first.
python tools/build_android_native_local.py            # xsystem4 engine
python tools/build_android_native.py --cheats         # cheat variant
```

`ANDROID_SDK_ROOT` / `ANDROID_NDK_HOME` / `JAVA_HOME` take precedence when set.
For Linux, [the CI workflow](.github/workflows/build.yml) contains the complete
CMake invocation, JNI staging and Gradle build steps. It builds a development
APK without game data. To build the cheat variant, use a separate native build
directory with `XSYSTEM4_RANCE10_CHEATS=ON`, stage its engine plus SDL2/cglm in
`android/cheats-jni/arm64-v8a`, then pass `-PRANCE10_CHEATS` to Gradle.

### Bundled payload + APK

```sh
python tools/prepare-bundled-game.py --game "/path/to/Rance10" --movies /path/to/movies
cd android/project
./gradlew assembleDebug -PBUNDLED_GAME       # self-contained APK
./gradlew assembleDebug -PBUNDLED_GAME -PRANCE10_CHEATS  # separate cheat APK
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
