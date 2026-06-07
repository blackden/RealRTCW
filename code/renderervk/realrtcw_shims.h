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

/* CVAR_DEVELOPER: Quake3e flag "settable only in developer mode". RealRTCW
 * doesn't have it; engine cvar machinery ignores unknown flag bits. 0x10000
 * confirmed unused in RealRTCW q_shared.h (last user flag is 0x4000=our
 * CVAR_NODEFAULT addition; 0x8000 and above unused before the 0x40000000
 * MODIFIED bit). */
#ifndef CVAR_DEVELOPER
#define CVAR_DEVELOPER 0x10000
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

/* LIGHTMAP_2D: sentinel value used in tr_font.c and tr_shader.c */
#ifndef LIGHTMAP_2D
#define LIGHTMAP_2D (-4)
#endif
/* NOTE: IMGFLAG_CLAMPTOEDGE is NOT defined here — tr_common.h provides it
 * as an enum member (0x0004). A macro here would shadow the enum and break
 * the tr_common.h enum definition with "expected identifier". */

/* Typedef / enum shims — see abi-diff doc Section 2. */

/* color4ub_t: now defined as a proper union in code/qcommon/q_shared.h
 * (guard COLOR4UB_T_DEFINED). q_shared.h is included above so the typedef
 * is already visible to vendored .c files via this shim. Legacy
 * `typedef byte color4ub_t[4]` sites in tr_local.h / renderercommon are
 * guarded with the same macro and become no-ops. */

/* SURF_DUST: Quake3e surface flag for dust trails; commented out in Q3 source
 * (tr_shader.c:126) but still referenced in infoParms[] table. Map to a
 * harmless unused bit so the table entry compiles without effect. */
#ifndef SURF_DUST
#define SURF_DUST 0x200000  /* unused in RealRTCW surfaceflags.h */
#endif

/* Com_Error: q_shared.h/qcommon.h declare Com_Error(int,...) but vendored
 * tr_init.c defines it as Com_Error(errorParm_t,...). In C, enum != int at
 * the type-system level even though ABI is identical. Remap errorParm_t to
 * int for vendored renderervk TUs so the definition matches the declaration.
 * The cast inside the body (ri.Error(code,...)) still works — int is
 * implicitly convertible to errorParm_t at the call site. */
#define errorParm_t int

/* SURF_FLESH / SURF_METALSTEPS: Quake3e surface flags absent from RealRTCW
 * surfaceflags.h (RTCW replaced them with SURF_CERAMIC / SURF_METAL).
 * Map to nearest RTCW equivalents so vendored tr_shader.c surface-name table
 * compiles. Runtime behaviour is shader-name lookup only; no flag mismatch
 * at BSP parse time because RTCW maps store RTCW flags, not Q3 flags. */
#ifndef SURF_FLESH
#define SURF_FLESH      0x40    /* RTCW: SURF_CERAMIC — same bit */
#endif
#ifndef SURF_METALSTEPS
#define SURF_METALSTEPS 0x1000  /* RTCW: SURF_METAL — same bit */
#endif

/* MAX_UINT: Quake3e q_shared.h defines this; RealRTCW does not.
 * Used in tr_backend.c for unsigned saturation arithmetic. */
#ifndef MAX_UINT
#define MAX_UINT ((unsigned)(~0))
#endif

/* CV_FLOAT / CV_INTEGER: Quake3e cvarValidator_t enum values. RealRTCW's
 * cvarValidator_t is opaque (void *) per renderercommon/tr_public.h. Cast
 * via intptr_t so vendored call sites (ri.Cvar_CheckRange(..., CV_INTEGER))
 * type-check without warning. Value is passed through unchanged; engine
 * never inspects it. */
#ifndef CV_NONE
#define CV_NONE     ((cvarValidator_t)(intptr_t)0)
#endif
#ifndef CV_FLOAT
#define CV_FLOAT    ((cvarValidator_t)(intptr_t)1)
#endif
#ifndef CV_INTEGER
#define CV_INTEGER  ((cvarValidator_t)(intptr_t)2)
#endif

/* CVG_RENDERER: Quake3e cvarGroup_t enum value. RealRTCW's cvarGroup_t is
 * opaque int (renderercommon/tr_public.h). No cast needed — int→int. */
#ifndef CVG_NONE
#define CVG_NONE     0
#endif
#ifndef CVG_RENDERER
#define CVG_RENDERER 1
#endif

/* Inline function shims — abi-diff doc Section 3 + items surfaced by the
 * M2.2 post-build log that the original catalog missed. */

/* myftol(x): Quake3e float-to-long cast helper. RealRTCW doesn't have it
 * by that name; pure cast on every modern platform. */
#ifndef myftol
#define myftol(x) ((long)(x))
#endif

/* Q_atof: Quake3e wraps atof to handle locale/whitespace; RealRTCW uses
 * raw atof. The single vendored call site is locale-agnostic, so direct
 * alias is safe. */
#ifndef Q_atof
#define Q_atof(s) atof(s)
#endif

/* log2pad(v, roundup): ceil-power-of-2 log2 helper. Quake3e qcommon.h
 * has it as static ID_INLINE; RealRTCW has no equivalent. */
static inline unsigned int realrtcw_log2pad(unsigned int v, int roundup) {
    unsigned int r = 0;
    if (roundup && v && (v & (v - 1))) v <<= 1;
    while ((v >>= 1) != 0) r++;
    return r;
}
#define log2pad(v, ru) realrtcw_log2pad((v), (ru))

