/* LVGL 9.4 <-> VGLite (VGLITE_HEADER_VERSION 6) compatibility shim.
 *
 * LVGL's VG_LITE backend targets a NEWER NXP driver than the one vendored
 * here. It references 47 gcFEATURE_BIT_VG_* names where this driver defines 9,
 * plus 9 error/format names. lv_vg_lite_feature_string() and
 * lv_vg_lite_error_string() are compiled UNCONDITIONALLY, so every name must
 * exist at compile time even though most are only ever used for logging.
 *
 * C enums cannot be reopened, so these are macros rather than enumerators.
 * vg_lite_query_feature((vg_lite_feature_t)N) compiles, and
 * `case gcFEATURE_BIT_VG_X:` stays a valid constant case label.
 *
 * ★ Values start at gcFEATURE_COUNT because the vendor bounds-checks:
 *
 *     if (feature < gcFEATURE_COUNT) result = ctx->s_ftable.ftable[feature];
 *     else                           result = 0;
 *
 * so every name below reads 0 with no out-of-bounds table access. That is a
 * documented path, not an exploited accident. A value BELOW the count would
 * silently alias a real feature -- asking for SCISSOR and being told DITHER's
 * answer -- so the static assertions at the foot of this file pin it.
 *
 * What 0 MEANS here: "this GC355 does not have that capability", asserted by
 * construction rather than by asking the hardware. Every LVGL use of these is
 * `if (supported) fast_path else portable_path`, so a bit that is in fact
 * supported costs an optimisation, never correctness. Re-vendoring a newer
 * driver is how you would find out; see the Phase 2 spec §4.
 *
 * This file is force-included (-include) for LVGL's translation units ONLY.
 * The VGLite driver itself must compile against the UNSHIMMED headers, or its
 * own switch statements would see names they do not handle.
 *
 * The name set is gated: tools/vglite-lvgl-names.py --check <this file> in the
 * rt1176-evkb tree fails if an LVGL or VGLite pin bump moves it.
 */
#ifndef VG_LITE_LVGL_COMPAT_H
#define VG_LITE_LVGL_COMPAT_H

#include "vg_lite.h"

/* Above the table, so vg_lite_query_feature() takes its `else result = 0`. */
#define VGL_COMPAT_FEATURE(n) ((vg_lite_feature_t)(gcFEATURE_COUNT + (n)))

/* --- feature bits LVGL 9.4 names and this driver does not -----------------
 * Generated: tools/vglite-lvgl-names.py | grep gcFEATURE_BIT
 * Sorted, consecutive indices. Distinct values are load-bearing: duplicates
 * would fail to compile in lv_vg_lite_feature_string()'s switch, which is a
 * safety net worth keeping. */
