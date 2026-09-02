/* Host-compiled unit test for port/vglite_guard.h's path validator.
 * Build: tests/run.sh
 * Copyright (c) 2026 Nicholas Newdigate
 * SPDX-License-Identifier: MIT
 *
 * ★ WHY THIS EXISTS, and why it carries a negative arm per status.
 *
 * NO GATE IN THE rt1176-evkb TREE CAN SEE GPU CODE. Every QEMU gate runs the
 * SOFTWARE engine (there is no GC355 model), and the GPU goldens live only in
 * hand-pressed hardware transcripts. So a defect in the guard would not
 * surface in a 124-gate sweep; this file is the guard's only automated
 * coverage, which is exactly why the validator was split to need no driver.
 *
 * ★★ EVERY STATUS GETS AN ARM. A validator whose failing branches nothing
 * executes could be hard-wired to return OK and still leave a positive-only
 * suite green -- demonstrated three separate times in this workstream (the
 * conformance suite's stray-ink arm, the pinned blend reading, the fader
 * premultiply's pin-fires arm). A check nothing exercises is decoration.
 */
#undef NDEBUG          /* assertions must survive a -DNDEBUG build; BEFORE every
                        * include, so no header can pull in assert.h first */
#define VGLITE_GUARD_NO_DRIVER   /* the point of the split: no vg_lite.h here */
#include "../port/vglite_guard.h"
#include <assert.h>
#include <stdio.h>

static int checks;

#define CHECK(words, expect)                                                  \
    do {                                                                      \
        const vglite_guard_status_t got =                                     \
            vglite_guard_check_path((words), sizeof(words)/sizeof((words)[0]));\
        if (got != (expect)) {                                                \
            printf("FAIL %s:%d: got %s, expected %s\n", __FILE__, __LINE__,   \
                   vglite_guard_strerror(got), vglite_guard_strerror(expect));\
            assert(got == (expect));                                          \
        }                                                                     \
        checks++;                                                             \
    } while (0)

