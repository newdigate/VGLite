/* vg_lite_platform.h - bare-metal platform contract for the RT1176 GC355.
 * Copyright (c) 2026 Nicholas Newdigate
 * SPDX-License-Identifier: MIT
 *
 * Replaces upstream VGLiteKernel/rtos/vg_lite_platform.h. Note the vendored
 * inc/vg_lite_hal.h includes this file directly, so it must stay on the
 * include path for any consumer of the driver.
 *
 * ★ Upstream's `_BAREMETAL` switch is NOT defined here and must not be: it is
 * Xilinx FPGA scaffolding (FPGA register base, Xil_DCacheFlush(), a FreeRTOS
 * semaphore handle, and an unbounded spin in wait_interrupt). See
 * VENDORING.md.
 */
#ifndef _VG_LITE_PLATFORM_H
#define _VG_LITE_PLATFORM_H

#include <stdint.h>
/* ★ stddef.h is load-bearing, not tidiness. The vendored VGLiteKernel/
 * vg_lite_kernel.c uses NULL but includes no C standard header for it -- under
 * upstream's FreeRTOS build it arrived via FreeRTOS.h. This header is the
 * FIRST include in that file (vg_lite_kernel.c:27) and is also pulled in by
 * inc/vg_lite_hal.h:33, so supplying NULL here fixes it without patching
 * vendored code. Remove this and the kernel fails with "'NULL' undeclared". */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Upstream contract, unchanged ---------------------------------------
 * A pure setter: stores the register window, the GPU-address bias and the
 * contiguous pool that every HAL function then reads. The application passes
 * these in so the port stays board-agnostic. */
void vg_lite_init_mem(uint32_t register_mem_base,
                      uint32_t gpu_mem_base,
                      volatile void *contiguous_mem_base,
                      uint32_t contiguous_mem_size);

/* GPU2D interrupt trampoline, attached by vg_lite_hal_initialize(). */
void vg_lite_IRQHandler(void);

/* --- This port's additions ---------------------------------------------- */

/* GC355 on the i.MX RT1176. Both values are cited in the evkb tree's
 * examples/display/vglite_probe/HARDWARE-NOTES.md:
 *   base  0x4180_0000, a 1 MB "GPU2D (Peripheral, AHB)" window   (RM:2108)
 *   IRQ   60, "GPU2D interrupt"                                  (RM:2975)
 * Pass the base to vg_lite_init_mem() rather than letting the HAL assume it. */
#define VGLITE_RT1176_REGISTER_BASE   0x41800000u
#define VGLITE_RT1176_GPU2D_IRQ       60

/* ★ Bounded-wait diagnostics. Every wait in this port has a deadline; when one
 * expires this counts it. A GPU that never signals therefore NAMES ITSELF in
 * the UART instead of presenting as a dead board -- the same contract the
 * LCDIFv2 vsync fence uses with VSYNC_TIMEOUTS. Gates assert it reads 0. */
uint32_t vg_lite_os_wait_timeouts(void);

/* ★ Presence check -- CALL THIS BEFORE vg_lite_init().
 *
 * vg_lite_init() assumes the GPU exists; on a part without one it does not
 * return an error, it SPINS. (Measured on QEMU, which has no GC355 model: the
 * probe printed PANEL_OK then went silent, no fault logged.) This reads the
 * GC355 chip-ID register with the clocks enabled and returns 0 when nothing
 * answers, so an application can choose a software path instead of hanging.
 *
 * It is what lets ONE binary run accelerated on silicon and fall back cleanly
 * under QEMU -- which is what the gate asserts. */
uint32_t vg_lite_hal_probe_chip_id(void);

#ifdef __cplusplus
}
#endif
#endif /* _VG_LITE_PLATFORM_H */
