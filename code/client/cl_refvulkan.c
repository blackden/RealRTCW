/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2010-2014 iortcw contributors
Copyright (C) 2026 RealRTCW contributors

This file is part of RealRTCW source code.

RealRTCW source code is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as published
by the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

RealRTCW source code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with RealRTCW source code; if not, write to the Free Software Foundation,
Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

/*
 * cl_refvulkan.c -- engine<->Vulkan-renderer vtable translator.
 * RealRTCW-authored. See cl_refvulkan.h for context.
 *
 * IMPORTANT: this TU MUST NOT include client.h. client.h pulls
 * code/renderer/tr_public.h which defines refimport_t as the SMALL
 * RTCW struct. This TU needs the BIG Quake3e struct from
 * code/renderercommon/tr_public.h. Including both causes a struct
 * redefinition error. We include the big one only and pull engine
 * function declarations directly from qcommon.h, plus locally
 * forward-declare any client-side symbols (CIN_*) we wire in.
 */

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../qcommon/cm_public.h"
#include "../renderercommon/tr_public.h"
#include "../sys/sys_local.h"
#include "cl_refvulkan.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
/* <vulkan/vulkan.h> is already transitively included via renderercommon/tr_public.h */

/* Engine-side SDL window pointer. Defined by sdl_input.c (code/sdl/sdl_input.c:60)
 * in the engine binary; populated by IN_Init when the renderer hands us its
 * window pointer via ri.IN_Init(). The renderer DLL has its own private copy
 * in sdl_glimp.c -- that one is not visible here.
 * Forward-declared (rather than via a shared header) because there is only
 * one consumer in the engine binary today (this TU). */
extern SDL_Window *SDL_window;

/* γ' migration: VKimp_Init is defined engine-side in code/sdl/sdl_glimp.c
 * (under #ifdef BUILD_RENDERER_VULKAN). The renderer DLL invokes it
 * through this slot; we wire the engine-side symbol directly.
 * See notes/decisions/2026-06-10-m4-window-ownership-model.md §4. */
extern void VKimp_Init( glconfig_t *config );
extern void VKimp_Shutdown( qboolean unloadDLL );

/* Forward declarations for client-side symbols normally exposed via
 * client.h. We avoid including client.h so the small refimport_t
 * doesn't pollute this TU. */
int      CIN_PlayCinematic( const char *arg0, int xpos, int ypos,
                            int width, int height, int bits );
e_status CIN_RunCinematic( int handle );
void     CIN_UploadCinematic( int handle );

/* Engine's print adapter shared with the OpenGL renderer path
 * (cl_main.c:3276). Format-string + level handling lives there as the
 * single source of truth; both renderers route through it. */
void QDECL CL_RefPrintf( int print_level, const char *fmt, ... );

/* The big refImport_t lives here as static storage. Filled lazily on
 * first call to CL_BuildVulkanRefImport; subsequent calls return the
 * same pointer. */
static refimport_t  vk_ri;
static qboolean     vk_ri_built = qfalse;

/* M5: BIG refexport_t cache. Set by cl_refvulkan_export.c during the
 * SMALL-side translator build. Thunks below invoke renderer entry
 * points through this pointer. */
static refexport_t *vk_re_big = NULL;

void CL_VulkanRefExport_StoreBig( void *big_export ) {
    vk_re_big = (refexport_t *)big_export;
}

/* ====================================================================
 * Forward declarations of stub/wrapper functions defined later in this
 * file. Grouped by category for readability.
 * ==================================================================== */

/* Wrappers (Q3e signature != engine signature) */
static int64_t   vk_Microseconds( void );
static void     *vk_Malloc( size_t bytes );
static void      vk_FreeAll( void );
static void     *vk_Hunk_Alloc( size_t size, ha_pref preference );
#ifdef HUNK_DEBUG
static void     *vk_Hunk_AllocDebug( size_t size, ha_pref preference, const char *label, const char *file, int line );
#endif
static void     *vk_Hunk_AllocateTempMemory( size_t size );
static int       vk_FS_ReadFile( const char *qpath, void **buffer );

/* No-op stubs for Q3e cvar API extensions not present in RealRTCW engine.
 * Description / validator / group metadata is non-load-bearing for rendering
 * — the renderer registers cvars and reads their values via Cvar_Get/Set;
 * dropping the metadata is functionally equivalent to never having set it. */