int main(void)
{
    /* ================= POSITIVE ARMS: real compositor geometry ============ */

    /* emit_rect() + finish_path()'s END -- the fader's tick run, verbatim
     * shape: MOVE, 3x LINE, CLOSE, END. */
    static const int32_t rect[] = {
        VLC_OP_MOVE,  128, 2288,
        VLC_OP_LINE, 1472, 2288,
        VLC_OP_LINE, 1472, 2544,
        VLC_OP_LINE,  128, 2544,
        VLC_OP_CLOSE,
        VLC_OP_END
    };
    CHECK(rect, VGLITE_GUARD_OK);

    /* emit_round_rect(): cubic corners. Exercises the 6-operand walk, which a
     * validator that assumed 2 operands everywhere would desynchronise on. */
    static const int32_t rrect[] = {
        VLC_OP_MOVE,   96,   0,
        VLC_OP_LINE,  384,   0,
        VLC_OP_CUBIC, 402,   0,  416,  14,  416,  32,
        VLC_OP_LINE,  416, 288,
        VLC_OP_CUBIC, 416, 306,  402, 320,  384, 320,
        VLC_OP_CLOSE,
        VLC_OP_END
    };
    CHECK(rrect, VGLITE_GUARD_OK);

    /* The rotary's emit_ring keyhole: ONE contour that still cuts a hole --
     * outer edge, a LINE across, the reversed inner edge, CLOSE. This is the
     * construction the measured rule PRESCRIBES (path/two-draws-ring measured
     * fill=5376 exactly), so it must pass. */
    static const int32_t keyhole[] = {
        VLC_OP_MOVE,  576,   0,
        VLC_OP_QUAD,  576, 318,  318, 576,
        VLC_OP_LINE,  256, 576,
        VLC_OP_QUAD,  256, 141,  141, 256,
        VLC_OP_CLOSE,
        VLC_OP_END
    };
    CHECK(keyhole, VGLITE_GUARD_OK);

    /* A bare MOVE+END: degenerate but structurally legal -- one contour,
     * terminated. The guard's job is the MEASURED defect, not taste. */
    static const int32_t bare[] = { VLC_OP_MOVE, 0, 0, VLC_OP_END };
    CHECK(bare, VGLITE_GUARD_OK);

    /* ================= THE LOAD-BEARING NEGATIVE ARM ====================== */

    /* ★ TWO emit_rect()s in ONE path -- EXACTLY what synthui_fader_gpu.cpp
     * built before NEW-23, and exactly what this silicon renders as ONE bar
     * (path/multi-contour-disjoint: runs=1 of 4; path/two-disjoint-bars:
     * runs=1 of 2). If the guard catches nothing else, it must catch this. */
    static const int32_t two_rects[] = {
        VLC_OP_MOVE,  128, 2288,
        VLC_OP_LINE, 1472, 2288,
        VLC_OP_LINE, 1472, 2544,
        VLC_OP_CLOSE,
        VLC_OP_MOVE,  128, 2416,   /* <-- the second contour */
        VLC_OP_LINE, 1472, 2416,
        VLC_OP_LINE, 1472, 2672,
        VLC_OP_CLOSE,
        VLC_OP_END
    };
    CHECK(two_rects, VGLITE_GUARD_MULTI_MOVE);

    /* The nested case too -- a two-contour ring relying on winding to cut its
     * hole. It renders BOTH contours, but mis-covers by 769 px
     * (path/two-contour-ring-nonzero, cover=short:769) and does so
     * nondeterministically. Refused for the same reason. */
    static const int32_t nested_ring[] = {
        VLC_OP_MOVE,    0,   0,
        VLC_OP_LINE, 1024,   0,
        VLC_OP_LINE, 1024, 1024,
        VLC_OP_CLOSE,
        VLC_OP_MOVE,  256, 256,
        VLC_OP_LINE,  256, 768,
        VLC_OP_LINE,  768, 768,
        VLC_OP_CLOSE,
        VLC_OP_END
    };
    CHECK(nested_ring, VGLITE_GUARD_MULTI_MOVE);

    /* ================= ONE ARM PER REMAINING STATUS ======================= */

    /* NO_END -- the Phase 1 hang: unterminated data wedges the Vivante front
     * end while every vg_lite_* call keeps returning VG_LITE_SUCCESS. A
     * return-code check cannot see this; the validator is the only thing that
     * can. */
    static const int32_t no_end[] = {
        VLC_OP_MOVE, 0, 0, VLC_OP_LINE, 16, 0, VLC_OP_CLOSE
    };
    CHECK(no_end, VGLITE_GUARD_NO_END);

    /* TRUNCATED -- an arena overflow that clipped mid-operand. */
    static const int32_t truncated[] = { VLC_OP_MOVE, 0, 0, VLC_OP_LINE, 16 };
    CHECK(truncated, VGLITE_GUARD_TRUNCATED);

    /* A CUBIC cut short: the 6-operand case of the same fault. */
    static const int32_t trunc_cubic[] = {
        VLC_OP_MOVE, 0, 0, VLC_OP_CUBIC, 1, 2, 3, 4
    };
    CHECK(trunc_cubic, VGLITE_GUARD_TRUNCATED);

    /* TRAILING -- two COMPLETE paths concatenated. Distinct from MULTI_MOVE:
     * the walk stops at the first END, so anything after it was never going to
     * be parsed as this path. Reported as its own fault rather than folded in,
     * because the fix differs (split the buffer, not the contour). */
    static const int32_t trailing[] = {
        VLC_OP_MOVE, 0, 0, VLC_OP_END,
        VLC_OP_MOVE, 8, 8, VLC_OP_END
    };
    CHECK(trailing, VGLITE_GUARD_TRAILING);

    /* NO_MOVE -- geometry with no contour ever opened. */
    static const int32_t no_move[] = { VLC_OP_LINE, 4, 4, VLC_OP_END };
    CHECK(no_move, VGLITE_GUARD_NO_MOVE);

    /* BAD_OPCODE -- an uninitialised or misaligned word. 0x03 is not a VLC
     * opcode we emit. */
    static const int32_t bad_op[] = { VLC_OP_MOVE, 0, 0, 3, VLC_OP_END };
    CHECK(bad_op, VGLITE_GUARD_BAD_OPCODE);

    /* EMPTY, both ways it can arise. */
    static const int32_t one[] = { VLC_OP_END };
    assert(vglite_guard_check_path(one, 0) == VGLITE_GUARD_EMPTY); checks++;
    assert(vglite_guard_check_path(NULL, 4) == VGLITE_GUARD_EMPTY); checks++;

    /* ================= ORDERING: structure before counting ================ */

    /* ★ A desynchronised walk must NOT report a contour count. This buffer has
     * two MOVEs AND a bad opcode; the bad opcode wins, because a walk that has
     * lost the opcode boundary cannot be trusted to have counted MOVEs. A
     * confident wrong answer is worse than a vague right one -- the same
     * failure class as the sample-point predicates that called a 14%-short
     * ring `ok` for two phases. */
    static const int32_t desync[] = {
        VLC_OP_MOVE, 0, 0, 7, VLC_OP_MOVE, 1, 1, VLC_OP_END
    };
    CHECK(desync, VGLITE_GUARD_BAD_OPCODE);

    /* ================= operand table ====================================== */

    assert(vglite_guard_operands(VLC_OP_END)   == 0); checks++;
    assert(vglite_guard_operands(VLC_OP_CLOSE) == 0); checks++;
    assert(vglite_guard_operands(VLC_OP_MOVE)  == 2); checks++;
    assert(vglite_guard_operands(VLC_OP_LINE)  == 2); checks++;
    assert(vglite_guard_operands(VLC_OP_QUAD)  == 4); checks++;
    assert(vglite_guard_operands(VLC_OP_CUBIC) == 6); checks++;
    assert(vglite_guard_operands(0x7F)         < 0);  checks++;

    /* Every status must have a distinct, non-empty name -- a guard that
     * reports a number teaches nobody, and two statuses sharing a name would
     * make a failure unreadable in exactly the moment it matters. */
    for (int a = VGLITE_GUARD_OK; a <= VGLITE_GUARD_BAD_OPCODE; a++) {
        const char *na = vglite_guard_strerror((vglite_guard_status_t)a);
        assert(na && na[0] && na[0] != 'u'); /* never falls through to "unknown" */
        for (int b = a + 1; b <= VGLITE_GUARD_BAD_OPCODE; b++) {
            const char *nb = vglite_guard_strerror((vglite_guard_status_t)b);
            int same = 1;
            for (int k = 0; na[k] || nb[k]; k++)
                if (na[k] != nb[k]) { same = 0; break; }
            assert(!same);
            checks++;
        }
        checks++;
    }

    printf("vglite_guard: all PASS (%d checks)\n", checks);
    return 0;
}
