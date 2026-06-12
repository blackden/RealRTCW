/*
===========================================================================
RealRTCW M9 fix: MDC compressed-mesh loader for the Vulkan renderer.

Vendor-ported from code/renderer/tr_model.c (legacy GL1 renderer), with
behavioral adaptations for renderervk's surface dispatch shape. Reference
port for non-GL1 backend adaptation: iortcw rend2 (~/fedorov_tech/refs/
iortcw/SP/code/rend2/tr_model.c lines 564-900).

This file exists in renderervk because Quake3e (the upstream of
renderervk) is Q3-only and never had MDC support. RTCW ships player
meshes and many world props as MDC, so registration must succeed
engine-side or DEFAULT_MODEL fails and the server crashes on map load.

Guarded by REALRTCW_ALLOW_VENDOR_EDIT so an upstream re-vendor diff
omits this file cleanly.

See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md
===========================================================================
*/

#ifdef REALRTCW_ALLOW_VENDOR_EDIT

#include "tr_local.h"

#define LL( x ) x = LittleLong( x )

/*
=================
R_LoadMDC
=================
*/
qboolean R_LoadMDC( model_t *mod, int lod, void *buffer, int fileSize, const char *mod_name ) {
	int i, j;
	mdcHeader_t         *pinmodel;
	md3Frame_t          *frame;
	mdcSurface_t        *surf;
	md3Shader_t         *shader;
	md3Triangle_t       *tri;
	md3St_t             *st;
	md3XyzNormal_t      *xyz;
	mdcXyzCompressed_t  *xyzComp;
#ifdef Q3_BIG_ENDIAN
	mdcTag_t            *tag;
#endif
	short               *ps;
	int version;
	int size;

	(void)fileSize; /* reserved for future bounds checks against truncated PAK reads */

	pinmodel = (mdcHeader_t *)buffer;

	version = LittleLong( pinmodel->version );
	if ( version != MDC_VERSION ) {
		ri.Printf( PRINT_WARNING, "R_LoadMDC: %s has wrong version (%i should be %i)\n",
				   mod_name, version, MDC_VERSION );
		return qfalse;
	}

	mod->type = MOD_MDC;
	size = LittleLong( pinmodel->ofsEnd );
	mod->dataSize += size;
	mod->mdc[lod] = ri.Hunk_Alloc( size, h_low );

	memcpy( mod->mdc[lod], buffer, LittleLong( pinmodel->ofsEnd ) );

	LL( mod->mdc[lod]->ident );
	LL( mod->mdc[lod]->version );
	LL( mod->mdc[lod]->numFrames );
	LL( mod->mdc[lod]->numTags );
	LL( mod->mdc[lod]->numSurfaces );
	LL( mod->mdc[lod]->ofsFrames );
	LL( mod->mdc[lod]->ofsTagNames );
	LL( mod->mdc[lod]->ofsTags );
	LL( mod->mdc[lod]->ofsSurfaces );
	LL( mod->mdc[lod]->ofsEnd );
	LL( mod->mdc[lod]->flags );
	LL( mod->mdc[lod]->numSkins );


	if ( mod->mdc[lod]->numFrames < 1 ) {
		ri.Printf( PRINT_WARNING, "R_LoadMDC: %s has no frames\n", mod_name );
		return qfalse;
	}

	// swap all the frames
	frame = ( md3Frame_t * )( (byte *)mod->mdc[lod] + mod->mdc[lod]->ofsFrames );
	for ( i = 0 ; i < mod->mdc[lod]->numFrames ; i++, frame++ ) {
		frame->radius = LittleFloat( frame->radius );
		if ( strstr( mod->name,"sherman" ) || strstr( mod->name, "mg42" ) ) {
			frame->radius = 256;
			for ( j = 0 ; j < 3 ; j++ ) {
				frame->bounds[0][j] = 128;
				frame->bounds[1][j] = -128;
				frame->localOrigin[j] = LittleFloat( frame->localOrigin[j] );
			}
		} else
		{
			for ( j = 0 ; j < 3 ; j++ ) {
				frame->bounds[0][j] = LittleFloat( frame->bounds[0][j] );
				frame->bounds[1][j] = LittleFloat( frame->bounds[1][j] );
				frame->localOrigin[j] = LittleFloat( frame->localOrigin[j] );
			}
		}
	}

#ifdef Q3_BIG_ENDIAN
	// swap all the tags
	tag = ( mdcTag_t * )( (byte *)mod->mdc[lod] + mod->mdc[lod]->ofsTags );
	for ( i = 0 ; i < mod->mdc[lod]->numTags * mod->mdc[lod]->numFrames ; i++, tag++ ) {
		for ( j = 0 ; j < 3 ; j++ ) {
			tag->xyz[j] = LittleShort( tag->xyz[j] );
			tag->angles[j] = LittleShort( tag->angles[j] );
		}
	}
#endif

	// swap all the surfaces
	surf = ( mdcSurface_t * )( (byte *)mod->mdc[lod] + mod->mdc[lod]->ofsSurfaces );
	for ( i = 0 ; i < mod->mdc[lod]->numSurfaces ; i++ ) {

		LL( surf->ident );
		LL( surf->flags );
		LL( surf->numBaseFrames );
		LL( surf->numCompFrames );
		LL( surf->numShaders );
		LL( surf->numTriangles );
		LL( surf->ofsTriangles );
		LL( surf->numVerts );
		LL( surf->ofsShaders );
		LL( surf->ofsSt );
		LL( surf->ofsXyzNormals );
		LL( surf->ofsXyzCompressed );
		LL( surf->ofsFrameBaseFrames );
		LL( surf->ofsFrameCompFrames );
		LL( surf->ofsEnd );

		if ( surf->numVerts >= SHADER_MAX_VERTEXES ) {
			ri.Printf(PRINT_WARNING, "R_LoadMDC: %s has more than %i verts on %s (%i).\n",
				mod_name, SHADER_MAX_VERTEXES - 1, surf->name[0] ? surf->name : "a surface",
				surf->numVerts );
			return qfalse;
		}
		if ( surf->numTriangles*3 >= SHADER_MAX_INDEXES ) {
			ri.Printf(PRINT_WARNING, "R_LoadMDC: %s has more than %i triangles on %s (%i).\n",
				mod_name, ( SHADER_MAX_INDEXES / 3 ) - 1, surf->name[0] ? surf->name : "a surface",
				surf->numTriangles );
			return qfalse;
		}

		// change to surface identifier
		surf->ident = SF_MDC;

		// lowercase the surface name so skin compares are faster
		Q_strlwr( surf->name );

		// strip off a trailing _1 or _2
		// this is a crutch for q3data being a mess
		j = strlen( surf->name );
		if ( j > 2 && surf->name[j - 2] == '_' ) {
			surf->name[j - 2] = 0;
		}

		// register the shaders
		shader = ( md3Shader_t * )( (byte *)surf + surf->ofsShaders );
		for ( j = 0 ; j < surf->numShaders ; j++, shader++ ) {
			shader_t    *sh;

			sh = R_FindShader( shader->name, LIGHTMAP_NONE, qtrue );
			if ( sh->defaultShader ) {
				shader->shaderIndex = 0;
			} else {
				shader->shaderIndex = sh->index;
			}
		}

		// Ridah, optimization, only do the swapping if we really need to
		if ( LittleShort( 1 ) != 1 ) {

			// swap all the triangles
			tri = ( md3Triangle_t * )( (byte *)surf + surf->ofsTriangles );
			for ( j = 0 ; j < surf->numTriangles ; j++, tri++ ) {
				LL( tri->indexes[0] );
				LL( tri->indexes[1] );
				LL( tri->indexes[2] );
			}

			// swap all the ST
			st = ( md3St_t * )( (byte *)surf + surf->ofsSt );
			for ( j = 0 ; j < surf->numVerts ; j++, st++ ) {
				st->st[0] = LittleFloat( st->st[0] );
				st->st[1] = LittleFloat( st->st[1] );
			}

			// swap all the XyzNormals
			xyz = ( md3XyzNormal_t * )( (byte *)surf + surf->ofsXyzNormals );
			for ( j = 0 ; j < surf->numVerts * surf->numBaseFrames ; j++, xyz++ )
			{
				xyz->xyz[0] = LittleShort( xyz->xyz[0] );
				xyz->xyz[1] = LittleShort( xyz->xyz[1] );
				xyz->xyz[2] = LittleShort( xyz->xyz[2] );

				xyz->normal = LittleShort( xyz->normal );
			}

			// swap all the XyzCompressed
			xyzComp = ( mdcXyzCompressed_t * )( (byte *)surf + surf->ofsXyzCompressed );
			for ( j = 0 ; j < surf->numVerts * surf->numCompFrames ; j++, xyzComp++ )
			{
				LL( xyzComp->ofsVec );
			}

			// swap the frameBaseFrames
			ps = ( short * )( (byte *)surf + surf->ofsFrameBaseFrames );
			for ( j = 0; j < mod->mdc[lod]->numFrames; j++, ps++ )
			{
				*ps = LittleShort( *ps );
			}

			// swap the frameCompFrames
			ps = ( short * )( (byte *)surf + surf->ofsFrameCompFrames );
			for ( j = 0; j < mod->mdc[lod]->numFrames; j++, ps++ )
			{
				*ps = LittleShort( *ps );
			}
		}
		// done.

		// find the next surface
		surf = ( mdcSurface_t * )( (byte *)surf + surf->ofsEnd );
	}

	return qtrue;
}

#endif /* REALRTCW_ALLOW_VENDOR_EDIT */
