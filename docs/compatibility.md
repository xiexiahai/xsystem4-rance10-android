Game compatibility
==================

Upstream xsystem4's compatibility table ends at:

    All games released after Rance 01          Unsupported

This fork targets two of those titles on Android (ARM64):

| Title                    | Year | Upstream     | This fork                    |
|--------------------------|------|--------------|------------------------------|
| Rance 03                 | 2015 | Unsupported  | Runs (adapter build flag)    |
| Rance 10                 | 2018 | Unsupported  | Runs (1.0.4); known issues   |

"Runs" means the game starts, plays the story and battles, and saves/loads.
It does not mean every scene and every UI screen has been audited.

Known issues
------------

Rance 10
* Long sessions on some devices: battle pre-loading gets progressively slower
  and may crash/ANR. Under investigation.
* Map route display may occasionally be missing after loading a save.
* A texture-parking optimisation (off by default) caused stale redraws in the
  save list and dialogue; disabled in this tree.

Rance 03
* The adapter is a separate build flag; behaviour outside the main loop has not
  been audited as thoroughly as Rance 10.

Not tested
----------

Other post-Rance-01 titles have not been tried. The engine changes for AIN v12
are generic, but each title has its own HLL/parts surface; expect gaps.
