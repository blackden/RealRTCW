/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2010-2014 iortcw contributors
Copyright (C) 2026 RealRTCW contributors

This file contains a verbatim port of r_vidModes[] and R_GetModeInfo from
iortcw SP code/renderer/tr_init.c (GPLv2), surrounded by RealRTCW-authored
bridge glue. It is part of RealRTCW source code.

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
===========================================================================
RealRTCW Vulkan window/cvar bridge — RealRTCW-authored.

Quake3e's vendored renderervk dropped the renderer-side global cvars and
the R_GetModeInfo helper that RealRTCW's shared code/sdl/sdl_glimp.c
still expects (it was written against the iortcw/RTCW SP renderer ABI).
This translation unit re-supplies the 11 symbols sdl_glimp.c pulls as
externs from code/renderer/tr_local.h, so the Vulkan renderer DLL links
without having to fork sdl_glimp.c:

    cvar_t *r_mode, r_fullscreen, r_noborder, r_colorbits,
            r_depthbits, r_stencilbits, r_stereoEnabled, r_swapInterval;
    float    displayAspect;
    qboolean haveClampToEdge;
    qboolean R_GetModeInfo(int *w, int *h, float *aspect, int mode);

The cvar pointers are latched via ri.Cvar_Get in RealRTCW_VkBridgeInit
using the same defaults/flags as code/renderer/tr_init.c. Defaults and
flags are kept in sync deliberately so the engine-OpenGL DLL and the
Vulkan DLL register identical cvars.

R_GetModeInfo (incl. r_vidModes[] table) is ported verbatim from
iortcw SP code/renderer/tr_init.c lines 346–416 (r_vidModes at L354–L386,
R_GetModeInfo at L389–L416). The only deviation: direct dereferences of
r_customwidth / r_customheight / r_customPixelAspect were replaced by
ri.Cvar_VariableIntegerValue / atof(ri.Cvar_VariableString(...)) lookups
(the Quake3e refimport_t vtable has no Cvar_VariableValue), since the
bridge cannot assume those cvars are renderer-side globals in this DLL.

This file has no effect on the engine-OpenGL renderer DLL.
===========================================================================
*/

#include "tr_local.h"
#include "realrtcw_vk_window_bridge.h"

/* ---------------------------------------------------------------------- *
 * Tentative global definitions for symbols sdl_glimp.c expects as
 * externs (declared in code/renderer/tr_local.h on the engine-OpenGL
 * side).  Same storage types, same names — that's what the link needs.
 * ---------------------------------------------------------------------- */

cvar_t *r_mode;
cvar_t *r_fullscreen;
cvar_t *r_noborder;
cvar_t *r_colorbits;
cvar_t *r_depthbits;
cvar_t *r_stencilbits;
cvar_t *r_stereoEnabled;
cvar_t *r_swapInterval;

float    displayAspect;
qboolean haveClampToEdge;

/* ---------------------------------------------------------------------- *
 * r_vidModes[] table — verbatim port from
 *   refs/iortcw/SP/code/renderer/tr_init.c L354–L386
 * Keep ordering and pixelAspect values identical so cl_main.c and the
 * UI mode list agree across the two renderer DLLs.
 * ---------------------------------------------------------------------- */

typedef struct vidmode_s
{
	const char *description;
	int width, height;
	float pixelAspect;              /* pixel width / height */
} vidmode_t;

