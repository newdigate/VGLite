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
#include <stdarg.h>
#include <stdio.h>
#include <Arduino.h>
#include <core_pins.h>

/* ★ ORDER IS LOAD-BEARING under HEADER_VERSION 7, and it is the order the
 * vendored vg_lite_kernel.c itself uses: platform first, then kernel, then hal.
 * vg_lite_hal.h now declares prototypes in terms of vg_lite_gpu_execute_state_t,
 * vg_lite_cache_op_t and vg_lite_kernel_*_t, all of which arrive via
 * vg_lite_platform.h -> vg_lite_type.h. Include hal.h first (as the v6 port
 * did, when it was harmless) and the errors land INSIDE the vendored header,
 * which reads like a bad vendor drop rather than a local include-order bug. */
#include "vg_lite_platform.h"
#include "vg_lite_kernel.h"
#include "vg_lite_hal.h"
#include "vg_lite_hw.h"
#include "vg_lite_os.h"

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
#define VGL_GPU2D_MUX_OSC_24M       1u

/* Clock source. SYS_PLL3 at 480 MHz (mux 6, DIV 0) is inside CLOCK_ROOT68's
 * 500 MHz table maximum and needs no PLL bring-up (PLL3 is locked at boot).
 *
 * ★ MEASURED, so nobody repeats it: clock rate is NOT the cause of the
 * open no-pixels defect below. Running this root at OSC_24M (mux 1) instead --
 * unambiguously in spec, 20x slower -- behaves IDENTICALLY: vg_lite_init()
 * still succeeds and its command buffers still complete, and every submit
 * after it still times out with no completion interrupt. The only difference
 * was more interrupts during init (5 vs 2). Override at build time if you want
 * to re-test the axis. */
#ifndef VGLITE_GPU2D_MUX
#define VGLITE_GPU2D_MUX            VGL_GPU2D_MUX_SYS_PLL3
#endif
#ifndef VGLITE_GPU2D_DIV
#define VGLITE_GPU2D_DIV            0u
#endif

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

/* ★ THE HEADER IS PADDED TO THE ALIGNMENT ON PURPOSE.
 *
 * The payload starts immediately after the header, so if the header were its
 * natural 12 bytes every payload would land 12 bytes past a 64-byte boundary.
 * Vivante requires 64-byte-aligned command buffers; a misaligned one does NOT
 * fail cleanly -- the GPU starts executing and HANGS IN THE FRONT END.
 *
 * Measured on silicon before the fix: allocations came back at ...0x470
 * (0x470 % 64 == 48), and after a submit AQHiIdle (0x004) read 0x7FFFFFFE --
 * bit 0, the Front End, BUSY forever -- with no completion interrupt and no
 * pixels. Every driver call still returned SUCCESS.
 *
 * Padding the header to VGL_ALIGN keeps both the header and the payload
 * 64-byte aligned, given a 64-byte-aligned pool base and sizes rounded up to
 * VGL_ALIGN. */
typedef struct vgl_node {
    struct vgl_node *next;
    uint32_t         size;   /* payload bytes, excluding this header */
    uint32_t         used;
    uint8_t          _pad[VGL_ALIGN - (sizeof(void *) + 2u * sizeof(uint32_t))];
} vgl_node_t;

_Static_assert(sizeof(vgl_node_t) == VGL_ALIGN,
               "node header must be exactly VGL_ALIGN so payloads stay aligned");

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

/* --- Display-mix reset: DELIBERATELY NOT DONE HERE -----------------------
 * The GC355 sits in the display mix reset domain. EVERY NXP MCUXpresso VGLite
 * example for this board does this before touching the GPU
 * (`BOARD_ResetDisplayMix()` in SimplePath.c, Glyphs.c, RadialGradient.c,
 * Font.c ...), with the comment: "Reset the displaymix, otherwise during
 * debugging, the debugger may not reset the display, then the behavior is not
 * right."
 *
 * ★ That is exactly this tree's situation -- images are loaded and started by
 * LinkServer, i.e. under a debugger, on every run. Without this the GPU's AHB
 * slave answers (the chip ID reads back 0x355) and vg_lite_init()'s command
 * buffers complete, but no later submit ever finishes and NOT ONE PIXEL is
 * written -- measured.
 *
 * ★ BUT IT MUST NOT HAPPEN IN THIS DRIVER, and doing it here was a real bug.
 * The display mix contains the LCDIFv2 and MIPI-DSI as well as the GPU, so
 * resetting it from vg_lite_hal_initialize() -- which runs AFTER the
 * application has already brought the panel up -- wipes the panel's
 * configuration. Measured: the GPU rendered correctly (IRQs tracking submits,
 * TIMEOUTS=0, framebuffer checksum changing) while the RK055 stayed BLACK,
 * because nothing was scanning the buffer out any more.
 *
 * NXP does it in BOARD init, BEFORE the display is configured, which is the
 * only correct order. It is a board-level concern, not a GPU-driver one, so
 * this port does not do it at all; an application that needs it (e.g. because
 * a debugger left the display mix dirty) must do it before its Display.begin().
 * Measured separately: it does NOT affect GPU rendering either way.
 *
 * Registers, recorded so the knowledge is not lost (RM ch.25): SRC base
 * 0x40C04000; CTRL_DISPLAY at offset 0x224, bit 0 SW_RESET, SELF-CLEARING
 * (RM:121938-121947); STAT_DISPLAY at 0x230, read-only bit 0 = reset in
 * process (RM:122266). */
