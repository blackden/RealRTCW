/*
 * RealRTCW shims for vendored Quake3e renderervk/renderercommon.
 *
 * Included via Makefile's -include flag before every vendored source file
 * is compiled. NEVER include this from engine-side code.
 *
 * Edit policy: this file IS RealRTCW-authored (filename prefix
 * realrtcw_ signals that to the vendor-guard hook), so editing is
 * unblocked. See docs/vulkan-phase2-abi-diff.md for the drift catalog
 * each entry below comes from.
 */
#ifndef REALRTCW_RENDERERVK_SHIMS_H
#define REALRTCW_RENDERERVK_SHIMS_H

/* q_shared.h supplies byte, uint32_t (via stdint), CVAR_ARCHIVE, etc.
 * Including it here means subsequent shim definitions can use those types
 * even though the shim is -include'd before the vendored .c body. */
#include "../qcommon/q_shared.h"

/* Macro shims — see abi-diff doc Section 1. */

/* NORETURN_PTR: Quake3e uses for function-pointer noreturn attribute;
 * RealRTCW has NORETURN for functions but no equivalent for pointers.
 * Safe to define empty — pure static-analysis annotation. */
#ifndef NORETURN_PTR
#define NORETURN_PTR
#endif

/* FORMAT_PRINTF(x,y): Quake3e wraps __attribute__((format(printf,x,y))) in a
 * named macro; RealRTCW places the attribute inline everywhere.
 * Expand to the GCC/Clang attribute unconditionally on this platform. */
#ifndef FORMAT_PRINTF
#define FORMAT_PRINTF(x,y) __attribute__((format(printf,x,y)))
#endif

/* SGN(x): Standard signum macro — not present in RealRTCW q_shared.h.
 * Used in tr_main.c for billboard orientation. */
#ifndef SGN
#define SGN(x) (((x) >= 0) ? !!(x) : -1)
#endif

/* DotProduct4 / VectorScale4: 4-component vector ops used in tr_main.c
 * frustum/sphere culling. Direct copies from Quake3e q_shared.h. */
#ifndef DotProduct4
#define DotProduct4(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1]+(a)[2]*(b)[2]+(a)[3]*(b)[3])
#endif
#ifndef VectorScale4
#define VectorScale4(a,b,c) ((c)[0]=(a)[0]*(b),(c)[1]=(a)[1]*(b),(c)[2]=(a)[2]*(b),(c)[3]=(a)[3]*(b))
#endif

/* CVAR_NODEFAULT / CVAR_ARCHIVE_ND: Quake3e cvar flags not present in
 * RealRTCW. 0x4000 is confirmed unused in RealRTCW q_shared.h (flags
 * top out at CVAR_PROTECTED 0x2000 before the 0x40000000 modified bit).
 * Engine cvar machinery ignores unknown flag bits gracefully. */
#ifndef CVAR_NODEFAULT
#define CVAR_NODEFAULT 0x4000
#endif
#ifndef CVAR_ARCHIVE_ND
#define CVAR_ARCHIVE_ND (CVAR_ARCHIVE|CVAR_NODEFAULT)
#endif

/* CONTENTS_NODE: BSP tree-traversal sentinel (-1) for internal nodes.
 * Not in RealRTCW surfaceflags.h (which only lists leaf CONTENTS_* values).
 * Value confirmed correct per Q3 BSP spec and RealRTCW's own CM loader. */
#ifndef CONTENTS_NODE
#define CONTENTS_NODE -1
#endif

/* MAX_VIDEO_HANDLES: defined in cl_cin.c:73 as 16 but not exposed to
 * renderer headers. Used in tr_local.h for scratchImage array sizing.
 * Must stay in sync with cl_cin.c. */
#ifndef MAX_VIDEO_HANDLES
#define MAX_VIDEO_HANDLES 16
#endif

/* VIS_HEADER: size in bytes of the BSP vis-lump header (numClusters int +
 * clusterBytes int = 8 bytes). Used in tr_bsp.c to skip past the header
 * when reading raw vis data. Confirmed by Q3 BSP spec and
 * code/renderer/tr_bsp.c:R_LoadVisibility. */
#ifndef VIS_HEADER
#define VIS_HEADER 8
#endif

/* IMGFLAG_CLAMPTOEDGE / LIGHTMAP_2D: defined in RealRTCW renderer-private
 * headers (tr_local.h and tr_shader.c respectively) but needed by
 * renderercommon/tr_font.c which cannot include those private headers in
 * the renderervk build context. Values duplicated verbatim from source. */
#ifndef IMGFLAG_CLAMPTOEDGE
#define IMGFLAG_CLAMPTOEDGE 0x0040
#endif
#ifndef LIGHTMAP_2D
#define LIGHTMAP_2D (-4)
#endif

/* Typedef / enum shims — see abi-diff doc Section 2. */

/* color4ub_t: Quake3e changed byte shaderRGBA[4] in refEntity_t to a
 * tagged union providing both .rgba[] byte access and .u32 word access.
 * Vendored renderervk reads both members throughout (tr_shade_calc.c,
 * tr_surface.c, vk.c, etc.). Engine-side struct translation happens in
 * realrtcw_engine_glue.c (Task 2.5). Layout: rgba[0]=R … rgba[3]=A. */
typedef union {
    byte     rgba[4];
    uint32_t u32;
} color4ub_t;

/* CV_FLOAT / CV_INTEGER: Quake3e cvarValidator_t enum values. RealRTCW's
 * cvarValidator_t is opaque (void *) — tr_public.h already typedefs it as
 * void *. The values are passed to Cvar_Get but never inspected engine-side.
 * Define as integer constants to avoid redefinition conflict. */
#ifndef CV_NONE
#define CV_NONE     0
#endif
#ifndef CV_FLOAT
#define CV_FLOAT    1
#endif
#ifndef CV_INTEGER
#define CV_INTEGER  2
#endif

/* CVG_RENDERER: Quake3e cvarGroup_t enum value. RealRTCW's cvarGroup_t is
 * opaque int (tr_public.h). Value passed through, never read engine-side.
 * Define as integer constants to avoid redefinition conflict. */
#ifndef CVG_NONE
#define CVG_NONE     0
#endif
#ifndef CVG_RENDERER
#define CVG_RENDERER 1
#endif

/* Inline function shims — see abi-diff doc Section 3 where shim mechanism
 * is "static inline in header". */
/* (filled in Task 2.3) */

/* Function shim prototypes — implementations live in realrtcw_engine_glue.c.
 * See abi-diff doc Section 3 where shim mechanism is "wrapper in glue file". */
/* (filled in Task 2.3, 2.4) */

/* Vtable adapter types — Quake3e-style refimport_t typedef that
 * realrtcw_engine_glue.c populates from RealRTCW functions. */
/* (filled in Task 2.4) */

#endif /* REALRTCW_RENDERERVK_SHIMS_H */
