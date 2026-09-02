/* vglite_guard.h - make the GC355 defects we MEASURED unrepresentable.
 * Copyright (c) 2026 Nicholas Newdigate
 * SPDX-License-Identifier: MIT
 *
 * Our port code, never the vendored driver.
 *
 * ★ IT GUARDS ONLY WHAT THE PROBE CONFIRMED, and that is a constraint rather
 * than a style note. This header was written LAST on purpose: writing it
 * first would have encoded the beliefs the conformance probe existed to test,
 * and three of those beliefs did not survive contact with silicon. Every rule
 * below cites the `display/vglite_conformance` case that establishes it. A
 * rule without a case does not belong here.
 *
 * ------------------------------------------------------------------------
 * WHAT IS MEASURED (rt1176-evkb, docs/gc355-vglite-quirks.md)
 *
 *   * DISJOINT contours in one path are DROPPED. Four bars in one path render
 *     as one (`path/multi-contour-disjoint`, runs=1 of 4); two bars fail
 *     exactly as four do (`path/two-disjoint-bars`, runs=1 of 2).
 *   * NESTED contours render structurally correctly but NOT
 *     DETERMINISTICALLY (`path/four-nested-rings` read repeat=same on one boot
 *     and repeat=differs on the next; `path/evenodd-vs-nonzero` differs on
 *     every boot on record). Correct-but-nondeterministic is unsafe for a
 *     delta-rendering compositor -- it is exactly how NEW-20's winding-2 track
 *     defect presented.
 *   * HOLE-CUTTING renders MIS-COVER in both directions -- two nested
 *     contours draw 769 px too FEW (`path/two-contour-ring-nonzero`,
 *     cover=short:769), four draw ~1150 too MANY (stray). Structure alone said
 *     the picture was right; only a coverage check caught it.
 *   * ONE CONTOUR PER PATH IS EXACT. The same ring built as two
 *     single-contour paths and two draws measures fill=5376 EXACTLY
 *     (`path/two-draws-ring`), beside a single-path version of the identical
 *     ring that is 769 px short. So the rule below is measured against its own
 *     counterexample rather than assumed.
 *   * UNTERMINATED PATH DATA HANGS THE VIVANTE FRONT END while every
 *     vg_lite_* call keeps returning VG_LITE_SUCCESS (Phase 1). This is why a
 *     trailing VLC_OP_END is required rather than merely expected: the failure
 *     mode is a hang with a clean API, which no return-code check can see.
 *
 * The MECHANISM is NOT identified. "Disjoint" describes the geometry, not why
 * the tessellator drops it. A padded CLOSE slot (0x01010101, NXP's own
 * CHIPID==0x355 workaround) also works -- but that says a DIFFERENT
 * construction is viable, not that this one is unnecessary. Keep the
 * conservative rule.
 *
 * ------------------------------------------------------------------------
 * ★ WHAT THIS HEADER DELIBERATELY DOES NOT DO
 *
 * NO GRADIENT HELPERS. The design spec conditioned them on "if the probe
 * confirms [the gradient API] unusable for moving geometry". THE PROBE NEVER
 * TESTED GRADIENTS -- Phase 2 was redirected to colour and blend once scoping
 * found the matrix had never exercised the blend mode production actually
 * uses. The gradient claims in the quirks doc come from READING NXP's source,
 * not from a boot. Encoding them here would put an unmeasured belief into the
 * one layer whose ordering exists to prevent that. This is a recorded GAP: if
 * a later phase probes gradients, helpers belong here and not before.
 *
 * NO COLOUR HELPER. The premultiply that SRC_OVER requires on this silicon
 * (`color/premultiplied-srcover`: the hardware implements S + D*(1-Sa)) lives
 * in SynthUI's `src/synthui_fader_color.h`, where it is host-testable with NO
 * VGLite dependency -- 69017 checks. Moving it here would make that test
 * depend on the driver; duplicating it would create two copies of a measured
 * constant. Pointed at, not absorbed.
 *
 * ------------------------------------------------------------------------
 * LAYOUT. The PURE half (the validator) needs only <stdint.h>, so a host test
 * can compile it with no driver present. That split is not tidiness: NO GATE
 * IN THIS TREE CAN SEE GPU CODE -- every QEMU gate runs the software engine,
 * and GPU goldens live only in hand-pressed hardware transcripts. Pure,
 * host-tested logic is the only automated coverage this layer can have.
 * Define VGLITE_GUARD_NO_DRIVER to omit the half that needs vg_lite.h.
 */
#ifndef VGLITE_GUARD_H
#define VGLITE_GUARD_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- opcode values ------------------------------------------------------
 * Mirrored from vg_lite.h so the validator compiles with no driver. They are
 * ABI, not policy: the hardware's own path encoding. Guarded rather than
 * redefined so including vg_lite.h first stays harmless. */
#ifndef VLC_OP_END
#define VLC_OP_END      0x00
#endif
#ifndef VLC_OP_CLOSE
#define VLC_OP_CLOSE    0x01
#endif
#ifndef VLC_OP_MOVE
#define VLC_OP_MOVE     0x02
#endif
#ifndef VLC_OP_LINE
#define VLC_OP_LINE     0x04
#endif
#ifndef VLC_OP_QUAD
#define VLC_OP_QUAD     0x06
#endif
#ifndef VLC_OP_CUBIC
#define VLC_OP_CUBIC    0x08
#endif

