/*
===========================================================================
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
#ifndef __CL_REFVULKAN_H__
#define __CL_REFVULKAN_H__

/*
 * cl_refvulkan.h — public surface of the engine↔Vulkan-renderer
 * vtable translator.
 *
 * The vendored Quake3e Vulkan renderer (code/renderervk/) reads its
 * refImport_t from code/renderercommon/tr_public.h, which has a
 * different (disjoint) vocabulary from the engine-native small struct
 * in code/renderer/tr_public.h (which engine populates as `ri` in
 * CL_InitRef).
 *
 * This translator allocates a statically-stored big-shaped refImport_t
 * and fills it slot-by-slot. CL_InitRef hands the resulting pointer to
 * GetRefAPI when BUILD_RENDERER_VULKAN is active.
 *
 * Returns: opaque pointer to a fully-populated big refImport_t (size
 * known only to the translator's TU and the renderer DLL). Caller
 * passes it through as `refimport_t *` cast — pointer width is the
 * only thing the call ABI cares about.
 */
void *CL_BuildVulkanRefImport( void );

/* M5: refexport_t translator (renderer → engine direction).
 *
 * Takes the BIG refexport_t pointer returned by the vendored renderervk
 * GetRefAPI and returns a pointer to a SMALL refexport_t whose slots are
 * wired (directly or via wrappers) to the BIG slots.
 *
 * Opaque return type because cl_main.c (the only caller) sees SMALL but
 * this header is included from cl_refvulkan.c too which sees BIG. The
 * caller casts back to SMALL refexport_t * at the use site.
 *
 * Idempotent: subsequent calls return the same pointer; second BIG arg
 * is ignored. */
void *CL_BuildVulkanRefExport( void *big_export );

/* BIG-side thunk storage. Setter called from cl_refvulkan_export.c
 * during CL_BuildVulkanRefExport; thunks below read this pointer. */
void  CL_VulkanRefExport_StoreBig( void *big_export );

#endif /* __CL_REFVULKAN_H__ */
