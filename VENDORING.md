# Vendoring

Upstream: **MCUXpresso SDK v26.06.00-LTS**, `middleware/vglite/driver`
(`mcuxsdk` commit `a910e7645d2d809a3431e1d5f42fca1cdeee69c9`) — the same SDK
release the sibling LVGL tree is vendored from, so the two agree by
construction. Vivante VGLite, **`VGLITE_HEADER_VERSION 7` / `VGLITE_VERSION_3_0`**.

★ **Re-vendored 2026-08-17, from `VGLITE_HEADER_VERSION 6`.** The previous copy
came from `gs-vglite_examples_rt1170` and was chosen because it was MIT. It was
also OLDER than LVGL 9.4's VG_LITE backend expects, and no amount of shimming
closed that: names can be macros, but the missing pieces were *code* — the whole
`vg_lite_stroke_t` API, an 11th parameter on `vg_lite_draw_pattern`,
`VG_LITE_PATTERN_REPEAT`, and a gradient table smaller than
`LV_GRADIENT_MAX_STOPS`. Stroking settled it: a vector backend without strokes
is not the feature. Full account in the consuming tree's
`docs/superpowers/specs/2026-08-17-vglite-phase2-design.md` §8.

This release also **drops `vg_lite_flat.{c,h}`**, the two Apache-2.0 files the
previous copy carried, so this repository is now MIT throughout — see `NOTICE`.

## Licence

**MIT throughout.** No copyleft, no Apache. Verified on the re-vendor,
2026-08-17, with the per-file survey below rather than a copyleft grep alone:

```sh
grep -rIl -E "GNU General Public|GNU Lesser|Mozilla Public" --exclude-dir=.git .   # -> empty
for f in $(git ls-files '*.c' '*.h'); do
  grep -qE "Permission is hereby granted|MIT License" "$f" || echo "NO PERMISSIVE TEXT: $f"
done
```
Only `port/baremetal/*` prints, and those are this tree's own work under the
repo `LICENSE`.

★ **The previous copy was NOT MIT throughout, and the claim outlived the fact
for a day.** `VGLite/vg_lite_flat.{c,h}` were Apache-2.0 (Raph Levien / Nicolas
Silva / NXP 2022) and linked into firmware. **This release does not contain
them at all**, so the exception is gone rather than merely corrected — but the
lesson that produced this survey stays: the consuming tree's licence audit greps
for COPYLEFT, and Apache-2.0 is not copyleft, so it passed correctly the whole
time. "The audit is green" answers *is there copyleft here*, not *is this repo
the licence it advertises*.

