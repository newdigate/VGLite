# NXP oracle build — `evkmimxrt1170_08_BlitColor`

An out-of-tree GCC build of one of NXP's own MCUXpresso VGLite examples, for
use as a **reference oracle**: known-good NXP code driving the same GC355 on
the same board. When our bare-metal port and NXP's differ in behaviour, this
turns guesswork into a diff.

## Why this example

`08_BlitColor` is one of only two examples in the set (with `12_BlitRect`) that
does **not** start a FreeRTOS scheduler, and a blit is the simplest GPU write
there is. It still `#include`s FreeRTOS.h for config/heap and the VGLite driver
uses its rtos port layer, so FreeRTOS sources are compiled in.

## Usage

Needs the reference checkout at `~/Development/gs-vglite_examples_rt1170`
(read-only — nothing is written back into it).

```sh
cmake -B build && cmake --build build -j 4
# NOTE: LinkServer `run` CANNOT start this image -- it has loadable sections in
# SDRAM (0x80000000) and NCACHE (0x83000000) that the run-stub cannot place
# ("Failed to load application (stub terminated with return code 1)"). Our own
# images are XIP-only, which is why `run` works for them.
LinkServer flash MIMXRT1176:MIMXRT1170-EVKB load build/blitcolor.elf --erase-all
# then press SW4 (RESET) on the board.
```

## What had to be supplied for a non-IDE build

Recorded because none of it is obvious and all of it cost time:

1. **FreeRTOS**: kernel + `portable/GCC/ARM_CM4F`, and exactly ONE MemMang heap
   (`heap_4.c`) — globbing MemMang defines `pvPortMalloc` five times.
2. **`common/component`** (serial_manager, uart, lists) and
   **`common/video`** (`fsl_dc_fb*`), neither of which the example's own
   directory hints at.
3. `SERIAL_PORT_TYPE_UART=1`, or `fsl_component_serial_manager.h` `#error`s.
4. `CUSTOM_VGLITE_MEMORY_CONFIG=1` — the example `#error`s without it and
   supplies `vglite_heap[]`/`vglite_heap_size` itself.
5. **Patched linker scripts** (copied into `ld/`, upstream left untouched):
   - dropped `libcr_newlib_nohost.a`, an MCUXpresso IDE library we do not have;
   - added `__NCACHE_REGION_START/_SIZE`, which `board.c`'s `BOARD_ConfigMPU()`
     reads and MCUXpresso normally injects;
   - `PROVIDE(Reset_Handler = ResetISR)` — the XIP boot header takes the IVT
     entry from `Reset_Handler`, while this startup file spells it `ResetISR`.
