/*
===========================================================================

Return to Castle Wolfenstein single player GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.

This file is part of the Return to Castle Wolfenstein single player GPL Source Code (RTCW SP Source Code).

RTCW SP Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTCW SP Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTCW SP Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the RTCW SP Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the RTCW SP Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

/*
 * r_glimp.c -- renderer-internal OpenGL probing.
 *
 * gamma' Task 2a (notes/decisions/2026-06-10-m4-window-ownership-model.md):
 * Code moved out of code/sdl/sdl_glimp.c so sdl_glimp.c can become a pure
 * platform-glue TU compiled engine-side. This file holds qgl* function
 * pointer DEFINITIONS, GLimp_GetProcAddresses/ClearProcAddresses, and the
 * extension detection (GLimp_InitExtensions) — all renderer-internal state
 * that has no business living in the engine binary.
 *
 * Entry points GLimp_RendererInit / GLimp_RendererShutdown are invoked
 * from R_Init / R_Shutdown after engine-side GLimp_Init creates the SDL
 * window + GL context.
 *
 * NOTE on software-rasterizer fallback: pre-gamma' sdl_glimp.c detected
 * software rasterizer inside the SDL_GL_CONTEXT_PROFILE_CORE attempt
 * loop and retried with a non-core context before giving up. Post-gamma'
 * Task 2a, by the time GLimp_RendererInit runs the context is already
 * committed and the only response to software is ERR_FATAL. This is
 * intentional for the target platform (Apple Silicon Metal-GL is
 * hardware-only) and the entanglement-removal it enables is worth it.
 * On Intel macOS with broken drivers or Linux with
 * LIBGL_ALWAYS_SOFTWARE=1, behavior degrades from graceful fallback to
 * a hard fatal. Acceptable for our ship target.
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

#include "tr_local.h"
#include "r_glimp.h"

#ifdef USE_OPENGLES
#ifdef USE_LOCAL_HEADERS
#	include "EGL/egl.h"
#else
#	include <EGL/egl.h>
#endif
void myglMultiTexCoord2f( GLenum texture, GLfloat s, GLfloat t )
{
	qglMultiTexCoord4f(texture, s, t, 0, 1);
}
#endif

int qglMajorVersion, qglMinorVersion;
int qglesMajorVersion, qglesMinorVersion;

typedef void (APIENTRYP qglActiveTextureARB_t) (GLenum texture);
typedef void (APIENTRYP qglClientActiveTextureARB_t) (GLenum texture);
typedef void (APIENTRYP qglMultiTexCoord2fARB_t) (GLenum target, GLfloat s, GLfloat t);

typedef void (APIENTRYP qglLockArraysEXT_t) (GLint first, GLsizei count);
typedef void (APIENTRYP qglUnlockArraysEXT_t) (void);

qglActiveTextureARB_t qglActiveTextureARB;
qglClientActiveTextureARB_t qglClientActiveTextureARB;
qglMultiTexCoord2fARB_t qglMultiTexCoord2fARB;

qglLockArraysEXT_t qglLockArraysEXT;
qglUnlockArraysEXT_t qglUnlockArraysEXT;

#define GLE(ret, name, ...) name##proc * qgl##name = NULL;
QGL_1_1_PROCS;
QGL_1_1_FIXED_FUNCTION_PROCS;
QGL_DESKTOP_1_1_PROCS;
QGL_DESKTOP_1_1_FIXED_FUNCTION_PROCS;
QGL_ES_1_1_PROCS;
QGL_ES_1_1_FIXED_FUNCTION_PROCS;
QGL_1_3_PROCS;
QGL_1_5_PROCS;
QGL_2_0_PROCS;
QGL_3_0_PROCS;
QGL_ARB_occlusion_query_PROCS;
QGL_ARB_framebuffer_object_PROCS;
QGL_ARB_vertex_array_object_PROCS;
QGL_EXT_direct_state_access_PROCS;
#undef GLE

#ifdef USE_OPENGLES
/*
===============
OpenGL ES compatibility
===============
*/
static void APIENTRY GLimp_GLES_ClearDepth( GLclampd depth ) {
	qglClearDepthf( depth );
}

static void APIENTRY GLimp_GLES_ClipPlane( GLenum plane, const GLdouble *equation ) {
	GLfloat values[4];
	values[0] = equation[0];
	values[1] = equation[1];
	values[2] = equation[2];
	values[3] = equation[3];
	qglClipPlanef( plane, values );
}

static void APIENTRY GLimp_GLES_Color3f( GLfloat red, GLfloat green, GLfloat blue ) {
	qglColor4f( red, green, blue, 1.0f );
}

