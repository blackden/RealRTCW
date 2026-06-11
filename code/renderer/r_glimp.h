/*
===========================================================================
RealRTCW source code is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as published
by the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
===========================================================================
*/
#ifndef __R_GLIMP_H__
#define __R_GLIMP_H__

#include "tr_local.h"

/* gamma' Task 2a (notes/decisions/2026-06-10-m4-window-ownership-model.md):
 * Renderer-internal GL state probing. Engine-side sdl_glimp.c creates the
 * SDL window and GL context; this TU populates qgl* function pointers and
 * the renderer-side glConfig string fields, and detects GL extensions.
 *
 * Called from R_Init() after ri.GLimp_Init() returns to the renderer DLL.
 */

/* Probe qgl* pointers, populate glConfig.vendor/renderer/version/extensions
 * strings, detect GL extensions. Returns qfalse if qgl* probe failed or the
 * underlying GL_RENDERER is a software rasterizer. */
qboolean GLimp_RendererInit( qboolean fixedFunction );

/* Clear qgl* pointers. Called from R_Shutdown. */
void GLimp_RendererShutdown( void );

#endif /* __R_GLIMP_H__ */