static void      vk_Cvar_SetDescription( cvar_t *cv, const char *description );
static void      vk_Cvar_CheckRange( cvar_t *cv, const char *minVal, const char *maxVal, cvarValidator_t type );
static void      vk_Cvar_SetGroup( cvar_t *var, cvarGroup_t group );
static int       vk_Cvar_CheckGroup( cvarGroup_t group );
static void      vk_Cvar_ResetGroup( cvarGroup_t group, qboolean resetModifiedFlags );
static void      vk_CL_SetScaling( float factor, int captureWidth, int captureHeight );
static void      vk_Free( void *ptr );
static void      vk_GLimp_InitGamma( glconfig_t *config );
static void      vk_GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] );
static qboolean  vk_CL_IsMinimized( void );
static void      vk_CL_LoadJPG( const char *filename, unsigned char **pic, int *width, int *height );

/* Vulkan-specific (no engine counterpart) */
static qboolean  vk_VK_CreateSurface( VkInstance instance, VkSurfaceKHR *pSurface );
static void     *vk_VK_GetInstanceProcAddr( VkInstance instance, const char *name );

/* ====================================================================
 * Local tagged-allocation tracker for vk_Malloc / vk_FreeAll.
 *
 * Engine's Z_TagMalloc / Z_FreeTags are dead code (common.c:832-873
 * is wrapped in #if 0). Z_Malloc is a bare malloc() with no tag
 * tracking, so we cannot route Q3e's tagged Malloc/FreeAll through it.
 * Instead we keep a private array of every pointer vk_Malloc returns
 * and free them all in vk_FreeAll.
 *
 * Sized for a few hundred renderer-owned allocations per init cycle;
 * fatal if exceeded so we notice early rather than silently leak.
 * ==================================================================== */

#define VK_RI_MAX_ALLOCS 4096

static void *vk_ri_allocs[VK_RI_MAX_ALLOCS];
static int   vk_ri_n_allocs = 0;

/* ====================================================================
 * Builder -- fills vk_ri and returns its address.
 * ==================================================================== */