/* Note: Also add these modes to ui/ui_shared.c */
static const vidmode_t r_vidModes[] =
{
	{ "Mode  0:   320x240   (4:3)",     320,     240,    1 },
	{ "Mode  1:   400x300   (4:3)",     400,     300,    1 },
	{ "Mode  2:   512x384   (4:3)",     512,     384,    1 },
	{ "Mode  3:   640x480   (4:3)",     640,     480,    1 },
	{ "Mode  4:   800x600   (4:3)",     800,     600,    1 },
	{ "Mode  5:   960x720   (4:3)",     960,     720,    1 },
	{ "Mode  6:  1024x768   (4:3)",    1024,     768,    1 },
	{ "Mode  7:  1152x864   (4:3)",    1152,     864,    1 },
	{ "Mode  8: 1280x1024   (5:4)",    1280,    1024,    1 },
	{ "Mode  9: 1600x1200   (4:3)",    1600,    1200,    1 },
	{ "Mode 10: 2048x1536   (4:3)",    2048,    1536,    1 },
	{ "Mode 11:   856x480  (16:9)",     856,     480,    1 },
	{ "Mode 12:   640x360  (16:9)",     640,     360,    1 },
	{ "Mode 13:   640x400 (16:10)",     640,     400,    1 },
	{ "Mode 14:   800x450  (16:9)",     800,     450,    1 },
	{ "Mode 15:   800x500 (16:10)",     800,     500,    1 },
	{ "Mode 16:  1024x640 (16:10)",    1024,     640,    1 },
	{ "Mode 17:  1024x576  (16:9)",    1024,     576,    1 },
	{ "Mode 18:  1280x720  (16:9)",    1280,     720,    1 },
	{ "Mode 19:  1280x768 (16:10)",    1280,     768,    1 },
	{ "Mode 20:  1280x800 (16:10)",    1280,     800,    1 },
	{ "Mode 21:  1280x960   (4:3)",    1280,     960,    1 },
	{ "Mode 22:  1440x900 (16:10)",    1440,     900,    1 },
	{ "Mode 23:  1600x900  (16:9)",    1600,     900,    1 },
	{ "Mode 24: 1600x1000 (16:10)",    1600,    1000,    1 },
	{ "Mode 25: 1680x1050 (16:10)",    1680,    1050,    1 },
	{ "Mode 26: 1920x1080  (16:9)",    1920,    1080,    1 },
	{ "Mode 27: 1920x1200 (16:10)",    1920,    1200,    1 },
	{ "Mode 28: 1920x1440   (4:3)",    1920,    1440,    1 },
	{ "Mode 29: 2560x1600 (16:10)",    2560,    1600,    1 }
};
static const int s_numVidModes = (int)(sizeof(r_vidModes) / sizeof(r_vidModes[0]));

/* ---------------------------------------------------------------------- *
 * R_GetModeInfo — ported from iortcw SP tr_init.c L389–L416. Only the
 * custom-mode branch deviates: this DLL doesn't own the r_custom*
 * cvars as globals, so we look them up by name via the refimport_t
 * vtable. Same semantics, identical math.
 * ---------------------------------------------------------------------- */

qboolean R_GetModeInfo( int *width, int *height, float *windowAspect, int mode ) {
	const vidmode_t *vm;
	float            pixelAspect;

	if ( mode < -1 ) {
		return qfalse;
	}
	if ( mode >= s_numVidModes ) {
		return qfalse;
	}

	if ( mode == -1 ) {
		*width  = ri.Cvar_VariableIntegerValue( "r_customwidth" );
		*height = ri.Cvar_VariableIntegerValue( "r_customheight" );

		pixelAspect = (float)atof( ri.Cvar_VariableString( "r_customPixelAspect" ) );
	} else {
		vm = &r_vidModes[mode];

		*width  = vm->width;
		*height = vm->height;
		pixelAspect = vm->pixelAspect;
	}

	*windowAspect = (float)*width / ( *height * pixelAspect );

	return qtrue;
}

/* ---------------------------------------------------------------------- *
 * RealRTCW_VkBridgeInit — latches the 8 cvar pointers and seeds the
 * two scalar globals. Idempotent: subsequent calls are no-ops. Called
 * from sdl_glimp.c (in a follow-up commit) before any code that may
 * dereference these cvars.
 *
 * Defaults and flags MUST track code/renderer/tr_init.c:1241–1306
 * (engine-OpenGL renderer). If you change one here, change the other.
 * ---------------------------------------------------------------------- */

void RealRTCW_VkBridgeInit(void) {
	static qboolean initialized = qfalse;

	if ( initialized ) {
		return;
	}
	initialized = qtrue;

	r_mode          = ri.Cvar_Get( "r_mode",          "-2", CVAR_ARCHIVE | CVAR_LATCH );
	r_fullscreen    = ri.Cvar_Get( "r_fullscreen",    "1",  CVAR_ARCHIVE | CVAR_LATCH );
	r_noborder      = ri.Cvar_Get( "r_noborder",      "0",  CVAR_ARCHIVE | CVAR_LATCH );
	r_colorbits     = ri.Cvar_Get( "r_colorbits",     "0",  CVAR_ARCHIVE | CVAR_LATCH );
	r_depthbits     = ri.Cvar_Get( "r_depthbits",     "0",  CVAR_ARCHIVE | CVAR_LATCH );
	r_stencilbits   = ri.Cvar_Get( "r_stencilbits",   "0",  CVAR_ARCHIVE | CVAR_LATCH );
	r_stereoEnabled = ri.Cvar_Get( "r_stereoEnabled", "0",  CVAR_ARCHIVE | CVAR_LATCH );
	r_swapInterval  = ri.Cvar_Get( "r_swapInterval",  "0",  CVAR_ARCHIVE | CVAR_LATCH );

	displayAspect    = 0.0f;
	haveClampToEdge  = qtrue;
}
