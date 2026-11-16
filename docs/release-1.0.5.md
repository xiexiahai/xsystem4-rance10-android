# Rance 10 Android 1.0.5

Version code: **145**. Normal and cheat variants use separate application IDs
and save directories. Update the matching variant in place to retain its data.

## Fixes

* Restore the constructor continuation when retrying a lost simple battle
  (yellow enemy), instead of exiting during resume.
* Honour widget visibility and template flags in MENU settings, fixing overlapping
  or misplaced controls on message, sound, input and game-setting pages.
* Hit-test scaled construction graphics in their rendered coordinates. This
  prevents clicks passing through the full-screen transition overlay when party
  formation is opened repeatedly or immediately closed.
* Correct the shared Split implementation's extra trailing empty field, which
  broke two-field conditions used by some special skills and battle events.
* Correct overlapping sex-option labels in Part 2 character setup. The female
  protagonist's original unlock condition is unchanged.
* Stage legacy-save imports before selecting the imported profile, so a failed
  copy does not activate an incomplete import.

## Controls and resource management

* The touch Ctrl control stays on the **right**, with improved appearance,
  accidental-touch protection and release handling.
* Connect supported gamepad input to the game's original input path. Right stick:
  cursor; left stick: original menu navigation or page scrolling; A: confirmation
  or pointer click/drag; B: context-dependent cancel; L1: hold Ctrl; R1: precision
  cursor movement. Some pages still require the cursor; there is no universal
  focus navigation across every selectable control.
* Release cancelled preparation data promptly when safe, reuse empty framebuffer
  objects, and cache source images within one construction batch with a **4 MiB
  total / 2 MiB per-image** limit. Entries are released after last use or failure.

Bundled builds retain direct game entry and exit. The cheat variant retains its
four independent switches: HP protection, no AP cost, maximum eligible treasure
drop probability, and food tickets / reusable owned Hani lighter.

## Validation and remaining work

Both ARM64 variants were built, package-checked and installed; existing save-file
fingerprints were unchanged. Local targeted tests cover the input and overlay
changes, resume, parsing and resource lifecycle. Full on-device acceptance of the
latest rapid-open fix, physical controllers and late-game special battles remains
open. These checks do not establish a full-game playthrough or a guaranteed
performance improvement on every device.

The correct activation/assignment of the route-completion display and Alice's
mansion content remain under investigation. Game data and self-contained APKs
are not distributed in this source repository.

## Acknowledgements

Thanks to [TomorrowX6 / AliceHub](https://github.com/TomorrowX6/AliceHub)
(formerly re-xsystem4) for public implementation references used when reviewing
Parts visibility, condition parsing and framebuffer reuse in this release.