#define gcFEATURE_BIT_VG_16PIXELS_ALIGN            VGL_COMPAT_FEATURE(0)
#define gcFEATURE_BIT_VG_24BIT                     VGL_COMPAT_FEATURE(1)
#define gcFEATURE_BIT_VG_24BIT_PLANAR              VGL_COMPAT_FEATURE(2)
#define gcFEATURE_BIT_VG_AYUV_INPUT                VGL_COMPAT_FEATURE(3)
#define gcFEATURE_BIT_VG_COLOR_TRANSFORMATION      VGL_COMPAT_FEATURE(4)
#define gcFEATURE_BIT_VG_DEC_COMPRESS              VGL_COMPAT_FEATURE(5)
#define gcFEATURE_BIT_VG_DEC_COMPRESS_2_0          VGL_COMPAT_FEATURE(6)
#define gcFEATURE_BIT_VG_DOUBLE_IMAGE              VGL_COMPAT_FEATURE(7)
#define gcFEATURE_BIT_VG_FLEXA                     VGL_COMPAT_FEATURE(8)
#define gcFEATURE_BIT_VG_GAMMA                     VGL_COMPAT_FEATURE(9)
#define gcFEATURE_BIT_VG_GAUSSIAN_BLUR             VGL_COMPAT_FEATURE(10)
#define gcFEATURE_BIT_VG_GLOBAL_ALPHA              VGL_COMPAT_FEATURE(11)
#define gcFEATURE_BIT_VG_HW_PREMULTIPLY            VGL_COMPAT_FEATURE(12)
#define gcFEATURE_BIT_VG_IM_DEC_INPUT              VGL_COMPAT_FEATURE(13)
#define gcFEATURE_BIT_VG_IM_FASTCLAER              VGL_COMPAT_FEATURE(14)
#define gcFEATURE_BIT_VG_IM_INPUT                  VGL_COMPAT_FEATURE(15)
#define gcFEATURE_BIT_VG_IM_REPEAT_REFLECT         VGL_COMPAT_FEATURE(16)
#define gcFEATURE_BIT_VG_INDEX_ENDIAN              VGL_COMPAT_FEATURE(17)
#define gcFEATURE_BIT_VG_LVGL_SUPPORT              VGL_COMPAT_FEATURE(18)
#define gcFEATURE_BIT_VG_MASK                      VGL_COMPAT_FEATURE(19)
#define gcFEATURE_BIT_VG_MIRROR                    VGL_COMPAT_FEATURE(20)
#define gcFEATURE_BIT_VG_NEW_BLEND_MODE            VGL_COMPAT_FEATURE(21)
#define gcFEATURE_BIT_VG_NEW_IMAGE_INDEX           VGL_COMPAT_FEATURE(22)
#define gcFEATURE_BIT_VG_PARALLEL_PATHS            VGL_COMPAT_FEATURE(23)
#define gcFEATURE_BIT_VG_PE_CLEAR                  VGL_COMPAT_FEATURE(24)
#define gcFEATURE_BIT_VG_PIXEL_MATRIX              VGL_COMPAT_FEATURE(25)
#define gcFEATURE_BIT_VG_RECTANGLE_TILED_OUT       VGL_COMPAT_FEATURE(26)
#define gcFEATURE_BIT_VG_RGBA8_ETC2_EAC            VGL_COMPAT_FEATURE(27)
#define gcFEATURE_BIT_VG_SCISSOR                   VGL_COMPAT_FEATURE(28)
#define gcFEATURE_BIT_VG_SRC_PREMULTIPLIED         VGL_COMPAT_FEATURE(29)
#define gcFEATURE_BIT_VG_STENCIL                   VGL_COMPAT_FEATURE(30)
#define gcFEATURE_BIT_VG_STRIPE_MODE               VGL_COMPAT_FEATURE(31)
#define gcFEATURE_BIT_VG_TESSELLATION_TILED_OUT    VGL_COMPAT_FEATURE(32)
#define gcFEATURE_BIT_VG_USE_DST                   VGL_COMPAT_FEATURE(33)
#define gcFEATURE_BIT_VG_YUV_INPUT                 VGL_COMPAT_FEATURE(34)
#define gcFEATURE_BIT_VG_YUV_OUTPUT                VGL_COMPAT_FEATURE(35)
#define gcFEATURE_BIT_VG_YUV_TILED_INPUT           VGL_COMPAT_FEATURE(36)
#define gcFEATURE_BIT_VG_YUY2_INPUT                VGL_COMPAT_FEATURE(37)

/* --- the alias check, not a comment ---------------------------------------
 * If any shimmed value fell below gcFEATURE_COUNT it would index the real
 * feature table and return another feature's answer. Assert instead of trust.
 */
#if defined(__GNUC__) || defined(__clang__)
_Static_assert(VGL_COMPAT_FEATURE(0) >= gcFEATURE_COUNT,
               "compat feature values must sit above the real feature table");
_Static_assert(gcFEATURE_BIT_VG_SCISSOR >= gcFEATURE_COUNT,
               "gcFEATURE_BIT_VG_SCISSOR aliases a real feature");
_Static_assert(gcFEATURE_BIT_VG_LVGL_SUPPORT >= gcFEATURE_COUNT,
               "gcFEATURE_BIT_VG_LVGL_SUPPORT aliases a real feature");
_Static_assert(gcFEATURE_BIT_VG_YUY2_INPUT >= gcFEATURE_COUNT,
               "the last compat feature aliases a real feature");
#endif

#endif /* VG_LITE_LVGL_COMPAT_H */
