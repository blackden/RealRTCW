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

/* M6 (notes/decisions/2026-06-12-m6-types-unification.md): this file and
 * code/renderer/tr_types.h MUST stay byte-equivalent in their type
 * definitions. Both use the shared __TR_TYPES_H guard so only one wins per
 * TU, but they must produce identical refEntity_t/refdef_t/glconfig_t/
 * polyVert_t layouts. A _Static_assert in code/client/cl_refvulkan.c locks
 * sizes; if you edit one file, mirror the same change to the other. */
#ifndef __TR_TYPES_H
#define __TR_TYPES_H

// RealRTCW shim: renderervk TUs include this header without prior
// q_shared.h in scope. Force the include here so byte/qhandle_t/vec3_t/
// qboolean/color4ub_t/floatint_t resolve. Engine TUs (which include the
// SMALL renderer/tr_types.h) already have q_shared.h in scope from their
// own qcommon.h chain — this include is idempotent there.
#include "../qcommon/q_shared.h"

// Defensive: if a build configuration somehow misses the q_shared.h
// color4ub_t typedef (older RealRTCW point in history without
// COLOR4UB_T_DEFINED), provide it locally. Should never trigger on
// current q_shared.h but harmless.
#ifndef COLOR4UB_T_DEFINED
#define COLOR4UB_T_DEFINED
typedef union {
	byte     rgba[4];
	uint32_t u32;
} color4ub_t;
#endif


#define MAX_CORONAS     32          //----(SA)	not really a reason to limit this other than trying to keep a reasonable count
#define MAX_DLIGHTS     32          // can't be increased, because bit flags are used on surfaces
#define	REFENTITYNUM_BITS	11		// can't be increased without changing drawsurf bit packing
#define REFENTITYNUM_MASK ((uint64_t)((1ull << REFENTITYNUM_BITS) - 1))
// the last N-bit number (2^REFENTITYNUM_BITS - 1) is reserved for the special world refentity,
//  and this is reflected by the value of MAX_REFENTITIES (which therefore is not a power-of-2)
#define	MAX_REFENTITIES		((1<<REFENTITYNUM_BITS) - 1)
#define	REFENTITYNUM_WORLD	((1<<REFENTITYNUM_BITS) - 1)

// renderfx flags
#define RF_MINLIGHT         0x0001       // allways have some light (viewmodel, some items)
#define RF_THIRD_PERSON     0x0002       // don't draw through eyes, only mirrors (player bodies, chat sprites)
#define RF_FIRST_PERSON     0x0004       // only draw through eyes (view weapon, damage blood blob)
#define RF_DEPTHHACK        0x0008       // for view weapon Z crunching

#define RF_CROSSHAIR		0x0010		// This item is a cross hair and will draw over everything similar to
						// DEPTHHACK in stereo rendering mode, with the difference that the
						// projection matrix won't be hacked to reduce the stereo separation as
						// is done for the gun.

#define RF_NOSHADOW         0x0040      // don't add stencil shadows

#define RF_LIGHTING_ORIGIN  0x0080     // use refEntity->lightingOrigin instead of refEntity->origin
									// for lighting.  This allows entities to sink into the floor
									// with their origin going solid, and allows all parts of a
									// player to get the same lighting
#define RF_SHADOW_PLANE     0x0100     // use refEntity->shadowPlane
#define RF_WRAP_FRAMES      0x0200     // mod the model frames by the maxframes to allow continuous
				       // animation without needing to know the frame count

#define RF_HILIGHT          ( 1 << 8 )  // more than RF_MINLIGHT.  For when an object is "Highlighted" (looked at/training identification/etc)
#define RF_BLINK            ( 1 << 9 )  // eyes in 'blink' state

// refdef flags
#define RDF_NOWORLDMODEL    0x0001       // used for player configuration screen
#define RDF_HYPERSPACE      0x0004       // teleportation effect

// Rafael
#define RDF_SKYBOXPORTAL    0x0008

#define RDF_DRAWSKYBOX      0x0010      // the above marks a scene as being a 'portal sky'.  this flag says to draw it or not

//----(SA)
#define RDF_UNDERWATER      ( 1 << 4 )  // so the renderer knows to use underwater fog when the player is underwater
#define RDF_DRAWINGSKY      ( 1 << 5 )
#define RDF_SNOOPERVIEW     ( 1 << 6 )  //----(SA)	added


typedef struct {
	vec3_t xyz;
	float st[2];
	byte modulate[4];
} polyVert_t;

typedef struct poly_s {
	qhandle_t hShader;
	int numVerts;
	polyVert_t          *verts;
} poly_t;

