/* vg_lite_os.c - bare-metal OS layer for VGLite on the i.MX RT1176 (GC355).
 * Copyright (c) 2026 Nicholas Newdigate
 * SPDX-License-Identifier: MIT
 *
 * Replaces upstream's VGLite/rtos/vg_lite_os.c (FreeRTOS). This tree is
 * bare-metal (LV_USE_OS 0), single-threaded, and drives the GPU from loop().
 *
 * ★ BUILT WITH -DVG_DRIVER_SINGLE_THREAD. That switch is honoured by the
 * vendored core (132 references in VGLite/vg_lite.c, 16 in
 * VGLiteKernel/vg_lite_kernel.c) and compiles out the TLS, mutex, async-event
 * and command-queue-task machinery -- which is exactly the machinery that has
 * no meaning without an RTOS. It reduces this layer from 19 functions to the
 * 7 below. If someone ever drops the define, this file will fail to link
 * against the multi-threaded entry points rather than silently misbehave.
 *
 * ★ NOT derived from upstream's `_BAREMETAL` path, which is Xilinx FPGA
 * scaffolding: it hardcodes an FPGA register base, calls Xil_DCacheFlush(),
 * still declares int_queue as a FreeRTOS xSemaphoreHandle, and -- in
 * vg_lite_os_wait_interrupt() -- spins in an UNBOUNDED `while (int_status==0)`
 * loop while dereferencing a `value` pointer it has just discarded with
 * `(void)value`. See VENDORING.md.
 */

#include <stdint.h>
#include <stdlib.h>
#include <Arduino.h>

/* Same ordering rule as vg_lite_hal.c: platform, kernel, hal, hw. Under
 * HEADER_VERSION 7 vg_lite_hal.h's prototypes need types that only arrive via
 * vg_lite_platform.h, so including hal.h first puts the errors inside the
 * vendored header and hides the fact that the cause is local. */
#include "vg_lite_platform.h"
#include "vg_lite_kernel.h"
#include "vg_lite_hal.h"
#include "vg_lite_hw.h"
#include "vg_lite_os.h"

/* Bit 31 of the interrupt status is an AXI bus error. Upstream defines this
 * in its rtos/ file, which this tree does not vendor, so it is restated here
 * verbatim (VGLite/rtos/vg_lite_os.c:13). */
#define IS_AXI_BUS_ERR(x) ((x) & (1U << 31))

/* How long a completion wait may block before it gives up and NAMES itself.
 * Generous: a full-screen path fill is milliseconds, so anything approaching
 * this is a fault, not slowness. */
#ifndef VGLITE_WAIT_TIMEOUT_MS
#define VGLITE_WAIT_TIMEOUT_MS 2000u
#endif

/* Interrupt flags accumulated by the ISR, consumed by wait_interrupt. */
static volatile uint32_t s_int_flags;

/* ★ Bounded-wait diagnostics. A GPU that never signals must name itself in
 * the UART rather than presenting as a dead board -- the same contract the
 * LCDIFv2 vsync fence uses (VSYNC_TIMEOUTS). Gates assert this is 0. */
static volatile uint32_t s_wait_timeouts;

uint32_t vg_lite_os_wait_timeouts(void)
{
    return s_wait_timeouts;
}

/* ★ How many times the GPU2D ISR actually ran.
 *
 * Load-bearing diagnostic, not decoration. A wait that returns "success" is
 * ambiguous on its own: it can mean the GPU signalled, or it can mean the wait
 * consumed a STALE flag left by an earlier operation and returned without the
 * GPU having done anything. Those two are indistinguishable from the outside
 * and produce very different pixels. Comparing this count against the number
 * of submits tells them apart. */
static volatile uint32_t s_irq_count;

uint32_t vg_lite_os_irq_count(void)
{
    return s_irq_count;
}

void *vg_lite_os_malloc(uint32_t size)
{
    return malloc(size);
}

void vg_lite_os_free(void *memory)
{
    free(memory);
}

void vg_lite_os_sleep(uint32_t msec)
{
    delay(msec);
}

int32_t vg_lite_os_initialize(void)
{
    s_int_flags = 0;
    return VG_LITE_SUCCESS;
}

void vg_lite_os_deinitialize(void)
{
    s_int_flags = 0;
}

/* Called from the GPU2D interrupt (IRQ 60), attached by the HAL.
 *
 * Reading VG_LITE_INTR_STATUS acknowledges the interrupt in hardware, so this
 * must read it exactly once and keep the value -- re-reading to "check" would
 * lose it. Flags are OR-accumulated because several completions can land
 * before the foreground gets to look. */
void vg_lite_os_IRQHandler(void)
{
    const uint32_t flags = vg_lite_hal_peek(VG_LITE_INTR_STATUS);

    s_irq_count++;
    if (flags) {
        s_int_flags |= flags;
    }
}

/* Wait for a GPU interrupt whose flags intersect `mask`.
 *
 * Returns 1 and stores the masked flags in *value when the GPU signals;
 * returns 0 on timeout, having counted it.
 *
 * ★ The wait is BOUNDED. Upstream's bare-metal variant spins forever, which
 * turns a GPU that never signals into a dead board with no diagnosis. Here a
 * stuck GPU costs VGLITE_WAIT_TIMEOUT_MS and then says so, exactly like the
 * panel's vsync fence.
 *
 * Note this polls rather than sleeping: there is no scheduler to yield to, and
 * the ISR only sets a flag. millis() is safe to poll here because the SysTick
 * interrupt is a higher priority than nothing else we hold off. */
int32_t vg_lite_os_wait_interrupt(uint32_t timeout, uint32_t mask, uint32_t *value)
{
    const uint32_t limit = (timeout == 0u || timeout > VGLITE_WAIT_TIMEOUT_MS)
                           ? VGLITE_WAIT_TIMEOUT_MS : timeout;
    const uint32_t t0 = millis();

    for (;;) {
        const uint32_t flags = s_int_flags;

        if (flags & mask) {
            /* ★ FLAG ALONE IS NOT COMPLETION. The ISR OR-accumulates, so a
             * late or extra interrupt from an earlier submission leaves a
             * flag a later wait would consume instantly -- and a frame the
             * driver split across several submissions then "finishes" while
             * the GPU is still executing. Found 2026-08-28 by the rotary
             * widget's delta equality guard: once the gpu-well work grew the
             * per-frame command stream, EVERY frame's checksum varied per
             * boot (ten boots, ten grid sums), rk_gpu_err=0 throughout.
             * So on top of the flag, require the front end to actually be
             * IDLE (AQHiIdle bit 0 -- the Phase-1 bring-up diagnostic).
             * Sound for this port's synchronous single-context model, where
             * every wait means "the queued work is done"; a future
             * overlapped-submission design must revisit this together with
             * the whole flag scheme. */
            if ((vg_lite_hal_peek(VG_LITE_HW_IDLE) & 0x1u) == 0u) {
                if ((millis() - t0) >= limit) {
                    s_wait_timeouts++;
                    if (value != NULL) {
                        *value = 0;
                    }
                    return 0;
                }
                continue;               /* signalled but still executing */
            }
            if (value != NULL) {
                *value = flags & mask;
                if (IS_AXI_BUS_ERR(*value)) {
                    /* The GPU reported a bus error. Surface it in the returned
                     * value -- the caller in vg_lite_kernel.c inspects it --
                     * rather than swallowing it here. */
                }
            }
            s_int_flags = 0;
            return 1;
        }

        if ((millis() - t0) >= limit) {
            s_wait_timeouts++;
            if (value != NULL) {
                *value = 0;
            }
            return 0;
        }
    }
}
