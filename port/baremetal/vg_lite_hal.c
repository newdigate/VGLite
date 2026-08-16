/* vg_lite_hal.c - bare-metal HAL for VGLite on the i.MX RT1176 (GC355).
 * Copyright (c) 2026 Nicholas Newdigate
 * SPDX-License-Identifier: MIT
 *
 * Replaces upstream's VGLiteKernel/rtos/vg_lite_hal.c (FreeRTOS). Built with
 * -DVG_DRIVER_SINGLE_THREAD, which also compiles out vg_lite_hal_submit() and
 * vg_lite_hal_wait() (both are guarded in inc/vg_lite_hal.h), leaving the 13
 * functions below.
 *
 * Every register address here is cited in the evkb tree's
 * examples/display/vglite_probe/HARDWARE-NOTES.md, which derives them from the
 * reference manual. Do not "tidy" a constant without re-reading that file: a
 * wrong value here is a hang, not a compile error.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <Arduino.h>
#include <core_pins.h>

#include "vg_lite_os.h"
#include "vg_lite_hal.h"
#include "vg_lite_kernel.h"
#include "vg_lite_hw.h"
#include "vg_lite_platform.h"

/* --- Clocks (HARDWARE-NOTES.md) -----------------------------------------
 * GPU2D consumes gpu2d_aclk, gpu2d_clk2x and gpu2d_hclk; all three share the
 * single gate LPCG128 (RM:83158-83170). Only gpu2d_clk2x needs a root
 * configured -- the bus clock root is already up from core startup.
 *
 * LPCGa_DIRECT = 0x40CC6000 + a*0x20 (RM:90202) -> LPCG128 at 0x40CC7000.
 * CLOCK_ROOT68_CONTROL is at CCM offset 0x2200 (RM:87106) -> 0x40CC2200.
 * DIV divides by DIVIDE+1 (RM:91484). Max 500 MHz (RM:85491).
 *
 * Source choice: MUX=110 SYS_PLL3_CLK, DIV=0 -> 480 MHz, just under the
 * ceiling. SYS_PLL3 is already LOCKED at 480 MHz before this runs -- see
 * teensy-cores/imxrt1176/startup.c:385-393 -- so no PLL bring-up is needed.
 * The reset default (MUX=000, OSC_RC_48M_DIV2) would run the GPU at ~24 MHz,
 * about 20x slower, so this MUST be set rather than left alone. */
#define VGL_CCM_LPCG128_DIRECT      (*(volatile uint32_t *)0x40CC7000u)
#define VGL_CCM_CLOCK_ROOT68_CTRL   (*(volatile uint32_t *)0x40CC2200u)
#define VGL_ROOT_MUX(x)             (((uint32_t)(x) << 8) & 0x700u)
#define VGL_ROOT_DIV(x)             (((uint32_t)(x) << 0) & 0x0FFu)
#define VGL_GPU2D_MUX_SYS_PLL3      6u

/* --- State set by vg_lite_init_mem() ------------------------------------
 * Upstream's contract is a pure setter storing four statics that every other
 * HAL function reads (.../rtos/vg_lite_hal.c:59-68). Honour it exactly: the
 * register base is PASSED IN, never hardcoded here, which is what keeps this
 * port board-agnostic. Upstream's own defaults are an RT500 address and an
 * FPGA address -- neither is right for the RT1176. */
static uint32_t         s_register_base;
static uint32_t         s_gpu_mem_base;
static volatile void   *s_pool_base;
static uint32_t         s_pool_size;

/* --- Contiguous pool ----------------------------------------------------
 * A first-fit allocator with in-pool node headers and forward coalescing on
 * free. VGLite takes its command and tessellation buffers at init and holds
 * them, but it can also allocate at runtime (paths, images), so a bump
 * allocator that cannot free would leak. GPU addresses are physical ==
 * logical here: this part has no MMU in the VGLite path, so map() is
 * identity. */
#define VGL_ALIGN            64u
#define VGL_ALIGN_UP(x)      (((x) + (VGL_ALIGN - 1u)) & ~(VGL_ALIGN - 1u))

typedef struct vgl_node {
    struct vgl_node *next;
    uint32_t         size;   /* payload bytes, excluding this header */
    uint32_t         used;
} vgl_node_t;

static vgl_node_t *s_head;

static void pool_reset(void)
{
    if (s_pool_base == NULL || s_pool_size <= sizeof(vgl_node_t)) {
        s_head = NULL;
        return;
    }
    s_head = (vgl_node_t *)(void *)s_pool_base;
    s_head->next = NULL;
    s_head->size = s_pool_size - (uint32_t)sizeof(vgl_node_t);
    s_head->used = 0u;
}

void vg_lite_init_mem(uint32_t register_mem_base,
                      uint32_t gpu_mem_base,
                      volatile void *contiguous_mem_base,
                      uint32_t contiguous_mem_size)
{
    s_register_base = register_mem_base;
    s_gpu_mem_base  = gpu_mem_base;
    s_pool_base     = contiguous_mem_base;
    s_pool_size     = contiguous_mem_size;
    pool_reset();
}

/* The GPU2D interrupt (IRQ 60) trampoline. The OS layer owns the flag state;
 * this only forwards. */
void vg_lite_IRQHandler(void)
{
    vg_lite_os_IRQHandler();
}

void vg_lite_hal_delay(uint32_t milliseconds)
{
    delay(milliseconds);
}

/* ★ No cache maintenance. The imxrt1176 core never writes SCB_CCR, so the
 * D-cache is off and CPU/GPU views of memory agree. A DSB is still needed to
 * order our register writes against the GPU's fetches. This is the OPPOSITE
 * of the rt1062 situation -- do not port that cache handling here. */
