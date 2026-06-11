/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#ifdef USE_LOCAL_HEADERS
#	include "SDL3/SDL.h"
#else
#	include <SDL3/SDL.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "../renderer/tr_local.h"
#include "../sys/sys_local.h"
#include "sdl_icon.h"

#ifdef BUILD_RENDERER_VULKAN
/* Populates the renderer-side cvar globals + R_GetModeInfo this TU expects
 * when linked into renderer_sp_vulkan_<arch>.dylib. Guard is set per-target
 * in Makefile (see "$(B)/rendv/sdl_glimp.o: CFLAGS += ..."). */
#include "../renderervk/realrtcw_vk_window_bridge.h"
#endif

/* OpenGL ES compat helpers (USE_OPENGLES) moved to code/renderer/r_glimp.c
 * (gamma' Task 2a). */

typedef enum
{
	RSERR_OK,

	RSERR_INVALID_FULLSCREEN,
	RSERR_INVALID_MODE,

	RSERR_UNKNOWN
} rserr_t;

SDL_Window *SDL_window = NULL;
static SDL_GLContext SDL_glContext = NULL;

cvar_t *r_allowSoftwareGL; // Don't abort out if a hardware visual can't be obtained
cvar_t *r_allowResize; // make window resizable
cvar_t *r_centerWindow;
cvar_t *r_sdlDriver;

/* qgl* function pointer DEFINITIONS moved to code/renderer/r_glimp.c
 * (gamma' Task 2a). Renderer DLL's R_Init calls GLimp_RendererInit()
 * after this engine-side GLimp_Init returns. */

/*
===============
GLimp_Shutdown
===============
*/
void GLimp_Shutdown( void )
{
	ri.IN_Shutdown();

	SDL_QuitSubSystem( SDL_INIT_VIDEO );
}

/*
===============
GLimp_Minimize

Minimize the game so that user is back at the desktop
===============
*/
void GLimp_Minimize( void )
{
	SDL_MinimizeWindow( SDL_window );
}


/*
===============
GLimp_LogComment
===============
*/
void GLimp_LogComment( char *comment )
{
}

/*
===============
GLimp_CompareModes
===============
*/
static int GLimp_CompareModes( const void *a, const void *b )
{
	const float ASPECT_EPSILON = 0.001f;
	SDL_Rect *modeA = (SDL_Rect *)a;
	SDL_Rect *modeB = (SDL_Rect *)b;
	float aspectA = (float)modeA->w / (float)modeA->h;
	float aspectB = (float)modeB->w / (float)modeB->h;
	int areaA = modeA->w * modeA->h;
	int areaB = modeB->w * modeB->h;
	float aspectDiffA = fabs( aspectA - displayAspect );
	float aspectDiffB = fabs( aspectB - displayAspect );
	float aspectDiffsDiff = aspectDiffA - aspectDiffB;

	if( aspectDiffsDiff > ASPECT_EPSILON )
		return 1;
	else if( aspectDiffsDiff < -ASPECT_EPSILON )
		return -1;
	else
		return areaA - areaB;
}

/*
===============
GLimp_SetFullscreenMode
===============
*/
static qboolean GLimp_SetFullscreenMode( SDL_Window *window, SDL_DisplayID display, int width, int height )
{
	int i, numModes = 0;
	SDL_DisplayMode **modes;
	const SDL_DisplayMode *best = NULL;
	qboolean result = qfalse;

	modes = SDL_GetFullscreenDisplayModes( display, &numModes );
	if ( !modes || numModes <= 0 )
	{
		ri.Printf( PRINT_DEVELOPER,
			"SDL_GetFullscreenDisplayModes failed: %s\n", SDL_GetError() );

		SDL_free( modes );
		return qfalse;
	}

	for ( i = 0; i < numModes; i++ )
	{
		const SDL_DisplayMode *mode = modes[i];

		if ( !mode )
			continue;

		if ( mode->w == width && mode->h == height )
		{
			best = mode;
			break;
		}
	}

	if ( !best )
	{
		ri.Printf( PRINT_DEVELOPER,
			"No exclusive fullscreen mode found for %dx%d\n", width, height );

		SDL_free( modes );
		return qfalse;
	}

	if ( SDL_SetWindowFullscreenMode( window, best ) )
	{
		result = qtrue;
	}
	else
	{
		ri.Printf( PRINT_DEVELOPER,
			"SDL_SetWindowFullscreenMode %dx%d failed: %s\n",
			width, height, SDL_GetError() );
	}

	SDL_free( modes );
	return result;
}