void *CL_BuildVulkanRefImport( void ) {
    if ( vk_ri_built ) {
        return &vk_ri;
    }

    Com_Memset( &vk_ri, 0, sizeof( vk_ri ) );

    /* --- DIRECT ALIASES: slots whose Q3e name and signature match an
     *     existing engine function 1:1 (modulo a narrow cast).
     *
     *     Casts on the RHS are deliberately explicit and narrow:
     *     - Printf: Q3e expects (printParm_t, fmt, ...) — same signature
     *       as the engine's existing CL_RefPrintf adapter (cl_main.c).
     *       We do NOT cast Com_Printf directly: it has signature
     *       (fmt, ...) and the first arg slot would be misread as the
     *       format string, NULL-deref'ing vsnprintf at PRINT_ALL=0.
     *     - Error: engine's Com_Error already takes (int, fmt, ...);
     *       enum→int cast is signature-equivalent.
     *     - Cmd_AddCommand: engine takes xcommand_t (typedef of the
     *       same shape as Q3e's void(*)(void)) -- silent cast OK,
     *       made explicit.
     *     - Hunk_Alloc / Hunk_AllocateTempMemory / Hunk_AllocDebug:
     *       handled via vk_Hunk_* wrappers below — size_t→int
     *       narrowing made explicit (function-pointer cast worked by
     *       arm64 AAPCS accident, but it's UB by the C standard).
     *     - FS_ReadFile: handled via vk_FS_ReadFile wrapper —
     *       engine returns long, Q3e returns int; explicit (int)
     *       cast makes the narrowing visible.
     *     - CIN_PlayCinematic: engine and Q3e signatures match.
     *     - Cmd_ExecuteText: Q3e uses cbufExec_t (an enum), engine
     *       uses int -- compatible.
     *     - Cvar_VariableString: engine returns `char *`, Q3e wants
     *       `const char *`. Same data; cast is safe. */
    vk_ri.Printf                    = (void (QDECL *)( printParm_t, const char *, ... ))CL_RefPrintf;
    vk_ri.Error                     = (void (QDECL *)( errorParm_t, const char *, ... ))Com_Error;
    vk_ri.Milliseconds              = Sys_Milliseconds;
    vk_ri.Cmd_AddCommand            = (void (*)( const char *, void (*)( void ) ))Cmd_AddCommand;
    vk_ri.Cmd_RemoveCommand         = Cmd_RemoveCommand;
    vk_ri.Cmd_Argc                  = Cmd_Argc;
    vk_ri.Cmd_Argv                  = (const char *(*)( int ))Cmd_Argv;
    vk_ri.Cmd_ExecuteText           = (void (*)( cbufExec_t, const char * ))Cbuf_ExecuteText;
    /* HUNK_DEBUG is auto-defined in q_shared.h when !NDEBUG && !BSPC, and
     * tr_public.h mirrors the same toggle on the slot name. Match both
     * sides to whichever variant is active in this build. */
#ifdef HUNK_DEBUG
    vk_ri.Hunk_AllocDebug           = vk_Hunk_AllocDebug;
#else
    vk_ri.Hunk_Alloc                = vk_Hunk_Alloc;
#endif
    vk_ri.Hunk_AllocateTempMemory   = vk_Hunk_AllocateTempMemory;
    vk_ri.Hunk_FreeTempMemory       = Hunk_FreeTempMemory;
    vk_ri.FS_ReadFile               = vk_FS_ReadFile;
    vk_ri.FS_FreeFile               = FS_FreeFile;
    vk_ri.FS_WriteFile              = FS_WriteFile;
    vk_ri.FS_FreeFileList           = FS_FreeFileList;
    vk_ri.FS_ListFiles              = FS_ListFiles;
    vk_ri.FS_FileExists             = FS_FileExists;
    vk_ri.Cvar_Get                  = Cvar_Get;
    vk_ri.Cvar_Set                  = Cvar_Set;
    vk_ri.Cvar_SetValue             = Cvar_SetValue;
    vk_ri.Cvar_VariableIntegerValue = Cvar_VariableIntegerValue;
    vk_ri.Cvar_VariableString       = (const char *(*)( const char * ))Cvar_VariableString;
    vk_ri.Cvar_VariableStringBuffer = Cvar_VariableStringBuffer;
    vk_ri.CIN_UploadCinematic       = CIN_UploadCinematic;
    vk_ri.CIN_PlayCinematic         = CIN_PlayCinematic;
    vk_ri.CIN_RunCinematic          = CIN_RunCinematic;
    vk_ri.Com_RealTime              = Com_RealTime;
    vk_ri.Sys_LowPhysicalMemory     = Sys_LowPhysicalMemory;

    /* --- WRAPPERS: Q3e expects different signature than engine provides.
     *     Defined further down in this file. */
    vk_ri.Microseconds              = vk_Microseconds;
    vk_ri.Malloc                    = vk_Malloc;
    vk_ri.Free                      = vk_Free;
    vk_ri.FreeAll                   = vk_FreeAll;

    /* --- NO-OP STUBS: Q3e cvar metadata extensions (descriptions, ranges,
     *     groups) — engine has no equivalent storage but the renderer's
     *     R_Register calls them on every cvar, so they must be non-NULL.
     *     Functionally inert. */
    vk_ri.Cvar_SetDescription       = vk_Cvar_SetDescription;
    vk_ri.Cvar_CheckRange           = vk_Cvar_CheckRange;
    vk_ri.Cvar_SetGroup             = vk_Cvar_SetGroup;
    vk_ri.Cvar_CheckGroup           = vk_Cvar_CheckGroup;
    vk_ri.Cvar_ResetGroup           = vk_Cvar_ResetGroup;

    /* --- NO-OP STUB: Q3e renderer→engine scaling notification. RealRTCW
     *     engine has no equivalent plumbing (HUD assumes 1:1 with window).
     *     Safe no-op for boot; revisit if HUD coordinates drift. Called
     *     from code/renderervk/tr_init.c:543,556,562 right after VKimp_Init. */
    vk_ri.CL_SetScaling             = vk_CL_SetScaling;

    /* --- NO-OP STUBS: Q3e gamma slots called even on Vulkan path (cf.
     *     code/renderervk/tr_init.c:583 GLimp_InitGamma and
     *     code/renderervk/tr_image.c:1714 GLimp_SetGamma). MoltenVK does
     *     gamma via shader, not the OS gamma table -- safe no-op for boot.
     *     Iter 8 triage: docs/vulkan-phase2/2026-06-11-iter8-null-slot-sweep.md */
    vk_ri.GLimp_InitGamma           = vk_GLimp_InitGamma;
    vk_ri.GLimp_SetGamma            = vk_GLimp_SetGamma;

    /* --- NO-OP STUB: Q3e per-frame minimization query. Six callers in
     *     renderervk/ (vk.c:7371/7517/7543/7607, tr_cmds.c:89/95,
     *     tr_init.c:1069) -- first-frame landmine. Returns qfalse for now;
     *     wire to actual engine state when we add multi-window/visibility. */
    vk_ri.CL_IsMinimized            = vk_CL_IsMinimized;

    /* --- NO-OP STUB: JPG image loader. RealRTCW engine has no libjpeg
     *     integration. The image loader loop in R_LoadImage tries each
     *     extension; if vk_CL_LoadJPG returns *pic=NULL the fallback
     *     to TGA/PNG continues. JPG textures effectively unsupported on
     *     Vulkan path for now -- iter 10 fix. Add real decode later if
     *     game content actually ships JPG textures. */
    vk_ri.CL_LoadJPG                = vk_CL_LoadJPG;

    /* --- DIRECT WIRE: engine CM_ClusterPVS used by renderervk's BSP
     *     visibility code (tr_world.c). Signature matches. */
    vk_ri.CM_ClusterPVS             = CM_ClusterPVS;

    /* --- VULKAN WINDOW SYSTEM: code that doesn't exist on engine side.
     *     Defined further down. */
    vk_ri.VK_CreateSurface          = vk_VK_CreateSurface;
    vk_ri.VK_GetInstanceProcAddr    = vk_VK_GetInstanceProcAddr;
    vk_ri.VKimp_Init                = VKimp_Init;
    vk_ri.VKimp_Shutdown            = VKimp_Shutdown;

    /* --- INTENTIONALLY NULL: Q3e-only slots the RTCW engine has no
     *     equivalent for. Com_Memset above zeroed all slots; NULL
     *     function pointers remain NULL. Renderer crashes with clean
     *     NULL-deref signal if any of these gets called -- easy to
     *     diagnose in M4 logs.
     *
     *     Listed for grep-discoverability (alphabetical):
     *       CL_SaveJPG           -- not present in RealRTCW engine
     *       CL_SaveJPGToBuffer   -- not present in RealRTCW engine
     *       CL_WriteAVIVideoFrame-- engine sig (const byte*,int) vs
     *                              Q3e sig matches; could wire if
     *                              renderer triages it. Leave NULL
     *                              for now -- AVI capture not in M4.
     *       CM_DrawDebugSurface  -- not exported via qcommon.h
     *       GLimp_EndFrame       -- OpenGL window slot, not Vulkan
     *       GLimp_Init           -- OpenGL window slot, not Vulkan
     *       GLimp_Shutdown       -- OpenGL window slot, not Vulkan
     *       GL_GetProcAddress    -- OpenGL window slot, not Vulkan
     *       Sys_SetClipboardBitmap -- not present in RealRTCW engine
     *
     *     The refexport_t slots (AddAdditiveLightToScene,
     *     AddLinearLightToScene, FinishBloom, ThrottleBackend,
     *     SetColorMappings, CanMinimize, VertexLighting, SyncRender,
     *     inPVS) belong to the RENDERER -> ENGINE direction and are
     *     filled by the renderer's GetRefAPI, not by this translator.
     *     They're handled by Task 4's refexport adapter, not here. */

    vk_ri_built = qtrue;
    return &vk_ri;
}