#define VGL_SRC_CTRL_DISPLAY        (*(volatile uint32_t *)0x40C04224u)
#define VGL_SRC_STAT_DISPLAY        (*(volatile uint32_t *)0x40C04230u)
#define VGL_SRC_SW_RESET            (1u << 0)
#define VGL_SRC_UNDER_RST           (1u << 0)

static void gpu2d_clocks_on(void)
{
    /* Route GPU2D_CLK_ROOT to SYS_PLL3 (480 MHz) before ungating, so the GPU
     * never sees a running clock it was not configured for. */
    VGL_CCM_CLOCK_ROOT68_CTRL = VGL_ROOT_MUX(VGLITE_GPU2D_MUX) | VGL_ROOT_DIV(VGLITE_GPU2D_DIV);
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

/* HEADER_VERSION 7 made this return void. Nothing is lost: both call sites in
 * the vendored kernel already discarded the value, and this implementation
 * always returned VG_LITE_SUCCESS -- clock-gating and NVIC attach have no
 * failure path to report. Init failures still surface where they can be acted
 * on: vg_lite_init() checks CHIPID/REVISION/CID/ECOID against the silicon and
 * returns VG_LITE_NOT_SUPPORT itself. */
void vg_lite_hal_initialize(void)
{
    gpu2d_clocks_on();

    attachInterruptVector((IRQ_NUMBER_t)VGLITE_RT1176_GPU2D_IRQ, vg_lite_IRQHandler);
    NVIC_ENABLE_IRQ(VGLITE_RT1176_GPU2D_IRQ);

    vg_lite_os_initialize();
}

void vg_lite_hal_deinitialize(void)
{
    NVIC_DISABLE_IRQ(VGLITE_RT1176_GPU2D_IRQ);
    vg_lite_os_deinitialize();
    /* Leave the clock gated off: nothing else on this part shares LPCG128. */
    VGL_CCM_LPCG128_DIRECT = 0u;
    vg_lite_hal_barrier();
}

/* HEADER_VERSION 7 added `pool` and `klogical`.
 *
 * pool selects among several video-memory heaps upstream; this port has ONE
 * flat contiguous pool, so it is clamped and ignored -- NXP's own FreeRTOS port
 * clamps out-of-range pools the same way rather than indexing blind.
 *
 * klogical is the kernel-side logical address. There is one address space here
 * and no MMU, so it equals the user logical address by construction. */
vg_lite_error_t vg_lite_hal_allocate_contiguous(unsigned long size,
                                                vg_lite_vidmem_pool_t pool,
                                                void **logical, void **klogical,
                                                uint32_t *physical, void **node)
{
    const uint32_t want = VGL_ALIGN_UP((uint32_t)size);

    (void)pool;   /* single flat pool -- see above */

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
        if (klogical) *klogical = payload;   /* one address space, no MMU */
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

/* Register externally-owned memory (e.g. a panel framebuffer the application
 * allocated) for GPU access.
 *
 * ★ THE RETURN VALUE IS A SUCCESS FLAG, NOT AN OPTIONAL HANDLE. The kernel's
 * do_map() (VGLiteKernel/vg_lite_kernel.c:879-887) fails the whole call with
 * VG_LITE_OUT_OF_RESOURCES when this returns NULL. An earlier revision here
 * returned NULL "because there is no MMU and nothing needs recording", which
 * made every vg_lite_map() fail -- and then, because nothing else checks it,
 * vg_lite_draw() and vg_lite_finish() still both reported SUCCESS while the
 * GPU wrote NOT ONE PIXEL. Measured on silicon: the framebuffer checksum came
 * back as exactly the all-zeros FNV.
 *
 * Translation is genuinely identity (no MMU in this path), so the handle only
 * has to be a distinct non-NULL token that unmap can accept. The logical
 * address serves: it is unique per mapping and needs no bookkeeping. */
/* HEADER_VERSION 7 replaced `size` with `bytes` and added `flags` and
 * `dma_buf_fd`. NXP's own port (VGLiteKernel/rtos/vg_lite_hal.c) casts all
 * three to void and ignores them; there is no dma-buf and no mapping mode to
 * honour on bare metal, so this does the same rather than inventing meaning. */
void *vg_lite_hal_map(uint32_t flags, uint32_t bytes, void *logical,
                      uint32_t physical, int32_t dma_buf_fd, uint32_t *gpu)
{
    (void)flags;
    (void)bytes;
    (void)dma_buf_fd;

    if (gpu) {
        *gpu = physical + s_gpu_mem_base;
    }
    /* Non-NULL required. Fall back to the physical address when the caller
     * supplied only that, so a mapping is never reported as a failure. */
    return (logical != NULL) ? logical : (void *)(uintptr_t)physical;
}

void vg_lite_hal_unmap(void *memory_handle)
{
    /* Nothing was allocated to describe the mapping, so nothing to release. */
    (void)memory_handle;
}

/* --- HEADER_VERSION 7 kernel-command handlers -------------------------------
 * Four entry points the newer kernel dispatches for VG_LITE_MAP_MEMORY,
 * UNMAP_MEMORY, CACHE and EXPORT_MEMORY. NXP's own port implements each in
 * 4-7 lines; on this part they are simpler still, and the reasons are worth
 * stating because each one is a place where copying another port's code would
 * be actively wrong. */

/* Memory is already CPU-visible: physical and logical coincide (identity, no
 * MMU), so mapping is just handing the address back. */
vg_lite_error_t vg_lite_hal_map_memory(vg_lite_kernel_map_memory_t *node)
{
    if (node == NULL) {
        return VG_LITE_INVALID_ARGUMENT;
    }
    node->logical = (void *)(uintptr_t)node->physical;
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_hal_unmap_memory(vg_lite_kernel_unmap_memory_t *node)
{
    (void)node;   /* nothing was mapped -- see above */
    return VG_LITE_SUCCESS;
}

/* ★ No cache maintenance, and this is a deliberate per-core fact rather than an
 * omission. The imxrt1176 core never writes SCB_CCR, so the D-cache is OFF and
 * CPU and GPU views of memory agree for free. Do NOT port the rt1062 handling
 * here: that core DOES enable the D-cache, and a buffer coherent on one board
 * is not on the other. See the consuming tree's CLAUDE.md. */
vg_lite_error_t vg_lite_hal_operation_cache(void *handle, vg_lite_cache_op_t cache_op)
{
    (void)handle;
    (void)cache_op;
    return VG_LITE_SUCCESS;
}

/* dma-buf export is a Linux concept; there is no fd namespace here. Report it
 * unsupported rather than returning SUCCESS with an unset fd -- a caller that
 * believed the success would use a garbage descriptor. */
/* HEADER_VERSION 7 tracks whether the GPU is considered running. Nothing in
 * this port acts on it -- completion is driven by the IRQ counter, not by a
 * cached state flag -- but the kernel calls it, so it must exist. Kept as a
 * stored value rather than an empty body so it is observable when debugging. */
static volatile vg_lite_gpu_execute_state_t s_gpu_state = VG_LITE_GPU_STOP;

void vg_lite_set_gpu_execute_state(vg_lite_gpu_execute_state_t state)
{
    s_gpu_state = state;
}

/* vg_lite_kernel_print / _hintmsg are macros onto these two. The driver only
 * calls them on error paths, and this port has a UART console, so send them
 * there rather than discarding: a silent driver was exactly what made Phase 1
 * expensive. */
void vg_lite_hal_print(char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void vg_lite_hal_trace(char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

/* Used by the ONERROR macro to name a status in its diagnostic. Enumerating
 * every code beats a %d: the whole reason this port keeps the driver's error
 * output is that Phase 1's failures were silent. */
const char *vg_lite_hal_Status2Name(vg_lite_error_t status)
{
    switch (status) {
    case VG_LITE_SUCCESS:            return "VG_LITE_SUCCESS";
    case VG_LITE_INVALID_ARGUMENT:   return "VG_LITE_INVALID_ARGUMENT";
    case VG_LITE_OUT_OF_MEMORY:      return "VG_LITE_OUT_OF_MEMORY";
    case VG_LITE_NO_CONTEXT:         return "VG_LITE_NO_CONTEXT";
    case VG_LITE_TIMEOUT:            return "VG_LITE_TIMEOUT";
    case VG_LITE_OUT_OF_RESOURCES:   return "VG_LITE_OUT_OF_RESOURCES";
    case VG_LITE_GENERIC_IO:         return "VG_LITE_GENERIC_IO";
    case VG_LITE_NOT_SUPPORT:        return "VG_LITE_NOT_SUPPORT";
    case VG_LITE_ALREADY_EXISTS:     return "VG_LITE_ALREADY_EXISTS";
    case VG_LITE_NOT_ALIGNED:        return "VG_LITE_NOT_ALIGNED";
    default:                         return "VG_LITE_UNKNOWN";
    }
}

vg_lite_error_t vg_lite_hal_memory_export(int32_t *fd)
{
    (void)fd;
    return VG_LITE_NOT_SUPPORT;
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
