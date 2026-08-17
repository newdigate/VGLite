# Vendoring

Upstream: NXP `gs-vglite_examples_rt1170`, `common/vglite/` (local reference
checkout at `~/Development/gs-vglite_examples_rt1170`). Vivante VGLite,
`VGLITE_HEADER_VERSION 6`, `VGLITE_VERSION_2_0`.

## Licence

**MIT, with two Apache-2.0 files.** No copyleft. Verified before vendoring,
2026-08-16:

```sh
grep -rl "GNU General Public\|GNU Lesser\|Mozilla Public" . | wc -l   # -> 0
```

`VGLiteKernel/vg_lite_kernel.h` carries "The MIT License (MIT), Copyright (c)
2014 - 2020 Vivante Corporation"; `inc/vg_lite.h` the same for 2012 - 2020.

★ **CORRECTION 2026-08-17 — this section said "MIT throughout", and that was
wrong.** A per-file survey (run when the repo went public, and worth re-running
on any re-vendor) found **`VGLite/vg_lite_flat.c` and `VGLite/vg_lite_flat.h`
are Apache-2.0**, not MIT — "Copyright Raph Levien 2022 / Nicolas Silva 2022 /
NXP 2022". Ten source files are MIT; those two are not; the four
`port/baremetal/` files are this tree's own and take the repo's MIT terms.

They are **not** dead code that could simply be excluded: `vg_lite.c` includes
`vg_lite_flat.h` and calls `_flatten_quad_bezier()` / `_flatten_cubic_bezier()`
from its stroke-conversion path, so they compile and link into firmware.
Apache-2.0 is permissive and compatible here, so nothing about the vendoring
decision changes — but the claim did, and a licence claim that is casually
wrong is worth less than no claim.

★ **Note how this got missed for a day**, because the same blind spot will
recur: the rt1176-evkb licence audit greps for **copyleft** (GPL/LGPL/MPL), and
Apache-2.0 is not copyleft. The audit was doing exactly its job and passing
correctly. "The audit is green" answers *is there copyleft here*, not *is this
repo the licence its README says it is*. The second question needs a per-file
survey:

```sh
for f in $(git ls-files '*.c' '*.h'); do
  printf '%-46s %s\n' "$f" "$(grep -m1 -o 'MIT License\|Licensed under the Apache License' "$f")"
done
```

The full statement lives in `NOTICE`; `LICENSE` is kept as plain MIT so
automated licence detection still identifies the repo (an exception block
inside it reads as a modified licence and detection drops to NOASSERTION --
measured, not guessed).

★ **This is NOT the copy bundled with LVGL.** LVGL's
`src/libs/vg_lite_driver/` carries a dual-licensed VGLiteKernel that trips
`tools/license-audit.sh` Part 1 on 5 files, and it is correctly pruned in the
LVGL vendoring — that pruning stays. This repo is the MIT copy NXP ships, and
it is what `#include <vg_lite.h>` resolves to for LVGL's VG_LITE draw unit
(with `LV_USE_VG_LITE_DRIVER` and `LV_USE_VG_LITE_THORVG` both 0). LVGL
therefore needs no source modification and nothing is un-pruned.

## What was taken

Vendored verbatim, unmodified:

- `inc/` — `vg_lite.h`, `vg_lite_hal.h`, `vg_lite_text.h`
- `VGLite/` — `vg_lite.c`, `vg_lite_flat.[ch]`, `vg_lite_image.c`,
  `vg_lite_matrix.c`, `vg_lite_path.c`
- `VGLiteKernel/` — `vg_lite_kernel.[ch]`, `vg_lite_hw.h`

6 `.c` and 6 `.h`, all OS-agnostic. Verified:

```sh
grep -rl "FreeRTOS\|xSemaphore\|vTaskDelay\|pdTRUE" VGLite VGLiteKernel inc | wc -l   # -> 0
```

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
5. **Re-run the per-file licence survey above** and reconcile `NOTICE` with what
   it prints. Step 3's greps only prove *no copyleft*; they say nothing about a
   file arriving under a third permissive licence, which is exactly how the
   Apache-2.0 pair went unnoticed. A new licence found here is not necessarily a
   problem — it is a `NOTICE` entry that must be written before the push.