★ **A second blind spot, found and fixed on this re-vendor.**
`VGLite/vg_lite_stroke.c` shipped as **ISO-8859-1**, and `grep -I` (which the
audit's Part 1 uses) treats a non-UTF-8 file as *binary* and silently skips it.
Measured before transcoding: `grep -I` found nothing in that file while
`grep -a` found Vivante's MIT text — i.e. a source file the copyleft sweep never
read. It is transcoded to UTF-8 here, and the re-vendoring checklist now has an
encoding step. Same class as the unlicensed-binary hole, through a different
door.

★ **This is NOT the copy bundled with LVGL.** LVGL's
`src/libs/vg_lite_driver/` carries a dual-licensed VGLiteKernel that trips
`tools/license-audit.sh` Part 1 on 5 files, and it is correctly pruned in the
LVGL vendoring — that pruning stays. This repo is the MIT copy NXP ships, and
it is what `#include <vg_lite.h>` resolves to for LVGL's VG_LITE draw unit
(with `LV_USE_VG_LITE_DRIVER` and `LV_USE_VG_LITE_THORVG` both 0). LVGL
therefore needs no source modification and nothing is un-pruned.

## What was taken

Vendored verbatim (one edit: the encoding transcode noted above):

- `inc/` — `vg_lite.h`
- `VGLite/` — `vg_lite.c`, `vg_lite_image.c`, `vg_lite_matrix.c`,
  `vg_lite_path.c`, `vg_lite_stroke.c`, plus `vg_lite_context.h`,
  `vg_lite_options.h`
- `VGLiteKernel/` — `vg_lite_kernel.[ch]`, `vg_lite_hal.h`, `vg_lite_hw.h`,
  `vg_lite_option.h`, `vg_lite_type.h`, `vg_lite_debug.h`

15 files, all OS-agnostic. Verified:

```sh
grep -rl "FreeRTOS\|xSemaphore\|vTaskDelay\|pdTRUE" VGLite VGLiteKernel inc | wc -l   # -> 0
```

★ Note `vg_lite_hal.h` moved from `inc/` to `VGLiteKernel/` in this release, and
`vg_lite_text.h` is gone (VGLite-native text now lives elsewhere upstream). Both
matter to include paths.

## What was NOT taken, and why

- **`VGLite/rtos/` and `VGLiteKernel/rtos/`** — upstream's FreeRTOS port layer
  (`vg_lite_os.c`, 27 FreeRTOS references; `vg_lite_hal.c`, 3). This tree is
  bare-metal (`LV_USE_OS 0`), so `port/baremetal/` replaces them. These are the
  ONLY files this repo reimplements.

  ★ **One header from `rtos/` IS vendored, unmodified:
  `port/baremetal/vg_lite_os.h`.** It is not FreeRTOS-specific (zero FreeRTOS
  references) — it is the API contract the port implements, and the vendored
  headers include it directly (`VGLiteKernel/vg_lite_kernel.h:30`,
  `inc/vg_lite_hal.h:34`). Deleting `rtos/` wholesale breaks those includes;
  this was caught before the first build. It carries no per-file licence
  notice, like several upstream headers, and is covered by the repo `LICENSE`
  (MIT); it contains no copyleft text.
- **`font/`** (mcufont) — VGLite's own text rasteriser. LVGL renders glyphs as
  paths through its own font engine (`lv_draw_vg_lite_label.c`), so it is dead
  weight here. Note `inc/vg_lite_text.h` was kept for completeness but has no
  implementation in this repo: **including it will not link.** Vendor `font/`
  if VGLite-native text is ever wanted.
- `ChangeLogKSDK.txt` — upstream release notes, not source.

## ★ The `_BAREMETAL` switch is not a bare-metal port

`VGLiteKernel/rtos/vg_lite_platform.h` upstream defines `_BAREMETAL 0`, and it
is tempting to think setting it to 1 does this job. It does not — under that
switch the upstream HAL:

- hardcodes an FPGA register base (`registerMemBase = 0x43c80000`),
- calls Xilinx's `Xil_DCacheFlush()`,
- **still** declares `int_queue` as a FreeRTOS `xSemaphoreHandle`.

It is Xilinx FPGA bring-up scaffolding, not a Cortex-M port. Do not enable it.

## Re-vendoring

1. Re-copy `inc/`, `VGLite/`, `VGLiteKernel/` from upstream.
2. Delete both `rtos/` trees.
3. Re-run both greps above; each must return 0.
4. Diff `port/baremetal/` against the new `VGLite/vg_lite.c` and
   `VGLiteKernel/vg_lite_kernel.c` for changed `vg_lite_os_*` / `vg_lite_hal_*`
   signatures — the port implements those contracts and drift is a link error
   at best, a hang at worst.
5. **Check the encoding of every vendored source**, before the licence survey:

```sh
for f in $(git ls-files '*.c' '*.h'); do
  iconv -f UTF-8 -t UTF-8 "$f" >/dev/null 2>&1 || echo "NON-UTF8 (the audit will SKIP this): $f"
done
```
   Transcode anything it prints. A non-UTF-8 source is invisible to `grep -I`,
   so it passes the copyleft sweep without ever being read.
6. **Re-run the per-file licence survey above** and reconcile `NOTICE` with what
   it prints. Step 3's greps only prove *no copyleft*; they say nothing about a
   file arriving under a third permissive licence, which is exactly how the
   Apache-2.0 pair went unnoticed. A new licence found here is not necessarily a
   problem — it is a `NOTICE` entry that must be written before the push.