/*
===============
GLimp_DetectAvailableModes
===============
*/
static void GLimp_DetectAvailableModes(void)
{
	int i, j;
	char buf[ MAX_STRING_CHARS ] = { 0 };
	int numSDLModes;
	SDL_DisplayMode **fsmodes = NULL;
	SDL_Rect *modes;
	int numModes = 0;

    const SDL_DisplayMode *pwindowMode = NULL;
	SDL_DisplayMode windowMode;
	int maxWidth = 0, maxHeight = 0;

	const SDL_DisplayID display = SDL_GetDisplayForWindow( SDL_window );
	if( display == 0 )
	{
		ri.Printf( PRINT_WARNING, "Couldn't get window display index, no resolutions detected: %s\n", SDL_GetError() );
		return;
	}
	
	fsmodes = SDL_GetFullscreenDisplayModes( display, &numSDLModes );
	pwindowMode = SDL_GetDesktopDisplayMode(display);

	if (numSDLModes <= 0)
	{
		ri.Printf(PRINT_WARNING, "No fullscreen display modes detected: %s\n", SDL_GetError());
		SDL_free(fsmodes);
		return;
	}

	if (pwindowMode)
	{
		SDL_copyp(&windowMode, pwindowMode);
	}
	else
	{
		SDL_zero(windowMode);
	}

	modes = SDL_calloc( (size_t)numSDLModes, sizeof( SDL_Rect ) );
	if ( !modes )
	{
		ri.Error( ERR_FATAL, "Out of memory" );
		SDL_free( fsmodes );
        return;
	}

	for( i = 0; i < numSDLModes; i++ )
	{
		SDL_DisplayMode mode;

		SDL_copyp(&mode, fsmodes[i]);

		if( !mode.w || !mode.h )
		{
			ri.Printf( PRINT_ALL, "Display supports any resolution\n" );
			SDL_free( modes );
			SDL_free( fsmodes );
			return;
		}

		if( windowMode.format && windowMode.format != mode.format )
			continue;

		// SDL can give the same resolution with different refresh rates.
		// Only list resolution once.
		for( j = 0; j < numModes; j++ )
		{
			if( mode.w == modes[ j ].w && mode.h == modes[ j ].h )
				break;
		}

		if( j != numModes )
			continue;

		if ( maxWidth < mode.w ) maxWidth = mode.w;
		if ( maxHeight < mode.h ) maxHeight = mode.h;

		modes[ numModes ].w = mode.w;
		modes[ numModes ].h = mode.h;
		numModes++;
	}

	if( numModes > 1 )
		qsort( modes, numModes, sizeof( SDL_Rect ), GLimp_CompareModes );

	for( i = 0; i < numModes; i++ )
	{
		const char *newModeString = va( "%ux%u ", modes[ i ].w, modes[ i ].h );

		if( strlen( newModeString ) < (int)sizeof( buf ) - strlen( buf ) )
			Q_strcat( buf, sizeof( buf ), newModeString );
		else
			ri.Printf( PRINT_WARNING, "Skipping mode %ux%u, buffer too small\n", modes[ i ].w, modes[ i ].h );
	}

	if( *buf )
	{
		buf[ strlen( buf ) - 1 ] = 0;
		ri.Printf( PRINT_ALL, "Available modes: '%s'\n", buf );
		ri.Cvar_Set( "r_availableModes", buf );
		ri.Cvar_SetValue( "r_maxResolutionWidth", (float)maxWidth );
		ri.Cvar_SetValue( "r_maxResolutionHeight", (float)maxHeight );
	}
	SDL_free( modes );
	SDL_free( fsmodes );
}

