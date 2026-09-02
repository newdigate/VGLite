# VGLite

Vivante VGLite vector-GPU driver for the i.MX RT1176's **GC355** (GPU2D at
`0x4180_0000`, IRQ 60, `GPU2D_CLK_ROOT`), vendored from NXP and ported to bare
metal for the rt1176-evkb tree.

MIT throughout (`LICENSE`); no copyleft anywhere. The Apache-2.0 exception this
file used to name, `VGLite/vg_lite_flat.{c,h}`, is gone — the SDK v26.06.00-LTS
re-vendor (`VGLITE_HEADER_VERSION 7`) does not ship those files at all.
`NOTICE` records that history and why a copyleft grep would not have caught it;
`VENDORING.md` records provenance and carries the per-file survey to re-run on
every re-vendor.

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

## Status

The port is done and the GC355 renders on silicon. Four consumers in the evkb
tree, in the order they were built:

- `display/vglite_probe` — the GPU-alive probe.
- `display/vglite_lvgl_test` — LVGL's `LV_USE_DRAW_VG_LITE` backend. It renders
  **correctly** but **not faster**: 2.45 fps against software's 2.83 on the same
  scene, CPU-bound in the backend's per-task path construction rather than in
  the GPU. Kept as the integration proof, not as the fast path.
- `SynthUI`'s rotary-knob and fader compositors — **direct `vg_lite` calls**
  behind a chip-ID probe, with cached paths and damage-bounded scissors. Both
  meet a 30 fps budget where the LVGL backend does not: the knob at 32.1 fps
  vsync-locked (~42 unfenced), the fader at 30.4. The reason is the point — the
  win came from not rebuilding paths per frame, which the LVGL draw unit cannot
  avoid.
- `display/vglite_conformance` — the conformance matrix behind the quirks
  reference below.

## Behaviour on the RT1176's GC355

This driver's API reports success for several things this silicon does not do.
`rt1176-evkb`'s `docs/gc355-vglite-quirks.md` is the reference — one row per
feature, each citing the `display/vglite_conformance` probe case that
establishes it.
