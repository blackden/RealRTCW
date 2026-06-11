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

void *CL_BuildVulkanRefExport( void *big_export ) {
    if ( vk_re_built ) {
        return &vk_re_small;
    }

    Com_Memset( &vk_re_small, 0, sizeof( vk_re_small ) );

    /* Hand the BIG pointer to the sibling TU which holds the thunks. */
    CL_VulkanRefExport_StoreBig( big_export );

    /* Group B — Shutdown: qboolean → refShutdownCode_t */
    vk_re_small.Shutdown = vk_re_wrap_Shutdown;

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

    vk_re_built = qtrue;
    return &vk_re_small;
}