/* GLimp_GetProcAddresses, GLimp_ClearProcAddresses, GLES compat shims,
 * and GLimp_InitExtensions moved to code/renderer/r_glimp.c
 * (gamma' Task 2a). Renderer DLL's R_Init invokes GLimp_RendererInit()
 * after this engine-side GLimp_Init returns to perform GL probing. */


/*
===============
GLimp_SetMode
===============
*/
static int GLimp_SetMode(int mode, qboolean fullscreen, qboolean noborder, qboolean fixedFunction, qboolean vulkan)
{
	int perChannelColorBits;
	int colorBits, depthBits, stencilBits;
	int samples;
	int i = 0;
	SDL_Surface *icon = NULL;
    Uint32 flags = vulkan ? SDL_WINDOW_VULKAN : SDL_WINDOW_OPENGL;
    const SDL_DisplayMode *pdesktopMode = NULL;
	SDL_DisplayMode desktopMode;
	SDL_DisplayID display = 0;
	int x = SDL_WINDOWPOS_UNDEFINED, y = SDL_WINDOWPOS_UNDEFINED;

	ri.Printf( PRINT_ALL, "Initializing OpenGL display\n");

	if ( r_allowResize->integer )
		flags |= SDL_WINDOW_RESIZABLE;

#ifdef USE_ICON
	icon = SDL_CreateSurfaceFrom(
				     CLIENT_WINDOW_ICON.width,
				     CLIENT_WINDOW_ICON.height,
                     SDL_PIXELFORMAT_RGBA32,
                     (void *)CLIENT_WINDOW_ICON.pixel_data,
				     CLIENT_WINDOW_ICON.bytes_per_pixel * CLIENT_WINDOW_ICON.width);
#endif

	// If a window exists, use its display. Otherwise use primary display.
	if (SDL_window != NULL)
	{
		display = SDL_GetDisplayForWindow(SDL_window);
		if (display == 0)
		{
			ri.Printf(PRINT_DEVELOPER, "SDL_GetDisplayForWindow() failed: %s\n", SDL_GetError());
		}
	}

	if (display == 0)
	{
		display = SDL_GetPrimaryDisplay();
		if (display == 0)
		{
			ri.Printf(PRINT_DEVELOPER, "SDL_GetPrimaryDisplay() failed: %s\n", SDL_GetError());
		}
	}

	pdesktopMode = SDL_GetDesktopDisplayMode( display );
	 if ( pdesktopMode )
	{
		SDL_copyp(&desktopMode, pdesktopMode);
		displayAspect = (float)desktopMode.w / (float)desktopMode.h;

		ri.Printf( PRINT_ALL, "Display aspect: %.3f\n", displayAspect );
	}
	else
	{
		Com_Memset( &desktopMode, 0, sizeof( SDL_DisplayMode ) );

		ri.Printf( PRINT_ALL,
				"Cannot determine display aspect, assuming 1.333\n" );
	}

	ri.Printf (PRINT_ALL, "...setting mode %d:", mode );

	if (mode == -2)
	{
		// use desktop video resolution
		if( desktopMode.h > 0 )
		{
			glConfig.vidWidth = desktopMode.w;
			glConfig.vidHeight = desktopMode.h;
		}
		else
		{
			glConfig.vidWidth = 640;
			glConfig.vidHeight = 480;
			ri.Printf( PRINT_ALL,
					"Cannot determine display resolution, assuming 640x480\n" );
		}

		glConfig.windowAspect = (float)glConfig.vidWidth / (float)glConfig.vidHeight;
	}
	else if ( !R_GetModeInfo( &glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect, mode ) )
	{
		ri.Printf( PRINT_ALL, " invalid mode\n" );
		return RSERR_INVALID_MODE;
	}
	ri.Printf( PRINT_ALL, " %d %d\n", glConfig.vidWidth, glConfig.vidHeight);

	// Center window
	if( r_centerWindow->integer && !fullscreen )
	{
		x = ( desktopMode.w / 2 ) - ( glConfig.vidWidth / 2 );
		y = ( desktopMode.h / 2 ) - ( glConfig.vidHeight / 2 );
	}

	// Destroy existing state if it exists
	if( SDL_glContext != NULL )
	{
		/* qgl* clearing now happens renderer-side via GLimp_RendererShutdown */
		SDL_GL_DestroyContext( SDL_glContext );
		SDL_glContext = NULL;
	}

	if( SDL_window != NULL )
	{
		SDL_GetWindowPosition( SDL_window, &x, &y );
		ri.Printf( PRINT_DEVELOPER, "Existing window at %dx%d before being destroyed\n", x, y );
		SDL_DestroyWindow( SDL_window );
		SDL_window = NULL;
	}

	if (fullscreen)
	{
		glConfig.isFullscreen = qtrue;
	}
	else
	{
		if( noborder )
			flags |= SDL_WINDOW_BORDERLESS;

		glConfig.isFullscreen = qfalse;
	}

	colorBits = r_colorbits->value;
	if ((!colorBits) || (colorBits >= 32))
		colorBits = 24;

	if (!r_depthbits->value)
		depthBits = 24;
	else
		depthBits = r_depthbits->value;

	stencilBits = r_stencilbits->value;
	samples = r_ext_multisample->value;

	for (i = 0; i < 16; i++)
	{
		int testColorBits, testDepthBits, testStencilBits;
		int realColorBits[3];

		// 0 - default
		// 1 - minus colorBits
		// 2 - minus depthBits
		// 3 - minus stencil
		if ((i % 4) == 0 && i)
		{
			// one pass, reduce
			switch (i / 4)
			{
				case 2 :
					if (colorBits == 24)
						colorBits = 16;
					break;
				case 1 :
					if (depthBits == 24)
						depthBits = 16;
					else if (depthBits == 16)
						depthBits = 8;
				case 3 :
					if (stencilBits == 24)
						stencilBits = 16;
					else if (stencilBits == 16)
						stencilBits = 8;
			}
		}

		testColorBits = colorBits;
		testDepthBits = depthBits;
		testStencilBits = stencilBits;

		if ((i % 4) == 3)
		{ // reduce colorBits
			if (testColorBits == 24)
				testColorBits = 16;
		}

		if ((i % 4) == 2)
		{ // reduce depthBits
			if (testDepthBits == 24)
				testDepthBits = 16;
			else if (testDepthBits == 16)
				testDepthBits = 8;
		}

		if ((i % 4) == 1)
		{ // reduce stencilBits
			if (testStencilBits == 24)
				testStencilBits = 16;
			else if (testStencilBits == 16)
				testStencilBits = 8;
			else
				testStencilBits = 0;
		}

		if (testColorBits == 24)
			perChannelColorBits = 8;
		else
			perChannelColorBits = 4;

#ifdef __sgi /* Fix for SGIs grabbing too many bits of color */
		if (perChannelColorBits == 4)
			perChannelColorBits = 0; /* Use minimum size for 16-bit color */

		/* Need alpha or else SGIs choose 36+ bit RGB mode */
		SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 1);
#endif

		if ( !vulkan )
		{
#ifdef USE_OPENGLES
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 1 );
#endif

			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, perChannelColorBits );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, perChannelColorBits );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, perChannelColorBits );
			SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, testDepthBits );
			SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, testStencilBits );

			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, samples ? 1 : 0 );
			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, samples );

			if(r_stereoEnabled->integer)
			{
				glConfig.stereoEnabled = qtrue;
				SDL_GL_SetAttribute(SDL_GL_STEREO, 1);
			}
			else
			{
				glConfig.stereoEnabled = qfalse;
				SDL_GL_SetAttribute(SDL_GL_STEREO, 0);
			}

			SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
		}