/* ====================================================================
 * WRAPPERS -- Q3e signature != engine signature.
 *
 * vk_Malloc / vk_FreeAll use a local tracker (see above) because
 * Z_TagMalloc / Z_FreeTags in common.c are #if 0'd dead code.
 * vk_Hunk_* and vk_FS_ReadFile make size_t→int / long→int narrowings
 * explicit so the calling convention doesn't silently drop bits.
 * ==================================================================== */

/* Q3e wants microseconds as int64_t; engine has Sys_Milliseconds
 * returning int. Promote to int64_t before multiplying to avoid
 * overflow at ~24 days uptime. */
static int64_t vk_Microseconds( void ) {
    return (int64_t)Sys_Milliseconds() * 1000;
}

static void *vk_Malloc( size_t bytes ) {
    void *ptr = malloc( bytes );
    if ( !ptr ) {
        Com_Error( ERR_FATAL, "vk_Malloc: out of memory (requested %zu bytes)", bytes );
        return NULL;
    }
    Com_Memset( ptr, 0, bytes );

    if ( vk_ri_n_allocs >= VK_RI_MAX_ALLOCS ) {
        Com_Error( ERR_FATAL, "vk_Malloc: tracker full (>%d allocs) -- bump VK_RI_MAX_ALLOCS or audit renderervk for runaway allocations", VK_RI_MAX_ALLOCS );
        return NULL;
    }
    vk_ri_allocs[vk_ri_n_allocs++] = ptr;
    return ptr;
}