typedef enum {
    VGLITE_GUARD_OK = 0,
    VGLITE_GUARD_EMPTY,        /* no words at all */
    VGLITE_GUARD_NO_MOVE,      /* no contour was ever opened */
    VGLITE_GUARD_MULTI_MOVE,   /* ★ THE MEASURED DEFECT: >1 contour in a path */
    VGLITE_GUARD_TRUNCATED,    /* an opcode's operands run past the end */
    VGLITE_GUARD_NO_END,       /* data ends without VLC_OP_END */
    VGLITE_GUARD_TRAILING,     /* words follow the VLC_OP_END */
    VGLITE_GUARD_BAD_OPCODE    /* not a VLC opcode we emit */
} vglite_guard_status_t;

/* Operand count for one opcode, or -1 if it is not an opcode we emit.
 * An opcode occupies ONE BYTE at the BASE of a format-width slot -- the
 * caller passes S32 words, so the value is the word itself. (Getting this
 * wrong is not academic: an FP32 path array written as (float)VLC_OP_MOVE
 * begins with byte 0x00, which IS VLC_OP_END, and dies at slot 0.) */
static inline int vglite_guard_operands(int32_t op)
{
    switch (op) {
    case VLC_OP_END:
    case VLC_OP_CLOSE: return 0;
    case VLC_OP_MOVE:
    case VLC_OP_LINE:  return 2;
    case VLC_OP_QUAD:  return 4;
    case VLC_OP_CUBIC: return 6;
    default:           return -1;
    }
}

/* Walk `n` S32 path words and report the FIRST thing wrong with them.
 *
 * Order of checks matters and is deliberate: structural faults (bad opcode,
 * truncation) are reported before MULTI_MOVE, because a desynchronised walk
 * cannot be trusted to have counted MOVEs correctly. Reporting "two contours"
 * from a walk that has lost the opcode boundary would be a confident wrong
 * answer -- the same failure class as the sample-point predicates that called
 * a 14%-short ring `ok` for two phases. */
static inline vglite_guard_status_t
vglite_guard_check_path(const int32_t *w, size_t n)
{
    size_t i = 0;
    int moves = 0;
    int saw_end = 0;

    if (w == NULL || n == 0) return VGLITE_GUARD_EMPTY;

    while (i < n) {
        const int ops = vglite_guard_operands(w[i]);
        if (ops < 0) return VGLITE_GUARD_BAD_OPCODE;
        if (w[i] == VLC_OP_MOVE) moves++;
        if (w[i] == VLC_OP_END) { saw_end = 1; i++; break; }
        if (i + 1 + (size_t)ops > n) return VGLITE_GUARD_TRUNCATED;
        i += 1 + (size_t)ops;
    }

    if (!saw_end)    return VGLITE_GUARD_NO_END;
    if (i != n)      return VGLITE_GUARD_TRAILING;
    if (moves == 0)  return VGLITE_GUARD_NO_MOVE;
    if (moves > 1)   return VGLITE_GUARD_MULTI_MOVE;
    return VGLITE_GUARD_OK;
}

/* A guard that reports `3` teaches nobody; this is what gets printed. */
static inline const char *vglite_guard_strerror(vglite_guard_status_t s)
{
    switch (s) {
    case VGLITE_GUARD_OK:          return "ok";
    case VGLITE_GUARD_EMPTY:       return "empty-path";
    case VGLITE_GUARD_NO_MOVE:     return "no-contour";
    case VGLITE_GUARD_MULTI_MOVE:  return "multi-contour";
    case VGLITE_GUARD_TRUNCATED:   return "truncated";
    case VGLITE_GUARD_NO_END:      return "no-end";
    case VGLITE_GUARD_TRAILING:    return "trailing-data";
    case VGLITE_GUARD_BAD_OPCODE:  return "bad-opcode";
    default:                       return "unknown";
    }
}

#ifndef VGLITE_GUARD_NO_DRIVER

#include "vg_lite.h"

/* Shared replacement for the GPU_TRY that was copy-pasted into both
 * compositors. BEHAVIOUR-IDENTICAL to what it replaces, deliberately: this
 * retrofit's acceptance test is that no golden moves, so the macro may not
 * gain cleverness on the way in. */
#define VGLITE_GUARD_TRY(call, errctr) \
    do { if ((call) != VG_LITE_SUCCESS) (errctr)++; } while (0)

/* Checked vg_lite_init_path: validates the words FIRST and, on failure,
 * returns false WITHOUT calling the driver -- so a path carrying the measured
 * defect is never submitted. `*status` (optional) carries the reason out for
 * the caller to count or print.
 *
 * `count_words` is a WORD count, not the byte count vg_lite_init_path wants;
 * the conversion happens here so no caller can get it wrong in one direction
 * while the validator reads it in the other. */
static inline int
vglite_guard_init_path(vg_lite_path_t *p, const int32_t *words, size_t count_words,
                       vg_lite_float_t x0, vg_lite_float_t y0,
                       vg_lite_float_t x1, vg_lite_float_t y1,
                       vglite_guard_status_t *status)
{
    const vglite_guard_status_t s = vglite_guard_check_path(words, count_words);
    if (status) *status = s;
    if (s != VGLITE_GUARD_OK) return 0;

    vg_lite_init_path(p, VG_LITE_S32, VG_LITE_HIGH,
                      (uint32_t)(count_words * sizeof(int32_t)),
                      (void *)(uintptr_t)words, x0, y0, x1, y1);
    return 1;
}

#endif /* VGLITE_GUARD_NO_DRIVER */

#ifdef __cplusplus
}
#endif

#endif /* VGLITE_GUARD_H */