static void APIENTRY GLimp_GLES_Color4ubv( const GLubyte *v ) {
	qglColor4ub( v[0], v[1], v[2], v[3] );
}

static void APIENTRY GLimp_GLES_DepthRange( GLclampd near_val, GLclampd far_val ) {
	qglDepthRangef( near_val, far_val );
}

static void APIENTRY GLimp_GLES_DrawBuffer( GLenum mode ) {
	// unsupported
}

static void APIENTRY GLimp_GLES_Frustum( GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val ) {
	qglFrustumf( left, right, bottom, top, near_val, far_val );
}

static void APIENTRY GLimp_GLES_Ortho( GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val ) {
	qglOrthof( left, right, bottom, top, near_val, far_val );
}

static void APIENTRY GLimp_GLES_PolygonMode( GLenum face, GLenum mode ) {
	// unsupported
}

/*Added*/
static void APIENTRY GLimp_GLES_Fogi( GLenum pname, GLint param ) {
	qglFogf( pname, param );
}
#endif

/*
===============
GLimp_GetProcAddresses

Get addresses for OpenGL functions.
===============
*/
static qboolean GLimp_GetProcAddresses( qboolean fixedFunction ) {
	qboolean success = qtrue;
	const char *version;

#ifdef __SDL_NOGETPROCADDR__
#define GLE( ret, name, ... ) qgl##name = gl#name;
#else
#define GLE( ret, name, ... ) qgl##name = (name##proc *) SDL_GL_GetProcAddress("gl" #name); \
	if ( qgl##name == NULL ) { \
		ri.Printf( PRINT_ALL, "ERROR: Missing OpenGL function %s\n", "gl" #name ); \
		success = qfalse; \
	}
#endif

	// OpenGL 1.0 and OpenGL ES 1.0
	GLE(const GLubyte *, GetString, GLenum name)

	if ( !qglGetString ) {
		Com_Error( ERR_FATAL, "glGetString is NULL" );
	}

	version = (const char *)qglGetString( GL_VERSION );

	if ( !version ) {
		Com_Error( ERR_FATAL, "GL_VERSION is NULL" );
	}

	if ( Q_stricmpn( "OpenGL ES", version, 9 ) == 0 ) {
		char profile[6]; // ES, ES-CM, or ES-CL
		sscanf( version, "OpenGL %5s %d.%d", profile, &qglesMajorVersion, &qglesMinorVersion );
		// common lite profile (no floating point) is not supported
		if ( Q_stricmp( profile, "ES-CL" ) == 0 ) {
			qglesMajorVersion = 0;
			qglesMinorVersion = 0;
		}
	} else {
		sscanf( version, "%d.%d", &qglMajorVersion, &qglMinorVersion );
	}

	if ( fixedFunction ) {
		if ( QGL_VERSION_ATLEAST( 1, 1 ) ) {
			QGL_1_1_PROCS;
			QGL_1_1_FIXED_FUNCTION_PROCS;
			QGL_DESKTOP_1_1_PROCS;
			QGL_DESKTOP_1_1_FIXED_FUNCTION_PROCS;
		} else if ( qglesMajorVersion == 1 && qglesMinorVersion >= 1 ) {
			// OpenGL ES 1.1 (2.0 is not backward compatible)
			QGL_1_1_PROCS;
			QGL_1_1_FIXED_FUNCTION_PROCS;
			QGL_ES_1_1_PROCS;
			QGL_ES_1_1_FIXED_FUNCTION_PROCS;

#ifdef USE_OPENGLES
			qglClearDepth = GLimp_GLES_ClearDepth;
			qglClipPlane = GLimp_GLES_ClipPlane;
			qglColor3f = GLimp_GLES_Color3f;
			qglColor4ubv = GLimp_GLES_Color4ubv;
			qglDepthRange = GLimp_GLES_DepthRange;
			qglDrawBuffer = GLimp_GLES_DrawBuffer;
			qglFrustum = GLimp_GLES_Frustum;
			qglOrtho = GLimp_GLES_Ortho;
			qglPolygonMode = GLimp_GLES_PolygonMode;
			qglFogi = GLimp_GLES_Fogi; /*Added*/
#else
			// error so this doesn't segfault due to NULL desktop GL functions being used
			Com_Error( ERR_FATAL, "Unsupported OpenGL Version: %s", version );
#endif
		} else {
			Com_Error( ERR_FATAL, "Unsupported OpenGL Version (%s), OpenGL 1.1 is required", version );
		}
	} else {
		if ( QGL_VERSION_ATLEAST( 2, 0 ) ) {
			QGL_1_1_PROCS;
			QGL_DESKTOP_1_1_PROCS;
			QGL_1_3_PROCS;
			QGL_1_5_PROCS;
			QGL_2_0_PROCS;
		} else if ( QGLES_VERSION_ATLEAST( 2, 0 ) ) {
			QGL_1_1_PROCS;
			QGL_ES_1_1_PROCS;
			QGL_1_3_PROCS;
			QGL_1_5_PROCS;
			QGL_2_0_PROCS;
			// error so this doesn't segfault due to NULL desktop GL functions being used
			Com_Error( ERR_FATAL, "Unsupported OpenGL Version: %s", version );
		} else {
			Com_Error( ERR_FATAL, "Unsupported OpenGL Version (%s), OpenGL 2.0 is required", version );
		}
	}

	if ( QGL_VERSION_ATLEAST( 3, 0 ) || QGLES_VERSION_ATLEAST( 3, 0 ) ) {
		QGL_3_0_PROCS;
	}

#undef GLE

	return success;
}

/*
===============
GLimp_ClearProcAddresses

Clear addresses for OpenGL functions.
===============
*/
static void GLimp_ClearProcAddresses( void ) {
#define GLE( ret, name, ... ) qgl##name = NULL;

	qglMajorVersion = 0;
	qglMinorVersion = 0;
	qglesMajorVersion = 0;
	qglesMinorVersion = 0;

	QGL_1_1_PROCS;
	QGL_1_1_FIXED_FUNCTION_PROCS;
	QGL_DESKTOP_1_1_PROCS;
	QGL_DESKTOP_1_1_FIXED_FUNCTION_PROCS;
	QGL_ES_1_1_PROCS;
	QGL_ES_1_1_FIXED_FUNCTION_PROCS;
	QGL_1_3_PROCS;
	QGL_1_5_PROCS;
	QGL_2_0_PROCS;
	QGL_3_0_PROCS;
	QGL_ARB_occlusion_query_PROCS;
	QGL_ARB_framebuffer_object_PROCS;
	QGL_ARB_vertex_array_object_PROCS;
	QGL_EXT_direct_state_access_PROCS;

	qglActiveTextureARB = NULL;
	qglClientActiveTextureARB = NULL;
	qglMultiTexCoord2fARB = NULL;

	qglLockArraysEXT = NULL;
	qglUnlockArraysEXT = NULL;

#undef GLE
}

/*
===============
GLimp_InitExtensions
===============
*/
static void GLimp_InitExtensions( qboolean fixedFunction )
{
	if ( !r_allowExtensions->integer )
	{
		ri.Printf( PRINT_ALL, "* IGNORING OPENGL EXTENSIONS *\n" );
		return;
	}

	ri.Printf( PRINT_ALL, "Initializing OpenGL extensions\n" );

	glConfig.textureCompression = TC_NONE;

	// GL_EXT_texture_compression_s3tc
	if ( SDL_GL_ExtensionSupported( "GL_ARB_texture_compression" ) &&
	     SDL_GL_ExtensionSupported( "GL_EXT_texture_compression_s3tc" ) )
	{
		if ( r_ext_compressed_textures->value )
		{
			glConfig.textureCompression = TC_S3TC_ARB;
			ri.Printf( PRINT_ALL, "...using GL_EXT_texture_compression_s3tc\n" );
		}
		else
		{
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_compression_s3tc\n" );
		}
	}
	else
	{
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_compression_s3tc not found\n" );
	}

	// GL_S3_s3tc ... legacy extension before GL_EXT_texture_compression_s3tc.
	if (glConfig.textureCompression == TC_NONE)
	{
		if ( SDL_GL_ExtensionSupported( "GL_S3_s3tc" ) )
		{
			if ( r_ext_compressed_textures->value )
			{
				glConfig.textureCompression = TC_S3TC;
				ri.Printf( PRINT_ALL, "...using GL_S3_s3tc\n" );
			}
			else
			{
				ri.Printf( PRINT_ALL, "...ignoring GL_S3_s3tc\n" );
			}
		}
		else
		{
			ri.Printf( PRINT_ALL, "...GL_S3_s3tc not found\n" );
		}
	}

	// OpenGL 1 fixed function pipeline
	if ( fixedFunction )
	{
		// GL_EXT_texture_env_add
#ifdef USE_OPENGLES
		glConfig.textureEnvAddAvailable = qtrue;
		ri.Printf( PRINT_ALL, "...using GL_EXT_texture_env_add\n" );
#else
		glConfig.textureEnvAddAvailable = qfalse;
		if ( SDL_GL_ExtensionSupported( "GL_EXT_texture_env_add" ) )
		{
			if ( r_ext_texture_env_add->integer )
			{
				glConfig.textureEnvAddAvailable = qtrue;
				ri.Printf( PRINT_ALL, "...using GL_EXT_texture_env_add\n" );
			}
			else
			{
				glConfig.textureEnvAddAvailable = qfalse;
				ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_env_add\n" );
			}
		}
		else
		{
			ri.Printf( PRINT_ALL, "...GL_EXT_texture_env_add not found\n" );
		}
#endif

		// GL_ARB_multitexture
		qglMultiTexCoord2fARB = NULL;
		qglActiveTextureARB = NULL;
		qglClientActiveTextureARB = NULL;
#ifdef USE_OPENGLES
		qglGetIntegerv( GL_MAX_TEXTURE_UNITS, &glConfig.numTextureUnits );
		//ri.Printf( PRINT_ALL, "...not using GL_ARB_multitexture, %i texture units\n", glConfig.maxActiveTextures );
		//glConfig.maxActiveTextures=4;
		qglMultiTexCoord2fARB = myglMultiTexCoord2f;
		qglActiveTextureARB = SDL_GL_GetProcAddress( "glActiveTexture" );
		qglClientActiveTextureARB = SDL_GL_GetProcAddress( "glClientActiveTexture" );
		if ( glConfig.numTextureUnits > 1 )
		{
			ri.Printf( PRINT_ALL, "...using GL_ARB_multitexture (%i texture units)\n", glConfig.numTextureUnits );
		}
		else
		{
			qglMultiTexCoord2fARB = NULL;
			qglActiveTextureARB = NULL;
			qglClientActiveTextureARB = NULL;
			ri.Printf( PRINT_ALL, "...not using GL_ARB_multitexture, < 2 texture units\n" );
		}
#else
		if ( SDL_GL_ExtensionSupported( "GL_ARB_multitexture" ) )
		{
			if ( r_ext_multitexture->value )
			{
				qglMultiTexCoord2fARB = (qglMultiTexCoord2fARB_t) SDL_GL_GetProcAddress( "glMultiTexCoord2fARB" );
				qglActiveTextureARB = (qglActiveTextureARB_t) SDL_GL_GetProcAddress( "glActiveTextureARB" );
				qglClientActiveTextureARB = (qglClientActiveTextureARB_t) SDL_GL_GetProcAddress( "glClientActiveTextureARB" );

				if ( qglActiveTextureARB )
				{
					GLint glint = 0;
					qglGetIntegerv( GL_MAX_TEXTURE_UNITS_ARB, &glint );
					glConfig.numTextureUnits = (int) glint;
					if ( glConfig.numTextureUnits > 1 )
					{
						ri.Printf( PRINT_ALL, "...using GL_ARB_multitexture\n" );
					}
					else
					{
						qglMultiTexCoord2fARB = NULL;
						qglActiveTextureARB = NULL;
						qglClientActiveTextureARB = NULL;
						ri.Printf( PRINT_ALL, "...not using GL_ARB_multitexture, < 2 texture units\n" );
					}
				}
			}
			else
			{
				ri.Printf( PRINT_ALL, "...ignoring GL_ARB_multitexture\n" );
			}
		}
		else
		{
			ri.Printf( PRINT_ALL, "...GL_ARB_multitexture not found\n" );
		}
#endif

		// GL_EXT_compiled_vertex_array
		if ( SDL_GL_ExtensionSupported( "GL_EXT_compiled_vertex_array" ) )
		{
			if ( r_ext_compiled_vertex_array->value )
			{
				ri.Printf( PRINT_ALL, "...using GL_EXT_compiled_vertex_array\n" );
                qglLockArraysEXT = (qglLockArraysEXT_t) SDL_GL_GetProcAddress( "glLockArraysEXT" );
				qglUnlockArraysEXT = (qglUnlockArraysEXT_t) SDL_GL_GetProcAddress( "glUnlockArraysEXT" );
				if (!qglLockArraysEXT || !qglUnlockArraysEXT)
				{
					ri.Error (ERR_FATAL, "bad getprocaddress");
				}
			}
			else
			{
				ri.Printf( PRINT_ALL, "...ignoring GL_EXT_compiled_vertex_array\n" );
			}
		}
		else
		{
			ri.Printf( PRINT_ALL, "...GL_EXT_compiled_vertex_array not found\n" );
		}
	}

	textureFilterAnisotropic = qfalse;
	if ( SDL_GL_ExtensionSupported( "GL_EXT_texture_filter_anisotropic" ) )
	{
		if ( r_ext_texture_filter_anisotropic->integer ) {
			qglGetIntegerv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, (GLint *)&maxAnisotropy );
			if ( maxAnisotropy <= 0 ) {
				ri.Printf( PRINT_ALL, "...GL_EXT_texture_filter_anisotropic not properly supported!\n" );
				maxAnisotropy = 0;
			}
			else
			{
				ri.Printf( PRINT_ALL, "...using GL_EXT_texture_filter_anisotropic (max: %i)\n", maxAnisotropy );
				textureFilterAnisotropic = qtrue;
			}
		}
		else
		{
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_filter_anisotropic\n" );
		}
	}
	else
	{
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_filter_anisotropic not found\n" );
	}

	haveClampToEdge = qfalse;
	if ( QGL_VERSION_ATLEAST( 1, 2 ) || QGLES_VERSION_ATLEAST( 1, 0 ) || SDL_GL_ExtensionSupported( "GL_SGIS_texture_edge_clamp" ) )
	{
		ri.Printf( PRINT_ALL, "...using GL_SGIS_texture_edge_clamp\n" );
		haveClampToEdge = qtrue;
	}
	else
	{
		ri.Printf( PRINT_ALL, "...GL_SGIS_texture_edge_clamp not found\n" );
	}
}