#if 0 // if multisampling is enabled on X11, this causes create window to fail.
		// If not allowing software GL, demand accelerated
		if( !r_allowSoftwareGL->integer )
			SDL_GL_SetAttribute( SDL_GL_ACCELERATED_VISUAL, 1 );
#endif

		if( ( SDL_window = SDL_CreateWindow( CLIENT_WINDOW_TITLE,
				glConfig.vidWidth, glConfig.vidHeight, flags ) ) == NULL )
		{
			ri.Printf( PRINT_DEVELOPER, "SDL_CreateWindow failed: %s\n", SDL_GetError( ) );
			continue;
		}

		 SDL_SetWindowPosition( SDL_window, x, y );

		SDL_SetWindowIcon( SDL_window, icon );

		if ( !vulkan )
		{
#ifdef USE_OPENGLES
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif

			if (!fixedFunction)
			{
				int profileMask, majorVersion, minorVersion;
				SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &profileMask);
				SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &majorVersion);
				SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minorVersion);

				ri.Printf(PRINT_ALL, "Trying to get an OpenGL 3.2 core context\n");
				SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
				SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
				SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
				if ((SDL_glContext = SDL_GL_CreateContext(SDL_window)) == NULL)
				{
					ri.Printf(PRINT_ALL, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
					ri.Printf(PRINT_ALL, "Reverting to default context\n");

					SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profileMask);
					SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, majorVersion);
					SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minorVersion);
				}
				else
				{
					ri.Printf(PRINT_ALL, "SDL_GL_CreateContext succeeded.\n");
					/* gamma' Task 2a: qgl* probe + software-rasterizer
					 * rejection moved to renderer-side GLimp_RendererInit
					 * (code/renderer/r_glimp.c). Engine-side keeps the
					 * 3.2-core context once SDL_GL_CreateContext succeeds. */
				}
			}
			else
			{
				SDL_glContext = NULL;
			}

			if ( !SDL_glContext )
			{
				if( ( SDL_glContext = SDL_GL_CreateContext( SDL_window ) ) == NULL )
				{
					ri.Printf( PRINT_DEVELOPER, "SDL_GL_CreateContext failed: %s\n", SDL_GetError( ) );
					SDL_DestroyWindow( SDL_window );
					SDL_window = NULL;
					continue;
				}
			}

			SDL_GL_SwapWindow( SDL_window );

			if( !SDL_GL_SetSwapInterval( r_swapInterval->integer ) )
			{
				ri.Printf( PRINT_DEVELOPER, "SDL_GL_SetSwapInterval failed: %s\n", SDL_GetError( ) );
			}

			SDL_GL_GetAttribute( SDL_GL_RED_SIZE, &realColorBits[0] );
			SDL_GL_GetAttribute( SDL_GL_GREEN_SIZE, &realColorBits[1] );
			SDL_GL_GetAttribute( SDL_GL_BLUE_SIZE, &realColorBits[2] );
			SDL_GL_GetAttribute( SDL_GL_DEPTH_SIZE, &glConfig.depthBits );
			SDL_GL_GetAttribute( SDL_GL_STENCIL_SIZE, &glConfig.stencilBits );

			glConfig.colorBits = realColorBits[0] + realColorBits[1] + realColorBits[2];

			ri.Printf( PRINT_ALL, "Using %d color bits, %d depth, %d stencil display.\n",
					glConfig.colorBits, glConfig.depthBits, glConfig.stencilBits );
		}

		if (fullscreen)
		{
			if (!GLimp_SetFullscreenMode(SDL_window, display,
										 glConfig.vidWidth, glConfig.vidHeight))
			{
				ri.Printf(PRINT_DEVELOPER,
						  "Falling back to borderless fullscreen desktop mode\n");

				SDL_SetWindowFullscreenMode(SDL_window, NULL);
			}

			if (!SDL_SetWindowFullscreen(SDL_window, true))
			{
				ri.Printf(PRINT_DEVELOPER,
						  "SDL_SetWindowFullscreen failed: %s\n", SDL_GetError());

				if ( !vulkan )
				{
					/* qgl* clearing now renderer-side (gamma' Task 2a) */
					SDL_GL_DestroyContext(SDL_glContext);
					SDL_glContext = NULL;
				}

				SDL_DestroyWindow(SDL_window);
				SDL_window = NULL;

				return RSERR_INVALID_FULLSCREEN;
			}

			SDL_SyncWindow(SDL_window);
		}

		break;
	}

	SDL_DestroySurface( icon );

	if( !SDL_window )
	{
		ri.Printf( PRINT_ALL, "Couldn't get a visual\n" );
		return RSERR_INVALID_MODE;
	}

	GLimp_DetectAvailableModes();

	/* GL_RENDERER print now emitted from renderer-side GLimp_RendererInit
	 * (gamma' Task 2a). */

	return RSERR_OK;
}