typedef enum {
	RT_MODEL,
	RT_POLY,
	RT_SPRITE,
	RT_SPLASH,  // ripple effect
	RT_BEAM,
	RT_RAIL_CORE,
	RT_RAIL_CORE_TAPER, // a modified core that creates a properly texture mapped core that's wider at one end
	RT_RAIL_RINGS,
	RT_LIGHTNING,
	RT_PORTALSURFACE,       // doesn't draw anything, just info for portals

	RT_MAX_REF_ENTITY_TYPE
} refEntityType_t;

#define ZOMBIEFX_FADEOUT_TIME   10000

#define REFLAG_ONLYHAND     1   // only draw hand surfaces
#define REFLAG_ZOMBIEFX     2   // special post-tesselation processing for zombie skin
#define REFLAG_ZOMBIEFX2    4   // special post-tesselation processing for zombie skin
#define REFLAG_FORCE_LOD    8   // force a low lod
#define REFLAG_ORIENT_LOD   16  // on LOD switch, align the model to the player's camera
#define REFLAG_DEAD_LOD     32  // allow the LOD to go lower than recommended
#define REFLAG_SCALEDSPHERECULL 64  // on LOD switch, align the model to the player's camera
#define REFLAG_FULL_LOD     8   // force a FULL lod

typedef struct {
	refEntityType_t reType;
	int renderfx;

	qhandle_t hModel;               // opaque type outside refresh

	// most recent data
	vec3_t lightingOrigin;          // so multi-part models can be lit identically (RF_LIGHTING_ORIGIN)
	float shadowPlane;              // projection shadows go here, stencils go slightly lower

	vec3_t axis[3];                 // rotation vectors
	vec3_t torsoAxis[3];            // rotation vectors for torso section of skeletal animation
	qboolean nonNormalizedAxes;     // axis are not normalized, i.e. they have scale
	float origin[3];                // also used as MODEL_BEAM's "from"
	int frame;                      // also used as MODEL_BEAM's diameter
	int torsoFrame;                 // skeletal torso can have frame independant of legs frame

	vec3_t scale;       //----(SA)	added

	// previous data for frame interpolation
	float oldorigin[3];             // also used as MODEL_BEAM's "to"
	int oldframe;
	int oldTorsoFrame;
	float backlerp;                 // 0.0 = current, 1.0 = old
	float torsoBacklerp;

	// texturing
	int skinNum;                    // inline skin index
	qhandle_t customSkin;           // NULL for default skin
	qhandle_t customShader;         // use one image for the entire thing

	// misc
	/* Entity tint: engine cgame writes via .shaderRGBA[N] (many sites in
	 * cg_effects/cg_view/cg_players); renderervk reads .shader.rgba[N]
	 * AND passes e.shader by-value to RB_AddQuadStamp expecting
	 * color4ub_t (tr_surface.c:248). The inner member is typed as
	 * color4ub_t (defined in q_shared.h as union {byte rgba[4]; uint32_t u32;})
	 * so the value-pass type-matches; shaderRGBA[] alias provides
	 * engine-side bytewise write access at the same 4 bytes. */
	union {
		byte shaderRGBA[4];
		color4ub_t shader;
	};
	float shaderTexCoord[2];        // texture coordinates used by tcMod entity modifiers
	floatint_t shaderTime;          // -EC- promoted to union for renderervk integer-encoded shaderTime trick (cg_localents.c writes .i, tr_backend.c:680,907 reads .i/.f)

	// extra sprite information
	float radius;
	float rotation;

	// Ridah
	vec3_t fireRiseDir;

	// Ridah, entity fading (gibs, debris, etc)
	int fadeStartTime, fadeEndTime;

	float hilightIntensity;         //----(SA)	added

	int reFlags;

	int entityNum;                  // currentState.number, so we can attach rendering effects to specific entities (Zombie)

} refEntity_t;

//----(SA)

//                                                                  //
// WARNING:: synch FOG_SERVER in sv_ccmds.c if you change anything	//
//                                                                  //
typedef enum {
	FOG_NONE,       //	0

	FOG_SKY,        //	1	fog values to apply to the sky when using density fog for the world (non-distance clipping fog) (only used if(glfogsettings[FOG_MAP].registered) or if(glfogsettings[FOG_MAP].registered))
	FOG_PORTALVIEW, //	2	used by the portal sky scene
	FOG_HUD,        //	3	used by the 3D hud scene

	//		The result of these for a given frame is copied to the scene.glFog when the scene is rendered

	// the following are fogs applied to the main world scene
	FOG_MAP,        //	4	use fog parameter specified using the "fogvars" in the sky shader
	FOG_WATER,      //	5	used when underwater
	FOG_SERVER,     //	6	the server has set my fog (probably a target_fog) (keep synch in sv_ccmds.c !!!)
	FOG_CURRENT,    //	7	stores the current values when a transition starts
	FOG_LAST,       //	8	stores the current values when a transition starts
	FOG_TARGET,     //	9	the values it's transitioning to.

	FOG_CMD_SWITCHFOG,  // 10	transition to the fog specified in the second parameter of R_SetFog(...) (keep synch in sv_ccmds.c !!!)

	NUM_FOGS
} glfogType_t;