/*
===============
GLimp_RendererInit

Renderer-side entry point. Called from R_Init after engine-side GLimp_Init
has created the SDL window + GL context. Probes qgl* function pointers,
detects software rasterizer, populates glConfig string fields, runs
extension detection.

Returns qfalse if probe failed or the GL context is a software rasterizer.
===============
*/
qboolean GLimp_RendererInit( qboolean fixedFunction )
{
	const char *renderer;

	if ( !GLimp_GetProcAddresses( fixedFunction ) ) {
		ri.Printf( PRINT_ALL, "GLimp_RendererInit: GLimp_GetProcAddresses failed\n" );
		GLimp_ClearProcAddresses();
		return qfalse;
	}

	renderer = (const char *)qglGetString( GL_RENDERER );
	if ( !renderer || (strstr(renderer, "Software Renderer") || strstr(renderer, "Software Rasterizer")) ) {
		if ( renderer ) {
			ri.Printf( PRINT_ALL, "GL_RENDERER is %s, rejecting context\n", renderer );
		}
		GLimp_ClearProcAddresses();
		return qfalse;
	}

	ri.Printf( PRINT_ALL, "GL_RENDERER: %s\n", renderer );

	// get our config strings
	Q_strncpyz( glConfig.vendor_string, (char *) qglGetString (GL_VENDOR), sizeof( glConfig.vendor_string ) );
	Q_strncpyz( glConfig.renderer_string, (char *) qglGetString (GL_RENDERER), sizeof( glConfig.renderer_string ) );
	if (*glConfig.renderer_string && glConfig.renderer_string[strlen(glConfig.renderer_string) - 1] == '\n')
		glConfig.renderer_string[strlen(glConfig.renderer_string) - 1] = 0;
	Q_strncpyz( glConfig.version_string, (char *) qglGetString (GL_VERSION), sizeof( glConfig.version_string ) );

#ifndef USE_OPENGLES
	// manually create extension list if using OpenGL 3
	if ( qglGetStringi )
	{
		int i, numExtensions, extensionLength, listLength;
		const char *extension;

		qglGetIntegerv( GL_NUM_EXTENSIONS, &numExtensions );
		listLength = 0;

		for ( i = 0; i < numExtensions; i++ )
		{
			extension = (char *) qglGetStringi( GL_EXTENSIONS, i );
			extensionLength = strlen( extension );

			if ( ( listLength + extensionLength + 1 ) >= sizeof( glConfig.extensions_string ) )
				break;

			if ( i > 0 ) {
				Q_strcat( glConfig.extensions_string, sizeof( glConfig.extensions_string ), " " );
				listLength++;
			}

			Q_strcat( glConfig.extensions_string, sizeof( glConfig.extensions_string ), extension );
			listLength += extensionLength;
		}
	}
	else
#endif
	{
		Q_strncpyz( glConfig.extensions_string, (char *) qglGetString (GL_EXTENSIONS), sizeof( glConfig.extensions_string ) );
	}

	// initialize extensions
	GLimp_InitExtensions( fixedFunction );

	return qtrue;
}

/*
===============
GLimp_RendererShutdown

Renderer-side teardown. Clears qgl* function pointers. Called from
R_Shutdown before engine-side GLimp_Shutdown destroys the context.
===============
*/
void GLimp_RendererShutdown( void )
{
	GLimp_ClearProcAddresses();
}
