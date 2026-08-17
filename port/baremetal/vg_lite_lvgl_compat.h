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

/* --- error + format names LVGL 9.4 names and this driver does not ----------
 * Generated: tools/vglite-lvgl-names.py | grep '^VG_LITE_'
 *
 * Values sit far above every real enumerator (formats reach 100, errors 11),
 * so none can alias one. Unlike the feature bits above, these are NOT all
 * inert -- see the hazard note below.
 */
#define VGL_COMPAT_ENUM(n) (0x7000 + (n))

/* Stringify-only in LVGL: they appear solely in lv_vg_lite_error_string() /
 * the format stringifier, so a never-produced value is correct and inert. */
#define VG_LITE_ABGR8565                VGL_COMPAT_ENUM(0)
#define VG_LITE_ARGB8565                VGL_COMPAT_ENUM(1)
#define VG_LITE_RGB888                  VGL_COMPAT_ENUM(2)
#define VG_LITE_RGBA5658                VGL_COMPAT_ENUM(3)
#define VG_LITE_FLEXA_TIME_OUT          VGL_COMPAT_ENUM(4)
#define VG_LITE_FLEXA_HANDSHAKE_FAIL    VGL_COMPAT_ENUM(5)

/* Compared, never produced: lv_vg_lite_utils.c:728 tests
 *     if (tiled || format == VG_LITE_RGBA8888_ETC2_EAC)
 * and nothing in this configuration yields it, so a unique value makes that
 * test permanently false -- which is the correct answer. */
#define VG_LITE_RGBA8888_ETC2_EAC       VGL_COMPAT_ENUM(6)

/* ★★ THESE TWO ARE NOT INERT. Read before touching.
 *
 * LVGL RETURNS them as the format a buffer will actually be drawn with:
 *     case LV_COLOR_FORMAT_ARGB8565: return VG_LITE_BGRA5658;   utils.c:582
 *     case LV_COLOR_FORMAT_RGB888:   return VG_LITE_BGR888;     utils.c:585
 * so if an image in either LVGL colour format reaches this backend, the value
 * below is handed to the GPU as a real format.
 *
 * This driver HANGS on input it cannot parse rather than reporting an error --
 * Phase 1 spent most of its time on a misaligned command buffer that stopped
 * the front end while every API call returned VG_LITE_SUCCESS. So the failure
 * mode here is SILENCE, not a wrong colour, and it would look like a hardware
 * fault rather than a shim defect.
 *
 * Safe today only because nothing produces those colour formats: every image
 * decoder is off in LVGL/port/lv_conf.h (LODEPNG, BMP, TJPGD,
 * BIN_DECODER_RAM_LOAD all 0). Enabling one makes this shim unsafe, and the
 * answer then is to re-vendor a driver that HAS the formats -- never to map
 * them onto something that "looks close". Silently drawing ARGB8565 as
 * BGRA8888 is the plausible-but-wrong behaviour this tree's gates exist to
 * catch.
 *
 * ★ The guard for that is enforced at CONFIGURE time by import_evkb_lvgl(),
 * NOT here. This header is force-included with -include, i.e. before the
 * translation unit's own #includes, so lv_conf.h has not been read yet and an
 * `#if LV_USE_LODEPNG` in this file would test an undefined macro and never
 * fire. A guard that cannot fire is worse than none: it reads as protection.
 */
#define VG_LITE_BGR888                  VGL_COMPAT_ENUM(7)
#define VG_LITE_BGRA5658                VGL_COMPAT_ENUM(8)

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