static void vk_FreeAll( void ) {
    int i;
    for ( i = 0; i < vk_ri_n_allocs; i++ ) {
        free( vk_ri_allocs[i] );
        vk_ri_allocs[i] = NULL;
    }
    vk_ri_n_allocs = 0;
}

/* M4 iter 7 fix: Q3e renderer calls ri.Free(ptr) on individual allocations
 * (e.g., extension_names + extension_properties in vk_initialize at
 * code/renderervk/vk.c:1382,1383). Was NULL in the translator, causing
 * SIGSEGV right after qvkCreateInstance. Find ptr in the alloc tracker,
 * swap with last, decrement count, free. Silent no-op if not in tracker
 * (allocations not made via vk_Malloc -- shouldn't happen but defensive). */
static void vk_Free( void *ptr ) {
    int i;
    if ( !ptr ) {
        return;
    }
    for ( i = 0; i < vk_ri_n_allocs; i++ ) {
        if ( vk_ri_allocs[i] == ptr ) {
            vk_ri_allocs[i] = vk_ri_allocs[--vk_ri_n_allocs];
            vk_ri_allocs[vk_ri_n_allocs] = NULL;
            break;
        }
    }
    free( ptr );
}

/* Q3e GLimp_InitGamma is called on Vulkan path too (renderervk/tr_init.c:583).
 * MoltenVK doesn't expose OS gamma tables — gamma is applied via shader.
 * Mark deviceSupportsGamma=qfalse so engine doesn't try OS gamma adjustments. */
static void vk_GLimp_InitGamma( glconfig_t *config ) {
    if ( config ) {
        config->deviceSupportsGamma = qfalse;
    }
}

/* Q3e GLimp_SetGamma is invoked from R_SetColorMappings (tr_image.c:1714)
 * unconditionally after GLimp_InitGamma. Since we report
 * deviceSupportsGamma=qfalse, the renderer should skip — but be defensive. */
static void vk_GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] ) {
    (void)red; (void)green; (void)blue;
}

/* Q3e per-frame minimization query — 6 callers in renderervk/. RealRTCW
 * doesn't track minimization state (no multi-window). Returns qfalse;
 * wire to engine state if/when we add visibility tracking. */
static qboolean vk_CL_IsMinimized( void ) {
    return qfalse;
}

/* JPG loader stub. Leaves *pic NULL so the renderer's R_LoadImage
 * format-search-loop falls through to the next extension (TGA, PNG).
 * Game content for stock RTCW SP doesn't ship JPG textures, so this
 * is observationally inert. Wire to real libjpeg if/when needed. */
static void vk_CL_LoadJPG( const char *filename, unsigned char **pic, int *width, int *height ) {
    (void)filename;
    if ( pic ) *pic = NULL;
    if ( width ) *width = 0;
    if ( height ) *height = 0;
}

/* Q3e cvar metadata API — no engine storage exists. Each is a no-op:
 * descriptions are documentation, validators are accepted-without-enforce,
 * groups are not tracked. The renderer doesn't read these back, so
 * dropping the writes is observationally indistinguishable. */
static void vk_Cvar_SetDescription( cvar_t *cv, const char *description ) {
    (void)cv; (void)description;
}

static void vk_Cvar_CheckRange( cvar_t *cv, const char *minVal, const char *maxVal, cvarValidator_t type ) {
    (void)cv; (void)minVal; (void)maxVal; (void)type;
}