typedef struct {
	int mode;                   // GL_LINEAR, GL_EXP
	int hint;                   // GL_DONT_CARE
	int startTime;              // in ms
	int finishTime;             // in ms
	float color[4];
	float start;                // near
	float end;                  // far
	qboolean useEndForClip;     // use the 'far' value for the far clipping plane
	float density;              // 0.0-1.0
	qboolean registered;        // has this fog been set up?
	qboolean drawsky;           // draw skybox
	qboolean clearscreen;       // clear the GL color buffer

	int dirty;
} glfog_t;

//----(SA)	end


#define MAX_RENDER_STRINGS          8
#define MAX_RENDER_STRING_LENGTH    32

typedef struct {
	int x, y, width, height;
	float fov_x, fov_y;
	vec3_t vieworg;
	vec3_t viewaxis[3];             // transformation matrix

	int time;           // time in milliseconds for shader effects and other time dependent rendering issues
	int rdflags;                    // RDF_NOWORLDMODEL, etc

	// 1 bits will prevent the associated area from rendering at all
	byte areamask[MAX_MAP_AREA_BYTES];




	// text messages for deform text shaders
	char text[MAX_RENDER_STRINGS][MAX_RENDER_STRING_LENGTH];


//----(SA)	added (needed to pass fog infos into the portal sky scene)
	glfog_t glfog;
//----(SA)	end

} refdef_t;


typedef enum {
	STEREO_CENTER,
	STEREO_LEFT,
	STEREO_RIGHT
} stereoFrame_t;


/*
** glconfig_t
**
** Contains variables specific to the OpenGL configuration
** being run right now.  These are constant once the OpenGL
** subsystem is initialized.
*/
typedef enum {
	TC_NONE,
	TC_S3TC,	// this is for the GL_S3_s3tc extension.
	TC_S3TC_ARB,	// this is for the GL_EXT_texture_compression_s3tc extension.
	TC_EXT_COMP_S3TC
} textureCompression_t;

typedef enum {
	GLDRV_ICD,                  // driver is integrated with window system
								// WARNING: there are tests that check for
								// > GLDRV_ICD for minidriverness, so this
								// should always be the lowest value in this
								// enum set
	GLDRV_STANDALONE,           // driver is a non-3Dfx standalone driver
	GLDRV_VOODOO                // driver is a 3Dfx standalone driver
} glDriverType_t;

typedef enum {
	GLHW_GENERIC,           // where everthing works the way it should
	GLHW_3DFX_2D3D,         // Voodoo Banshee or Voodoo3, relevant since if this is
							// the hardware type then there can NOT exist a secondary
							// display adapter
	GLHW_RIVA128,           // where you can't interpolate alpha
	GLHW_RAGEPRO,           // where you can't modulate alpha on alpha textures
	GLHW_PERMEDIA2          // where you don't have src*dst
} glHardwareType_t;

typedef struct {
	char renderer_string[MAX_STRING_CHARS];
	char vendor_string[MAX_STRING_CHARS];
	char version_string[MAX_STRING_CHARS];
	char extensions_string[4 * MAX_STRING_CHARS];                       // this is actually too short for many current cards/drivers  // (SA) doubled from 2x to 4x MAX_STRING_CHARS

	int maxTextureSize;                             // queried from GL
	int numTextureUnits;				// multitexture ability

	int colorBits, depthBits, stencilBits;

	glDriverType_t driverType;
	glHardwareType_t hardwareType;

	qboolean deviceSupportsGamma;
	textureCompression_t textureCompression;
	qboolean textureEnvAddAvailable;
	qboolean anisotropicAvailable;                  //----(SA)	added
	float maxAnisotropy;                            //----(SA)	added

	// vendor-specific support
	// NVidia
	qboolean NVFogAvailable;                    //----(SA)	added
	int NVFogMode;                                  //----(SA)	added
	// ATI
	int ATIMaxTruformTess;                          // for truform support
	int ATINormalMode;                          // for truform support
	int ATIPointMode;                           // for truform support

	int vidWidth, vidHeight;
	// aspect is the screen's physical width / height, which may be different
	// than scrWidth / scrHeight if the pixels are non-square
	// normal screens should be 4/3, but wide aspect monitors may be 16/9
	float windowAspect;

	int displayFrequency;

	// synonymous with "does rendering consume the entire screen?", therefore
	// a Voodoo or Voodoo2 will have this set to TRUE, as will a Win32 ICD that
	// used CDS.
	qboolean isFullscreen;
	qboolean stereoEnabled;
	qboolean smpActive;                     // UNUSED, present for compatibility

	qboolean textureFilterAnisotropicAvailable;                 //DAJ
} glconfig_t;


#endif  // __TR_TYPES_H
