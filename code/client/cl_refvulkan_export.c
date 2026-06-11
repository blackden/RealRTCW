/*
===========================================================================
Copyright (C) 2026 RealRTCW contributors

This file is part of RealRTCW source code.  See cl_refvulkan.c for the
sibling TU that handles the engine→renderer direction. This TU handles
the renderer→engine direction.

IMPORTANT: this TU MUST NOT include anything from renderercommon/. It
sees the SMALL refexport_t / refimport_t / tr_types.h via the engine's
own renderer/tr_public.h. cl_refvulkan.c sees the BIG view; the two
TUs communicate via void* and link-time symbols only.
===========================================================================
*/

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../renderer/tr_public.h"
#include "cl_refvulkan.h"

/* Engine-side SMALL refexport_t — what cl_main.c's `re` global expects. */
static refexport_t  vk_re_small;
static qboolean     vk_re_built = qfalse;

/* Engine SMALL: Shutdown(qboolean destroyWindow).
 *   destroyWindow == qtrue  → renderer should drop the window.
 *   destroyWindow == qfalse → reconfig only; keep window+context to
 *                             avoid the desktop-flash UX glitch.
 * Maps to Q3e BIG: Shutdown(refShutdownCode_t code).
 *   REF_DESTROY_WINDOW  ≈ qtrue
 *   REF_KEEP_CONTEXT    ≈ qfalse (also keeps window, so safe choice)
 *
 * Hardcoded enum values to avoid pulling in BIG type defs:
 *   REF_KEEP_CONTEXT   = 0
 *   REF_KEEP_WINDOW    = 1
 *   REF_DESTROY_WINDOW = 2
 *   REF_UNLOAD_DLL     = 3 */
static void vk_re_wrap_Shutdown( qboolean destroyWindow ) {
    vk_re_thunk_Shutdown( destroyWindow ? 2 /* REF_DESTROY_WINDOW */
                                        : 0 /* REF_KEEP_CONTEXT */ );
}

/* SMALL: AddRefEntityToScene(refEntity_t*). BIG adds qboolean intShaderTime.
 * Default to qfalse (Q3e's "use ent->shaderTime as-is"). */
static void vk_re_wrap_AddRefEntityToScene( const refEntity_t *re ) {
    vk_re_thunk_AddRefEntityToScene( (const void *)re, 0 /* intShaderTime = qfalse */ );
}

/* SMALL: AddPolyToScene(hShader, numVerts, verts). BIG adds int num.
 * Engine never split-loops over polys here — pass num=1. */
static void vk_re_wrap_AddPolyToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts ) {
    vk_re_thunk_AddPolyToScene( hShader, numVerts, (const void *)verts, 1 );
}

/* SMALL: AddLightToScene(org, intensity, r, g, b, overdraw). BIG drops overdraw. */
static void vk_re_wrap_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b, int overdraw ) {
    (void)overdraw;  /* RTCW-specific dynamic-light visibility hint; not modeled by Q3e renderer. */
    vk_re_thunk_AddLightToScene( org, intensity, r, g, b );
}

/* SMALL LerpTag: (tag, refent, tagName, startIndex).
 * BIG LerpTag:   (tag, hModel, startFrame, endFrame, frac, tagName).
 *
 * Translate by reading model/frame/oldframe/backlerp out of refent.
 *   BIG endFrame   = refent->frame      (current frame)
 *   BIG startFrame = refent->oldframe   (previous frame)
 *   BIG frac       = 1.0 - refent->backlerp
 *     (engine semantic: backlerp=1 means "fully on oldframe" → frac=0;
 *                       backlerp=0 means "fully on frame"    → frac=1.)
 *
 * startIndex dropped — RTCW-only multi-tag iteration extension. RTCW SP
 * cgame doesn't rely on it (verified via cgame asset audit, M3.5 era).
 * If a duplicate-name tag bug appears in gameplay, this is the place. */
static int vk_re_wrap_LerpTag( orientation_t *tag, const refEntity_t *refent,
                               const char *tagName, int startIndex ) {
    (void)startIndex;
    return vk_re_thunk_LerpTag( (void *)tag,
                                (int)refent->hModel,
                                refent->oldframe,
                                refent->frame,
                                1.0f - refent->backlerp,
                                tagName );
}

/* SMALL DrawStretchRaw / UploadCinematic take `const byte *data`; BIG drops
 * the const. The renderer copies data into texture memory and never writes
 * through the input ptr (verified in code/renderervk/tr_cmds.c). Wrap so
 * the const-cast lives inside vk_re_thunk_* and is grep-locatable. */
static void vk_re_wrap_DrawStretchRaw( int x, int y, int w, int h, int cols, int rows,
                                       const byte *data, int client, qboolean dirty ) {
    vk_re_thunk_DrawStretchRaw( x, y, w, h, cols, rows, data, client, dirty );
}