static void vk_Cvar_SetGroup( cvar_t *var, cvarGroup_t group ) {
    (void)var; (void)group;
}

static int vk_Cvar_CheckGroup( cvarGroup_t group ) {
    (void)group;
    return 0;
}

static void vk_Cvar_ResetGroup( cvarGroup_t group, qboolean resetModifiedFlags ) {
    (void)group; (void)resetModifiedFlags;
}

/* CL_SetScaling: Q3e renderer tells engine the render→display scale
 * factor and capture dimensions. Used for HUD/mouse coordinate
 * scaling in the Q3e client. RealRTCW engine has no equivalent
 * plumbing -- HUD assumes 1:1 with window. Safe no-op for boot;
 * revisit if HUD coordinates drift in Phase 3+. Called from
 * code/renderervk/tr_init.c:543, 556, 562 right after VKimp_Init. */
static void vk_CL_SetScaling( float factor, int captureWidth, int captureHeight ) {
    (void)factor; (void)captureWidth; (void)captureHeight;
}

/* Q3e Hunk_Alloc family takes size_t; engine takes int. Narrow with an
 * explicit cast so the truncation is visible and the calling convention
 * doesn't quietly drop the upper 32 bits. */
static void *vk_Hunk_Alloc( size_t size, ha_pref preference ) {
    return Hunk_Alloc( (int)size, preference );
}

#ifdef HUNK_DEBUG
static void *vk_Hunk_AllocDebug( size_t size, ha_pref preference, const char *label, const char *file, int line ) {
    /* Engine takes char*; Q3e slot passes const char*. Cast away const --
     * Hunk_AllocDebug only reads the strings (label/file are __FILE__-shape
     * literals in normal use). */
    return Hunk_AllocDebug( (int)size, preference, (char *)label, (char *)file, line );
}
#endif

static void *vk_Hunk_AllocateTempMemory( size_t size ) {
    return Hunk_AllocateTempMemory( (int)size );
}

/* Engine returns long; Q3e slot wants int. Explicit cast makes the
 * truncation visible -- files >2GiB or any future widening would
 * silently break, and we want the cast to flag that. */
static int vk_FS_ReadFile( const char *qpath, void **buffer ) {
    return (int)FS_ReadFile( qpath, buffer );
}

/* ====================================================================
 * VULKAN WINDOW SYSTEM -- SDL3-backed implementations.
 *
 * The engine's shared SDL window is created by sdl_glimp.c:GLimp_Init
 * before the renderer DLL is loaded. The Vulkan-renderer side just
 * grabs the loader entry point and creates a VkSurfaceKHR for that
 * already-existing window on demand.
 * ==================================================================== */