void vg_lite_hal_barrier(void)
{
    __asm volatile ("dsb" ::: "memory");
}

static void gpu2d_clocks_on(void)
{
    /* Route GPU2D_CLK_ROOT to SYS_PLL3 (480 MHz) before ungating, so the GPU
     * never sees a running clock it was not configured for. */
    VGL_CCM_CLOCK_ROOT68_CTRL = VGL_ROOT_MUX(VGL_GPU2D_MUX_SYS_PLL3) | VGL_ROOT_DIV(0);
    /* Ungate all three GPU2D clocks. Reset value is already 1, but state this
     * explicitly rather than depending on it by accident. */
    VGL_CCM_LPCG128_DIRECT = 1u;
    vg_lite_hal_barrier();
}

/* ★ Is the GC355 actually there?
 *
 * This exists because vg_lite_init() assumes the hardware exists: on a part
 * with no GPU it does not fail cleanly, it SPINS -- measured on QEMU, whose
 * mimxrt1170-evk machine has no GC355 model, where the probe printed PANEL_OK
 * and then went silent with no fault logged.
 *
 * So callers must ask first. Clocks are enabled here because a gated
 * peripheral reads back as zero, which would make a present GPU look absent.
 * Returns the chip ID, or 0 when nothing answers. 0xFFFFFFFF (floating bus)
 * is also reported as absent.
 *
 * This is what makes one binary serve both paths: accelerated on silicon,
 * software-rendered in QEMU, deterministically and without hanging. */
uint32_t vg_lite_hal_probe_chip_id(void)
{
    gpu2d_clocks_on();

    const uint32_t id = vg_lite_hal_peek(VG_LITE_HW_CHIP_ID);
    return (id == 0xFFFFFFFFu) ? 0u : id;
}

vg_lite_error_t vg_lite_hal_initialize(void)
{
    gpu2d_clocks_on();

    attachInterruptVector((IRQ_NUMBER_t)VGLITE_RT1176_GPU2D_IRQ, vg_lite_IRQHandler);
    NVIC_ENABLE_IRQ(VGLITE_RT1176_GPU2D_IRQ);

    vg_lite_os_initialize();
    return VG_LITE_SUCCESS;
}

void vg_lite_hal_deinitialize(void)
{
    NVIC_DISABLE_IRQ(VGLITE_RT1176_GPU2D_IRQ);
    vg_lite_os_deinitialize();
    /* Leave the clock gated off: nothing else on this part shares LPCG128. */
    VGL_CCM_LPCG128_DIRECT = 0u;
    vg_lite_hal_barrier();
}

vg_lite_error_t vg_lite_hal_allocate_contiguous(unsigned long size, void **logical,
                                                uint32_t *physical, void **node)
{
    const uint32_t want = VGL_ALIGN_UP((uint32_t)size);

    for (vgl_node_t *n = s_head; n != NULL; n = n->next) {
        if (n->used || n->size < want) {
            continue;
        }
        /* Split when the remainder can hold a header plus a useful payload. */
        if (n->size >= want + sizeof(vgl_node_t) + VGL_ALIGN) {
            vgl_node_t *rest =
                (vgl_node_t *)(void *)((uint8_t *)n + sizeof(vgl_node_t) + want);
            rest->next = n->next;
            rest->size = n->size - want - (uint32_t)sizeof(vgl_node_t);
            rest->used = 0u;
            n->next = rest;
            n->size = want;
        }
        n->used = 1u;

        uint8_t *payload = (uint8_t *)n + sizeof(vgl_node_t);
        if (logical)  *logical  = payload;
        if (physical) *physical = (uint32_t)(uintptr_t)payload + s_gpu_mem_base;
        if (node)     *node     = n;
        return VG_LITE_SUCCESS;
    }
    return VG_LITE_OUT_OF_MEMORY;
}

void vg_lite_hal_free_contiguous(void *memory_handle)
{
    vgl_node_t *n = (vgl_node_t *)memory_handle;

    if (n == NULL) {
        return;
    }
    n->used = 0u;
    /* Forward-coalesce so repeated alloc/free of similar sizes does not
     * fragment the pool into unusable slivers. */
    while (n->next != NULL && !n->next->used) {
        n->size += (uint32_t)sizeof(vgl_node_t) + n->next->size;
        n->next  = n->next->next;
    }
}

void vg_lite_hal_free_os_heap(void)
{
    pool_reset();
}

void *vg_lite_hal_map(unsigned long size, void *logical, uint32_t physical, uint32_t *gpu)
{
    (void)size;
    (void)logical;
    /* Identity: no MMU in this path, so the GPU address is the physical one. */
    if (gpu) *gpu = physical;
    return NULL;
}

void vg_lite_hal_unmap(void *memory_handle)
{
    (void)memory_handle;
}

uint32_t vg_lite_hal_peek(uint32_t address)
{
    return *(volatile uint32_t *)(uintptr_t)(s_register_base + address);
}

void vg_lite_hal_poke(uint32_t address, uint32_t data)
{
    *(volatile uint32_t *)(uintptr_t)(s_register_base + address) = data;
}

vg_lite_error_t vg_lite_hal_query_mem(vg_lite_kernel_mem_t *mem)
{
    uint32_t freebytes = 0u;

    if (mem == NULL) {
        return VG_LITE_INVALID_ARGUMENT;
    }
    for (const vgl_node_t *n = s_head; n != NULL; n = n->next) {
        if (!n->used) {
            freebytes += n->size;
        }
    }
    mem->bytes = freebytes;
    return VG_LITE_SUCCESS;
}

int32_t vg_lite_hal_wait_interrupt(uint32_t timeout, uint32_t mask, uint32_t *value)
{
    return vg_lite_os_wait_interrupt(timeout, mask, value);
}