/*
===============
GLimp_StartDriverAndSetMode
===============
*/
static qboolean GLimp_StartDriverAndSetMode(int mode, qboolean fullscreen, qboolean noborder, qboolean gl3Core, qboolean vulkan)
{
	rserr_t err;

	if (!SDL_WasInit(SDL_INIT_VIDEO))
	{
		const char *driverName;

		if (!SDL_Init(SDL_INIT_VIDEO))
		{
			ri.Printf( PRINT_ALL, "SDL_Init( SDL_INIT_VIDEO ) FAILED (%s)\n", SDL_GetError());
			return qfalse;
		}

		driverName = SDL_GetCurrentVideoDriver( );
		ri.Printf( PRINT_ALL, "SDL using driver \"%s\"\n", driverName );
		ri.Cvar_Set( "r_sdlDriver", driverName );
	}

	if (fullscreen && ri.Cvar_VariableIntegerValue( "in_nograb" ) )
	{
		ri.Printf( PRINT_ALL, "Fullscreen not allowed with in_nograb 1\n");
		ri.Cvar_Set( "r_fullscreen", "0" );
		r_fullscreen->modified = qfalse;
		fullscreen = qfalse;
	}
	
	err = GLimp_SetMode(mode, fullscreen, noborder, gl3Core, vulkan);

	switch ( err )
	{
		case RSERR_INVALID_FULLSCREEN:
			ri.Printf( PRINT_ALL, "...WARNING: fullscreen unavailable in this mode\n" );
			return qfalse;
		case RSERR_INVALID_MODE:
			ri.Printf( PRINT_ALL, "...WARNING: could not set the given mode (%d)\n", mode );
			return qfalse;
		default:
			break;
	}

	return qtrue;
}


