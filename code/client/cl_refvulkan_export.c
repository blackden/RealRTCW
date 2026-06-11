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

void *CL_BuildVulkanRefExport( void *big_export ) {
    if ( vk_re_built ) {
        return &vk_re_small;
    }

    Com_Memset( &vk_re_small, 0, sizeof( vk_re_small ) );

    /* Hand the BIG pointer to the sibling TU which holds the thunks. */
    CL_VulkanRefExport_StoreBig( big_export );

    /* M5 Task 1: skeleton only — every slot still NULL. First re.X call
     * from CL_InitRef caller will SIGSEGV; that's the wire-in proof.
     * Slot population follows in Tasks 2-7. */

    vk_re_built = qtrue;
    return &vk_re_small;
}
