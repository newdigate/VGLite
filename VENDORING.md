# Vendoring

Upstream: NXP `gs-vglite_examples_rt1170`, `common/vglite/` (local reference
checkout at `~/Development/gs-vglite_examples_rt1170`). Vivante VGLite,
`VGLITE_HEADER_VERSION 6`, `VGLITE_VERSION_2_0`.

## Licence

**MIT throughout.** Verified before vendoring, 2026-08-16:

```sh
grep -rl "GNU General Public\|GNU Lesser\|Mozilla Public" . | wc -l   # -> 0
```

`VGLiteKernel/vg_lite_kernel.h` carries "The MIT License (MIT), Copyright (c)
2014 - 2020 Vivante Corporation"; `inc/vg_lite.h` the same for 2012 - 2020.

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