static qboolean vk_VK_CreateSurface( VkInstance instance, VkSurfaceKHR *pSurface ) {
    /* Creates a VkSurfaceKHR for the engine's SDL window. The renderer
     * passes its VkInstance handle (opaque to engine) and a pointer to
     * where the VkSurfaceKHR handle should land.
     *
     * SDL3's SDL_Vulkan_CreateSurface does the platform-specific work
     * (Metal layer wrap on macOS via MoltenVK). */
    if ( !SDL_window ) {
        Com_Printf( S_COLOR_RED "vk_VK_CreateSurface: SDL_window is NULL -- was GLimp/VKimp init called?\n" );
        return qfalse;
    }

    if ( !SDL_Vulkan_CreateSurface( SDL_window, instance, NULL /* allocator */, pSurface ) ) {
        Com_Printf( S_COLOR_RED "SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError() );
        return qfalse;
    }
    return qtrue;
}

static void *vk_VK_GetInstanceProcAddr( VkInstance instance, const char *name ) {
    /* Returns address of a Vulkan function from the Vulkan loader.
     * Used by the renderer to resolve vkCreateInstance, vkGetDeviceProcAddr,
     * and the global instance-level entry points.
     *
     * SDL3 exposes the loader's vkGetInstanceProcAddr via this getter,
     * which wraps the platform-native loader from libvulkan.dylib. */
    static PFN_vkGetInstanceProcAddr loader = NULL;
    if ( !loader ) {
        loader = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
        if ( !loader ) {
            Com_Error( ERR_FATAL, "SDL_Vulkan_GetVkGetInstanceProcAddr: %s", SDL_GetError() );
            return NULL;
        }
    }
    /* When name is NULL, Q3e callers want the loader entry itself (rare
     * convention but seen in the wild). Most calls pass a real function
     * name. Pass `instance` and `name` straight through. */
    return (void *)loader( instance, name );
}

/* ====================================================================
 * M5 Group A — slot accessors for identity-signature slots.
 * Each returns (void *) so cl_refvulkan_export.c can assign without
 * pulling in BIG type declarations.
 * ==================================================================== */

#define VK_RE_GET(name)  void *vk_re_get_##name( void ) { return (void *)vk_re_big->name; }

VK_RE_GET(BeginRegistration)
VK_RE_GET(RegisterModel)
VK_RE_GET(RegisterSkin)
VK_RE_GET(RegisterShader)
VK_RE_GET(RegisterShaderNoMip)
VK_RE_GET(LoadWorld)
VK_RE_GET(SetWorldVisData)
VK_RE_GET(EndRegistration)
VK_RE_GET(ClearScene)
VK_RE_GET(LightForPoint)
VK_RE_GET(RenderScene)
VK_RE_GET(SetColor)
VK_RE_GET(DrawStretchPic)
VK_RE_GET(BeginFrame)
VK_RE_GET(EndFrame)
VK_RE_GET(MarkFragments)
VK_RE_GET(ModelBounds)
VK_RE_GET(RegisterFont)
VK_RE_GET(RemapShader)
VK_RE_GET(GetEntityToken)
VK_RE_GET(TakeVideoFrame)

#undef VK_RE_GET

/* ====================================================================
 * M5 Group B — signature-translating thunks.
 * Each takes int / void * args from the SMALL-side wrapper and calls
 * the BIG slot with proper BIG-shaped enums / types.
 * ==================================================================== */

void vk_re_thunk_Shutdown( int code ) {
    vk_re_big->Shutdown( (refShutdownCode_t)code );
}

void vk_re_thunk_AddRefEntityToScene( const void *re_ptr, int intShaderTime ) {
    /* re_ptr originates from engine-side SMALL refEntity_t. SMALL and BIG
     * tr_types.h diverge — boot-critical fields are believed to overlap
     * (proven by vk_BuildRefImport working through R_Init), but scene-
     * rendering field reads beyond the overlap zone are M6 territory. */
    vk_re_big->AddRefEntityToScene( (const refEntity_t *)re_ptr,
                                    intShaderTime ? qtrue : qfalse );
}

void vk_re_thunk_AddPolyToScene( qhandle_t hShader, int numVerts, const void *verts, int num ) {
    vk_re_big->AddPolyToScene( hShader, numVerts, (const polyVert_t *)verts, num );
}

void vk_re_thunk_AddLightToScene( const float *org, float intensity, float r, float g, float b ) {
    /* SMALL passes `int overdraw`; BIG has no such arg. Drop it. The
     * overdraw flag was an RTCW-specific dynamic-light visibility hint;
     * the Q3e renderer just renders the light. */
    vk_re_big->AddLightToScene( (vec_t *)org, intensity, r, g, b );
}

int vk_re_thunk_LerpTag( void *tag, int hModel, int startFrame, int endFrame,
                         float frac, const char *tagName ) {
    return vk_re_big->LerpTag( (orientation_t *)tag,
                               (qhandle_t)hModel,
                               startFrame, endFrame, frac, tagName );
}

void vk_re_thunk_DrawStretchRaw( int x, int y, int w, int h, int cols, int rows,
                                 const byte *data, int client, int dirty ) {
    /* BIG signature drops const on data ptr but renderer never writes
     * through it. Cast away const here, alone, so the unsafe cast is
     * grep-locatable. */
    vk_re_big->DrawStretchRaw( x, y, w, h, cols, rows,
                               (byte *)data, client, dirty ? qtrue : qfalse );
}

void vk_re_thunk_UploadCinematic( int w, int h, int cols, int rows,
                                  const byte *data, int client, int dirty ) {
    /* Same const-strip pattern as DrawStretchRaw — UploadCinematic copies
     * data into a renderer-owned texture, never writes through the input. */
    vk_re_big->UploadCinematic( w, h, cols, rows,
                                (byte *)data, client, dirty ? qtrue : qfalse );
}