/* Q_stradd(dst, src): append src to dst and return pointer to new end.
 * Quake3e helper used by Vulkan device-extension string building in vk.c.
 * Semantics: like strcat but returns end-pointer for chained appends. */
static inline char *realrtcw_Q_stradd(char *dst, const char *src) {
    char c;
    while ((c = *src++) != '\0') *dst++ = c;
    *dst = '\0';
    return dst;
}
#define Q_stradd(dst, src) realrtcw_Q_stradd((dst), (src))

/* Com_Split(in, out[], maxOut, sep): split `in` string on `sep` byte,
 * writing pointers to each token into out[]. Mutates `in` (NUL-terminates
 * at separators). Single call site: tr_bsp.c parses 3-float "x y z"
 * strings. Returns count of tokens written. */
static inline int realrtcw_Com_Split(char *in, char **out, int maxOut, char sep) {
    int n = 0;
    char *p = in;
    if (!in || !out || maxOut <= 0) return 0;
    out[n++] = p;
    while (*p) {
        if (*p == sep) {
            *p = '\0';
            p++;
            if (n >= maxOut) return n;
            out[n++] = p;
        } else {
            p++;
        }
    }
    return n;
}
#define Com_Split(in, out, maxOut, sep) realrtcw_Com_Split((in), (out), (maxOut), (sep))

/* Com_GenerateHashValue(name, size): case-insensitive bucket hash for
 * name → size-masked index. RealRTCW has a `static long generateHashValue`
 * in cvar.c (file-private, can't call). Algorithm matches the standard
 * Q3 multiply-add hash used by both Quake3e and RealRTCW internally. */
static inline int realrtcw_Com_GenerateHashValue(const char *fname, const int size) {
    int hash = 0;
    int i;
    for (i = 0; fname[i] != '\0'; i++) {
        char letter = fname[i];
        if (letter >= 'A' && letter <= 'Z') letter += ('a' - 'A');
        if (letter == '\\') letter = '/';
        hash += (int)(letter) * (i + 119);
    }
    hash &= (size - 1);
    return hash;
}
#define Com_GenerateHashValue(name, size) realrtcw_Com_GenerateHashValue((name), (size))

/* crc32_buffer(buf, len): CRC32 over a byte range. Quake3e helper used in
 * tr_bsp.c:1948 to verify a vis-lump magic value. Forwarded to zlib's
 * `unsigned long crc32(unsigned long, const Bytef *, uInt)` which is
 * already linked into the engine (Makefile USE_INTERNAL_LIBS=0 → -lz). */
static inline uint32_t realrtcw_crc32_buffer(const byte *buf, int len) {
    extern unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned int len);
    return (uint32_t)crc32(0L, buf, (unsigned int)len);
}
#define crc32_buffer(buf, len) realrtcw_crc32_buffer((const byte *)(buf), (int)(len))

/* COM_ParseComplex: Quake3e extended parser. Returns token; also writes
 * to com_tokentype so the caller can distinguish strings vs operators.
 * RealRTCW has COM_ParseExt with similar signature but no token-type
 * exposure. Forwarded to COM_ParseExt (with const-cast); com_tokentype
 * is per-TU static (set by the inline before return, read locally after
 * the call in tr_shader.c — same TU, works as long as no inter-TU sharing).
 * Runtime nuance: token-type-aware shader directives (conditional eval,
 * shader macros) will see TK_STRING default; advanced syntax in modded
 * shaders may misparse. Acceptable for M2 compile; revisit if shader
 * regressions appear in M5. */
typedef enum {
    TK_GENEGIC = 0, TK_STRING, TK_QUOTED, TK_EQ, TK_NEQ, TK_GT, TK_GTE,
    TK_LT, TK_LTE, TK_MATCH, TK_OR, TK_AND, TK_SCOPE_OPEN, TK_SCOPE_CLOSE,
    TK_NEWLINE, TK_EOF
} tokenType_t;

static tokenType_t com_tokentype = TK_GENEGIC;

static inline char *realrtcw_COM_ParseComplex(const char **data_p, qboolean allowLineBreak) {
    extern char *COM_ParseExt(char **, qboolean);
    char *tok = COM_ParseExt((char **)(void *)data_p, allowLineBreak);
    com_tokentype = (tok && tok[0]) ? TK_STRING : TK_EOF;
    return tok;
}
#define COM_ParseComplex(data_p, allow) realrtcw_COM_ParseComplex((data_p), (allow))

/* Function shim prototypes — implementations live in realrtcw_engine_glue.c.
 * See abi-diff doc Section 3 where shim mechanism is "wrapper in glue file". */
/* (filled in Task 2.4 — vtable adapter) */

/* Vtable adapter types — Quake3e-style refimport_t typedef that
 * realrtcw_engine_glue.c populates from RealRTCW functions. */
/* (filled in Task 2.4) */

/* renderercommon/tr_font.c uses renderer-private symbols declared in
 * renderervk/tr_common.h. Pull in tr_common.h here so all renderervk TUs
 * (including renderercommon/tr_font.c compiled in the renderervk build)
 * see the full image_t forward-decl, R_CreateImage, RE_RegisterShaderFromImage,
 * and imgFlags_t enum (including IMGFLAG_CLAMPTOEDGE). */
#include "tr_common.h"
/* r_saveFontData cvar is defined in tr_init.c but used in tr_font.c */
extern cvar_t *r_saveFontData;

#endif /* REALRTCW_RENDERERVK_SHIMS_H */
