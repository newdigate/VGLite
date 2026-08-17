# VGLite

Vivante VGLite vector-GPU driver for the i.MX RT1176's **GC355** (GPU2D at
`0x4180_0000`, IRQ 60, `GPU2D_CLK_ROOT`), vendored from NXP and ported to bare
metal for the rt1176-evkb tree.

MIT (`LICENSE`), except `VGLite/vg_lite_flat.{c,h}` which are Apache-2.0 — see
`NOTICE`. Both permissive; no copyleft anywhere. `VENDORING.md` records
provenance and how the licences were verified.

## Layout

- `inc/`, `VGLite/`, `VGLiteKernel/` — vendored verbatim, OS-agnostic.
- `port/baremetal/` — this tree's port: GPU2D clock/IRQ bring-up, a flat
  contiguous pool, and a **bounded** polled completion wait. Replaces
  upstream's FreeRTOS `rtos/` layer, which is not vendored.

## Consumers

LVGL's `LV_USE_DRAW_VG_LITE` backend compiles against this repo's `vg_lite.h`
via the `#include <vg_lite.h>` path (with `LV_USE_VG_LITE_DRIVER` and
`LV_USE_VG_LITE_THORVG` both 0), so LVGL needs no source modification and its
own dual-licensed bundled driver stays pruned.

## Why bare metal needs care here

The GPU signals completion by interrupt. Every wait in `port/baremetal/` is
bounded and increments a timeout counter (`vg_lite_os_wait_timeouts()`), so a
GPU that never signals **names itself** in the UART rather than presenting as a
dead board. Gates assert that counter is zero. This mirrors the LCDIFv2 vsync
fence's `VSYNC_TIMEOUTS` contract in the LVGL port.

Cache maintenance is deliberately absent: the `imxrt1176` core never enables
the D-cache, so CPU and GPU views of memory agree. Do not port the rt1062
cache handling here.

Status: bare-metal port + GPU-alive probe (`display/vglite_probe` in the evkb
tree). LVGL draw-unit integration is Phase 2.