static void vk_re_wrap_UploadCinematic( int w, int h, int cols, int rows,
                                        const byte *data, int client, qboolean dirty ) {
    vk_re_thunk_UploadCinematic( w, h, cols, rows, data, client, dirty );
}

/* ====================================================================
 * Group C — RTCW-only refexport slots (no BIG counterpart).
 *
 * Each is a tripwire stub: silent on subsequent calls, prints once on
 * first call. M5 use: drive a smoke run, scrape the log for which of
 * these actually fire during boot+menu+first-level-load, prioritize
 * follow-up routing accordingly.
 *
 * Signatures and behaviors are SMALL (engine view). When a renderer
 * counterpart is wired in a follow-up commit, replace the stub body
 * with the routing logic and remove the tripwire.
 * ==================================================================== */

#define VK_RE_TRIPWIRE(name) \
    static qboolean vk_re_C_fired_##name = qfalse; \
    static void vk_re_C_log_##name( void ) { \
        if ( !vk_re_C_fired_##name ) { \
            Com_Printf( S_COLOR_YELLOW "[M5/C] re." #name " called — RTCW-only slot, no-op stub\n" ); \
            vk_re_C_fired_##name = qtrue; \
        } \
    }

VK_RE_TRIPWIRE(RegisterSmartSkin)
VK_RE_TRIPWIRE(GetSkinModel)
VK_RE_TRIPWIRE(GetShaderFromModel)
VK_RE_TRIPWIRE(AddPolysToScene)
VK_RE_TRIPWIRE(AddCoronaToScene)
VK_RE_TRIPWIRE(SetFog)
VK_RE_TRIPWIRE(DrawStretchPicGradient)
VK_RE_TRIPWIRE(ZombieFXAddNewHit)

#undef VK_RE_TRIPWIRE

static qhandle_t vk_re_C_RegisterSmartSkin( const char *n, const char *m, qboolean u ) {
    (void)n; (void)m; (void)u;
    vk_re_C_log_RegisterSmartSkin();
    return 0;
}

static qboolean vk_re_C_GetSkinModel( qhandle_t skinid, const char *type, char *name ) {
    (void)skinid; (void)type; (void)name;
    vk_re_C_log_GetSkinModel();
    return qfalse;
}

static qhandle_t vk_re_C_GetShaderFromModel( qhandle_t modelid, int surfnum, int withlightmap ) {
    (void)modelid; (void)surfnum; (void)withlightmap;
    vk_re_C_log_GetShaderFromModel();
    return 0;
}

static void vk_re_C_AddPolysToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts, int numPolys ) {
    (void)hShader; (void)numVerts; (void)verts; (void)numPolys;
    vk_re_C_log_AddPolysToScene();
    /* TODO M5-followup: loop over numPolys, call vk_re_small.AddPolyToScene
     * for each. Trivial routing — deferred until smoke confirms it fires. */
}

static void vk_re_C_AddCoronaToScene( const vec3_t org, float r, float g, float b, float scale, int id, int flags ) {
    (void)org; (void)r; (void)g; (void)b; (void)scale; (void)id; (void)flags;
    vk_re_C_log_AddCoronaToScene();
    /* TODO M5-followup: approximate as small dynamic light via vk_re_small.AddLightToScene. */
}

static void vk_re_C_SetFog( int fogvar, int var1, int var2, float r, float g, float b, float density ) {
    (void)fogvar; (void)var1; (void)var2; (void)r; (void)g; (void)b; (void)density;
    vk_re_C_log_SetFog();
    /* TODO M5-followup: routing to renderervk fog system depends on
     * whether Q3e fog API is rich enough — investigate after smoke. */
}

static void vk_re_C_DrawStretchPicGradient( float x, float y, float w, float h,
                                            float s1, float t1, float s2, float t2,
                                            qhandle_t hShader, const float *gradientColor,
                                            int gradientType ) {
    vk_re_C_log_DrawStretchPicGradient();
    /* Fallback: gradientless DrawStretchPic — loses the gradient effect
     * but the pic still draws (HUD won't look wrong, just slightly off). */
    (void)gradientColor; (void)gradientType;
    if ( vk_re_small.DrawStretchPic ) {
        vk_re_small.DrawStretchPic( x, y, w, h, s1, t1, s2, t2, hShader );
    }
}

static void vk_re_C_ZombieFXAddNewHit( int entityNum, const vec3_t hitPos, const vec3_t hitDir ) {
    (void)entityNum; (void)hitPos; (void)hitDir;
    vk_re_C_log_ZombieFXAddNewHit();
    /* RealRTCW-specific zombie blood/spark FX. No Q3e equivalent. No-op
     * is observationally inert — the zombies will lack hit-spark VFX
     * but combat is functional. */
}