#define R_MODE_FALLBACK 3 // 640 * 480

/*
===============
GLimp_Init

This routine is responsible for initializing the OS specific portions
of OpenGL
===============
*/
void GLimp_Init( qboolean fixedFunction )
{
#ifdef BUILD_RENDERER_VULKAN
	RealRTCW_VkBridgeInit();
#endif

	ri.Printf( PRINT_DEVELOPER, "Glimp_Init( )\n" );

	r_allowSoftwareGL = ri.Cvar_Get( "r_allowSoftwareGL", "0", CVAR_LATCH );
	r_sdlDriver = ri.Cvar_Get( "r_sdlDriver", "", CVAR_ROM );
	r_allowResize = ri.Cvar_Get( "r_allowResize", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_centerWindow = ri.Cvar_Get( "r_centerWindow", "0", CVAR_ARCHIVE | CVAR_LATCH );

	if( ri.Cvar_VariableIntegerValue( "com_abnormalExit" ) )
	{
		ri.Cvar_Set( "r_mode", va( "%d", R_MODE_FALLBACK ) );
		ri.Cvar_Set( "r_fullscreen", "0" );
		ri.Cvar_Set( "r_centerWindow", "0" );
		ri.Cvar_Set( "com_abnormalExit", "0" );
	}

	ri.Sys_GLimpInit( );

	ri.Cvar_Get("r_availableModes", "", CVAR_ROM);
	ri.Cvar_Get("r_maxResolutionWidth", "0", 0);
	ri.Cvar_Get("r_maxResolutionHeight", "0", 0);

	// Create the window and set up the context
	if(GLimp_StartDriverAndSetMode(r_mode->integer, r_fullscreen->integer, r_noborder->integer, fixedFunction, qfalse))
		goto success;

	// Try again, this time in a platform specific "safe mode"
	ri.Sys_GLimpSafeInit( );

	if(GLimp_StartDriverAndSetMode(r_mode->integer, r_fullscreen->integer, qfalse, fixedFunction, qfalse))
		goto success;

	// Finally, try the default screen resolution
	if( r_mode->integer != R_MODE_FALLBACK )
	{
		ri.Printf( PRINT_ALL, "Setting r_mode %d failed, falling back on r_mode %d\n",
				r_mode->integer, R_MODE_FALLBACK );

		if(GLimp_StartDriverAndSetMode(R_MODE_FALLBACK, qfalse, qfalse, fixedFunction, qfalse))
			goto success;
	}

	// Nothing worked, give up
	ri.Error( ERR_FATAL, "GLimp_Init() - could not load OpenGL subsystem" );

success:
	// These values force the UI to disable driver selection
	glConfig.driverType = GLDRV_ICD;
	glConfig.hardwareType = GLHW_GENERIC;

	// Only using SDL_SetWindowBrightness to determine if hardware gamma is supported
#if 0  // !!! FIXME: use a shader, sorry.
	glConfig.deviceSupportsGamma = !r_ignorehwgamma->integer &&
		SDL_SetWindowBrightness( SDL_window, 1.0f ) >= 0;
		#else
	glConfig.deviceSupportsGamma = qfalse;
#endif

	/* GL state probing (vendor/renderer/version/extensions strings +
	 * GLimp_InitExtensions) moved to code/renderer/r_glimp.c (gamma'
	 * Task 2a). Renderer's R_Init calls GLimp_RendererInit() after this
	 * function returns. */

	// This depends on SDL_INIT_VIDEO, hence having it here
	ri.IN_Init( SDL_window );
}

#ifdef BUILD_RENDERER_VULKAN
/*
===============
VKimp_Init

θ' window-init path. Owns SDL window creation for the Vulkan renderer
DLL. Mirrors GLimp_Init but takes the Vulkan branch through
GLimp_StartDriverAndSetMode (no GL context creation).

The caller is code/renderervk/tr_init.c (vendored Q3e), which now
invokes this function directly instead of going through ri.VKimp_Init.
See notes/decisions/2026-06-10-m4-window-ownership-model.md §3.
===============
*/
void VKimp_Init( glconfig_t *config )
{
	RealRTCW_VkBridgeInit();

	ri.Printf( PRINT_DEVELOPER, "VKimp_Init( )\n" );

	r_allowSoftwareGL = ri.Cvar_Get( "r_allowSoftwareGL", "0", CVAR_LATCH );
	r_sdlDriver = ri.Cvar_Get( "r_sdlDriver", "", CVAR_ROM );
	r_allowResize = ri.Cvar_Get( "r_allowResize", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_centerWindow = ri.Cvar_Get( "r_centerWindow", "0", CVAR_ARCHIVE | CVAR_LATCH );

	if( ri.Cvar_VariableIntegerValue( "com_abnormalExit" ) )
	{
		ri.Cvar_Set( "r_mode", va( "%d", R_MODE_FALLBACK ) );
		ri.Cvar_Set( "r_fullscreen", "0" );
		ri.Cvar_Set( "r_centerWindow", "0" );
		ri.Cvar_Set( "com_abnormalExit", "0" );
	}

	ri.Sys_GLimpInit( );

	ri.Cvar_Get("r_availableModes", "", CVAR_ROM);
	ri.Cvar_Get("r_maxResolutionWidth", "0", 0);
	ri.Cvar_Get("r_maxResolutionHeight", "0", 0);

	/* Create the window with SDL_WINDOW_VULKAN; no GL context. */
	if(GLimp_StartDriverAndSetMode(r_mode->integer, r_fullscreen->integer, r_noborder->integer, qfalse, qtrue))
		goto success;

	ri.Sys_GLimpSafeInit( );

	if(GLimp_StartDriverAndSetMode(r_mode->integer, r_fullscreen->integer, qfalse, qfalse, qtrue))
		goto success;

	if( r_mode->integer != R_MODE_FALLBACK )
	{
		ri.Printf( PRINT_ALL, "Setting r_mode %d failed, falling back on r_mode %d\n",
				r_mode->integer, R_MODE_FALLBACK );

		if(GLimp_StartDriverAndSetMode(R_MODE_FALLBACK, qfalse, qfalse, qfalse, qtrue))
			goto success;
	}

	ri.Error( ERR_FATAL, "VKimp_Init() - could not create SDL Vulkan window" );

success:
	/* Populate glconfig fields that downstream Q3e Vulkan code reads.
	 * vidWidth / vidHeight are already set by GLimp_SetMode. */
	config->vidWidth        = glConfig.vidWidth;
	config->vidHeight       = glConfig.vidHeight;
	config->windowAspect    = glConfig.windowAspect;
	config->isFullscreen    = glConfig.isFullscreen;
	config->displayFrequency = 60;        /* refined later when swapchain is built */
	config->deviceSupportsGamma = qfalse; /* MoltenVK path -- shader gamma only */

	/* Hand the window pointer to the engine input subsystem via the
	 * existing ri.IN_Init handoff (same channel GLimp_Init uses at the
	 * end of its body). */
	ri.IN_Init( SDL_window );
}

/*
===============
VKimp_Shutdown
===============
*/
void VKimp_Shutdown( qboolean unloadDLL )
{
	if( SDL_window )
	{
		SDL_DestroyWindow( SDL_window );
		SDL_window = NULL;
	}

	if( unloadDLL )
	{
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
	}
}
#endif /* BUILD_RENDERER_VULKAN */


/*
===============
GLimp_EndFrame

Responsible for doing a swapbuffers
===============
*/
void GLimp_EndFrame( void )
{
	// don't flip if drawing to front buffer
	if ( Q_stricmp( r_drawBuffer->string, "GL_FRONT" ) != 0 )
	{
		SDL_GL_SwapWindow( SDL_window );
	}

	if( r_fullscreen->modified )
	{
		int         fullscreen;
		qboolean    needToToggle;

		// Find out the current state
		fullscreen = !!( SDL_GetWindowFlags( SDL_window ) & SDL_WINDOW_FULLSCREEN );

		if( r_fullscreen->integer && ri.Cvar_VariableIntegerValue( "in_nograb" ) )
		{
			ri.Printf( PRINT_ALL, "Fullscreen not allowed with in_nograb 1\n");
			ri.Cvar_Set( "r_fullscreen", "0" );
			r_fullscreen->modified = qfalse;
		}

		// Is the state we want different from the current state?
		needToToggle = !!r_fullscreen->integer != fullscreen;

		if( needToToggle )
		{
			// Need the vid_restart here since r_fullscreen is only latched
			if( fullscreen )
			{
				Com_Printf( "Switching to windowed rendering\n" );
				ri.Cmd_ExecuteText(EXEC_APPEND, "vid_restart\n");
			}
			else
			{
				Com_Printf( "Switching to fullscreen rendering\n" );
				ri.Cmd_ExecuteText(EXEC_APPEND, "vid_restart\n");
			}

			ri.IN_Restart( );
		}

		r_fullscreen->modified = qfalse;
	}
}
