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
#include "../renderercommon/tr_public.h"
#include "../sys/sys_local.h"
#include "cl_refvulkan.h"

/* Forward declarations for client-side symbols normally exposed via
 * client.h. We avoid including client.h so the small refimport_t
 * doesn't pollute this TU. */
int      CIN_PlayCinematic( const char *arg0, int xpos, int ypos,
                            int width, int height, int bits );
e_status CIN_RunCinematic( int handle );
void     CIN_UploadCinematic( int handle );

/* The big refImport_t lives here as static storage. Filled lazily on
 * first call to CL_BuildVulkanRefImport; subsequent calls return the
 * same pointer. */
static refimport_t  vk_ri;
static qboolean     vk_ri_built = qfalse;

/* ====================================================================
 * Forward declarations of stub/wrapper functions defined later in this
 * file. Grouped by category for readability.
 * ==================================================================== */

/* Wrappers (Q3e signature != engine signature) */
static int64_t   vk_Microseconds( void );
static void     *vk_Malloc( size_t bytes );
static void      vk_FreeAll( void );

/* Vulkan-specific (no engine counterpart) */
static qboolean  vk_VK_CreateSurface( VkInstance instance, VkSurfaceKHR *pSurface );
static void     *vk_VK_GetInstanceProcAddr( VkInstance instance, const char *name );
static void      vk_VKimp_Init( glconfig_t *config );
static void      vk_VKimp_Shutdown( qboolean unloadDLL );

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
     *     - Printf/Error: engine uses int for printLevel/errorLevel,
     *       Q3e uses typed enums (printParm_t/errorParm_t).
     *     - Cmd_AddCommand: engine takes xcommand_t (typedef of the
     *       same shape as Q3e's void(*)(void)) -- silent cast OK,
     *       made explicit.
     *     - Hunk_Alloc / Hunk_AllocateTempMemory / Malloc: engine uses
     *       int sizes, Q3e uses size_t. Pointer-to-function cast
     *       silences -Wincompatible-pointer-types; values fit unless
     *       a >2GiB allocation happens (won't on a Q3 renderer).
     *     - FS_ReadFile: engine returns long, Q3e returns int. Same
     *       caveat; will warn on negative sentinel? FS_ReadFile only
     *       returns -1 / length, both representable.
     *     - CIN_PlayCinematic: engine and Q3e signatures match.
     *     - Cmd_ExecuteText: Q3e uses cbufExec_t (an enum), engine
     *       uses int -- compatible.
     *     - Cvar_VariableString: engine returns `char *`, Q3e wants
     *       `const char *`. Same data; cast is safe. */
    vk_ri.Printf                    = (void (QDECL *)( printParm_t, const char *, ... ))Com_Printf;
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
    vk_ri.Hunk_AllocDebug           = (void *(*)( size_t, ha_pref, const char *, const char *, int ))Hunk_AllocDebug;
#else
    vk_ri.Hunk_Alloc                = (void *(*)( size_t, ha_pref ))Hunk_Alloc;
#endif
    vk_ri.Hunk_AllocateTempMemory   = (void *(*)( size_t ))Hunk_AllocateTempMemory;
    vk_ri.Hunk_FreeTempMemory       = Hunk_FreeTempMemory;
    vk_ri.FS_ReadFile               = (int (*)( const char *, void ** ))FS_ReadFile;
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
    vk_ri.FreeAll                   = vk_FreeAll;

    /* --- VULKAN WINDOW SYSTEM: code that doesn't exist on engine side.
     *     Defined further down. */
    vk_ri.VK_CreateSurface          = vk_VK_CreateSurface;
    vk_ri.VK_GetInstanceProcAddr    = vk_VK_GetInstanceProcAddr;
    vk_ri.VKimp_Init                = vk_VKimp_Init;
    vk_ri.VKimp_Shutdown            = vk_VKimp_Shutdown;

    /* --- INTENTIONALLY NULL: Q3e-only slots the RTCW engine has no
     *     equivalent for. Com_Memset above zeroed all slots; NULL
     *     function pointers remain NULL. Renderer crashes with clean
     *     NULL-deref signal if any of these gets called -- easy to
     *     diagnose in M4 logs.
     *
     *     Listed for grep-discoverability (alphabetical):
     *       CL_IsMinimized       -- not present in RealRTCW engine
     *       CL_LoadJPG           -- not present in RealRTCW engine
     *       CL_SaveJPG           -- not present in RealRTCW engine
     *       CL_SaveJPGToBuffer   -- not present in RealRTCW engine
     *       CL_SetScaling        -- not present in RealRTCW engine
     *       CL_WriteAVIVideoFrame-- engine sig (const byte*,int) vs
     *                              Q3e sig matches; could wire if
     *                              renderer triages it. Leave NULL
     *                              for now -- AVI capture not in M4.
     *       CM_ClusterPVS        -- not exported via qcommon.h
     *       CM_DrawDebugSurface  -- not exported via qcommon.h
     *       Cvar_CheckGroup      -- Q3e cvar group system not in engine
     *       Cvar_CheckRange      -- signature mismatch (engine: float
     *                              minVal/maxVal/qboolean; Q3e: const
     *                              char* minVal/maxVal/cvarValidator_t)
     *                              -- needs a real wrapper if renderer
     *                              calls it; defer to M4 triage.
     *       Cvar_ResetGroup      -- Q3e cvar group system not in engine
     *       Cvar_SetDescription  -- not present in RealRTCW engine
     *       Cvar_SetGroup        -- Q3e cvar group system not in engine
     *       Free                 -- engine has no Z_Free that takes a
     *                              raw pointer the renderer would own;
     *                              renderer typically pairs Malloc with
     *                              FreeAll. If triage shows otherwise,
     *                              wire to Z_Free.
     *       GLimp_EndFrame       -- OpenGL window slot, not Vulkan
     *       GLimp_Init           -- OpenGL window slot, not Vulkan
     *       GLimp_InitGamma      -- OpenGL window slot, not Vulkan
     *       GLimp_SetGamma       -- OpenGL window slot, not Vulkan
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
 * ==================================================================== */

/* Q3e wants microseconds as int64_t; engine has Sys_Milliseconds
 * returning int. Promote to int64_t before multiplying to avoid
 * overflow at ~24 days uptime. */
static int64_t vk_Microseconds( void ) {
    return (int64_t)Sys_Milliseconds() * 1000;
}

/* Q3e Malloc takes size_t; engine's Z_Malloc takes int. Narrow with a
 * cast -- a >2GiB single allocation from a Q3 renderer would already
 * be a bug. */
static void *vk_Malloc( size_t bytes ) {
    return Z_Malloc( (int)bytes );
}

/* Q3e FreeAll = "release every tagged allocation owned by the renderer".
 * Engine's Z_FreeTags is the equivalent. TAG_RENDERER exists in the
 * engine's memtag_t enum (see qcommon.h:948), so use it directly --
 * tighter than TAG_GENERAL and matches the renderer's ownership. */
static void vk_FreeAll( void ) {
    Z_FreeTags( TAG_RENDERER );
}

/* ====================================================================
 * VULKAN WINDOW SYSTEM -- placeholder stubs.
 * Task 5 will fill these with real SDL3/Vulkan code. For now they
 * Com_Error so we get an explicit panic if the renderer reaches them
 * before Task 5 lands.
 * ==================================================================== */

static qboolean vk_VK_CreateSurface( VkInstance instance, VkSurfaceKHR *pSurface ) {
    (void)instance; (void)pSurface;
    Com_Error( ERR_FATAL, "vk_VK_CreateSurface: stub -- Task 5 not yet landed" );
    return qfalse;
}

static void *vk_VK_GetInstanceProcAddr( VkInstance instance, const char *name ) {
    (void)instance; (void)name;
    Com_Error( ERR_FATAL, "vk_VK_GetInstanceProcAddr: stub -- Task 5 not yet landed" );
    return NULL;
}

static void vk_VKimp_Init( glconfig_t *config ) {
    (void)config;
    Com_Error( ERR_FATAL, "vk_VKimp_Init: stub -- Task 5 not yet landed" );
}

static void vk_VKimp_Shutdown( qboolean unloadDLL ) {
    (void)unloadDLL;
    /* idempotent stub -- safe to no-op on shutdown */
}