void *CL_BuildVulkanRefExport( void *big_export ) {
    if ( vk_re_built ) {
        return &vk_re_small;
    }

    Com_Memset( &vk_re_small, 0, sizeof( vk_re_small ) );

    /* Hand the BIG pointer to the sibling TU which holds the thunks. */
    CL_VulkanRefExport_StoreBig( big_export );

    /* Group B — Shutdown: qboolean → refShutdownCode_t */
    vk_re_small.Shutdown = vk_re_wrap_Shutdown;

    /* Group B — scene-build wrappers */
    vk_re_small.AddRefEntityToScene = vk_re_wrap_AddRefEntityToScene;
    vk_re_small.AddPolyToScene      = vk_re_wrap_AddPolyToScene;
    vk_re_small.AddLightToScene     = vk_re_wrap_AddLightToScene;
    vk_re_small.LerpTag             = vk_re_wrap_LerpTag;
    vk_re_small.DrawStretchRaw  = vk_re_wrap_DrawStretchRaw;
    vk_re_small.UploadCinematic = vk_re_wrap_UploadCinematic;

    /* Group A — identity slots: name + signature match in SMALL and BIG.
     * Cast through void * to silence "incompatible pointer type" warnings
     * (legitimate — SMALL and BIG see different tr_types.h, so the
     * function pointer types differ in record-typed args even when the
     * slot semantics are identical). */
    vk_re_small.BeginRegistration   = (void (*)( glconfig_t * ))vk_re_get_BeginRegistration();
    vk_re_small.RegisterModel       = (qhandle_t (*)( const char * ))vk_re_get_RegisterModel();
    vk_re_small.RegisterSkin        = (qhandle_t (*)( const char * ))vk_re_get_RegisterSkin();
    vk_re_small.RegisterShader      = (qhandle_t (*)( const char * ))vk_re_get_RegisterShader();
    vk_re_small.RegisterShaderNoMip = (qhandle_t (*)( const char * ))vk_re_get_RegisterShaderNoMip();
    vk_re_small.LoadWorld           = (void (*)( const char * ))vk_re_get_LoadWorld();
    vk_re_small.SetWorldVisData     = (void (*)( const byte * ))vk_re_get_SetWorldVisData();
    vk_re_small.EndRegistration     = (void (*)( void ))vk_re_get_EndRegistration();
    vk_re_small.ClearScene          = (void (*)( void ))vk_re_get_ClearScene();
    vk_re_small.LightForPoint       = (int (*)( vec3_t, vec3_t, vec3_t, vec3_t ))vk_re_get_LightForPoint();
    vk_re_small.RenderScene         = (void (*)( const refdef_t * ))vk_re_get_RenderScene();
    vk_re_small.SetColor            = (void (*)( const float * ))vk_re_get_SetColor();
    vk_re_small.DrawStretchPic      = (void (*)( float, float, float, float, float, float, float, float, qhandle_t ))vk_re_get_DrawStretchPic();
    vk_re_small.BeginFrame          = (void (*)( stereoFrame_t ))vk_re_get_BeginFrame();
    vk_re_small.EndFrame            = (void (*)( int *, int * ))vk_re_get_EndFrame();
    vk_re_small.MarkFragments       = (int (*)( int, const vec3_t *, const vec3_t, int, vec3_t, int, markFragment_t * ))vk_re_get_MarkFragments();
    vk_re_small.ModelBounds         = (void (*)( qhandle_t, vec3_t, vec3_t ))vk_re_get_ModelBounds();
    vk_re_small.RegisterFont        = (void (*)( const char *, int, fontInfo_t * ))vk_re_get_RegisterFont();
    vk_re_small.RemapShader         = (void (*)( const char *, const char *, const char * ))vk_re_get_RemapShader();
    vk_re_small.GetEntityToken      = (qboolean (*)( char *, int ))vk_re_get_GetEntityToken();
    vk_re_small.TakeVideoFrame      = (void (*)( int, int, byte *, byte *, qboolean ))vk_re_get_TakeVideoFrame();

    /* Group C — RTCW-only stubs */
    vk_re_small.RegisterSmartSkin     = vk_re_C_RegisterSmartSkin;
    vk_re_small.GetSkinModel          = vk_re_C_GetSkinModel;
    vk_re_small.GetShaderFromModel    = vk_re_C_GetShaderFromModel;
    vk_re_small.AddPolysToScene       = vk_re_C_AddPolysToScene;
    vk_re_small.AddCoronaToScene      = vk_re_C_AddCoronaToScene;
    vk_re_small.SetFog                = vk_re_C_SetFog;
    vk_re_small.DrawStretchPicGradient = vk_re_C_DrawStretchPicGradient;
    vk_re_small.ZombieFXAddNewHit     = vk_re_C_ZombieFXAddNewHit;

    vk_re_built = qtrue;
    return &vk_re_small;
}
