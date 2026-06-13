/*
===========================================================================
RealRTCW M9 fix: MDS skeletal animation runtime for the Vulkan renderer.

Vendor-ported from code/renderer/tr_animation.c (legacy GL1 renderer),
MDS half (lines 1-1366; the MDR half lives in code/renderervk/
tr_animation.c). RTCW's skeletal characters (default player, NPCs)
use this code path for cull, fog, frame interpolation, bone-matrix
construction, and per-vertex skinning during draw.

Reference port for non-GL1 backend adaptation: iortcw rend2
(~/fedorov_tech/refs/iortcw/SP/code/rend2/tr_animation.c).

Static state convention follows legacy verbatim — large file-static
arrays (bones[MDS_MAX_BONES], rawBones, oldBones, etc.) sit at file
scope so RB_SurfaceAnim and R_CalcBones can share without parameter-
threading. This is intentional and matches both legacy and rend2.

The realrtcw_ filename prefix is the vendor-edit convention marker
so an upstream re-vendor diff omits this file cleanly (no preprocessor
guard needed — see notes/decisions/2026-06-08-vendor-prefix-convention.md
and the M5.10 in-tree precedent at tr_image.c:633).

See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md
===========================================================================
*/

#include "tr_local.h"

//#define HIGH_PRECISION_BONES	// enable this for 32bit precision bones
//#define DBG_PROFILE_BONES

//-----------------------------------------------------------------------------
// Static Vars, ugly but easiest (and fastest) means of separating RB_SurfaceAnim
// and R_CalcBones

static float frontlerp, backlerp;
static float torsoFrontlerp, torsoBacklerp;
static int *triangles;
static glIndex_t *pIndexes;
static int indexes;
static int baseIndex, baseVertex, oldIndexes;
static int numVerts;
static mdsVertex_t     *v;
static mdsBoneFrame_t bones[MDS_MAX_BONES], rawBones[MDS_MAX_BONES], oldBones[MDS_MAX_BONES];
static char validBones[MDS_MAX_BONES];
static char newBones[ MDS_MAX_BONES ];
static mdsBoneFrame_t  *bonePtr, *bone, *parentBone;
static mdsBoneFrameCompressed_t    *cBonePtr, *cTBonePtr, *cOldBonePtr, *cOldTBonePtr, *cBoneList, *cOldBoneList, *cBoneListTorso, *cOldBoneListTorso;
static mdsBoneInfo_t   *boneInfo, *thisBoneInfo, *parentBoneInfo;
static mdsFrame_t      *frame, *torsoFrame;
static mdsFrame_t      *oldFrame, *oldTorsoFrame;
static int frameSize;
static short           *sh, *sh2;
static float           *pf;
static vec3_t angles, tangles, torsoParentOffset, torsoAxis[3], tmpAxis[3];
static float           *tempVert, *tempNormal;
static vec3_t vec, v2, dir;
static float diff, a1, a2;
static int render_count;
static float lodRadius, lodScale;
static int             *collapse_map, *pCollapseMap;
static int collapse[ MDS_MAX_VERTS ], *pCollapse;
static int p0, p1, p2;
static qboolean isTorso, fullTorso;
static vec4_t m1[4], m2[4];
//static  vec4_t m3[4], m4[4], tmp1[4], tmp2[4]; // TTimo: unused
static vec3_t t;
static refEntity_t lastBoneEntity;

static int totalrv, totalrt, totalv, totalt;    //----(SA)

//-----------------------------------------------------------------------------

static float RB_ProjectRadius( float r, vec3_t location ) {
	float pr;
	float dist;
	float c;
	vec3_t p;
	float projected[4];

	c = DotProduct( backEnd.viewParms.or.axis[0], backEnd.viewParms.or.origin );
	dist = DotProduct( backEnd.viewParms.or.axis[0], location ) - c;

	if ( dist <= 0 ) {
		return 0;
	}

	p[0] = 0;
	p[1] = fabs( r );
	p[2] = -dist;

	projected[0] = p[0] * backEnd.viewParms.projectionMatrix[0] +
				   p[1] * backEnd.viewParms.projectionMatrix[4] +
				   p[2] * backEnd.viewParms.projectionMatrix[8] +
				   backEnd.viewParms.projectionMatrix[12];

	projected[1] = p[0] * backEnd.viewParms.projectionMatrix[1] +
				   p[1] * backEnd.viewParms.projectionMatrix[5] +
				   p[2] * backEnd.viewParms.projectionMatrix[9] +
				   backEnd.viewParms.projectionMatrix[13];

	projected[2] = p[0] * backEnd.viewParms.projectionMatrix[2] +
				   p[1] * backEnd.viewParms.projectionMatrix[6] +
				   p[2] * backEnd.viewParms.projectionMatrix[10] +
				   backEnd.viewParms.projectionMatrix[14];

	projected[3] = p[0] * backEnd.viewParms.projectionMatrix[3] +
				   p[1] * backEnd.viewParms.projectionMatrix[7] +
				   p[2] * backEnd.viewParms.projectionMatrix[11] +
				   backEnd.viewParms.projectionMatrix[15];


	pr = projected[1] / projected[3];

	if ( pr > 1.0f ) {
		pr = 1.0f;
	}

	return pr;
}

/*
=============
R_CullModel
=============
*/
static int R_CullModel( mdsHeader_t *header, trRefEntity_t *ent ) {
	vec3_t bounds[2];
	mdsFrame_t  *oldFrame, *newFrame;
	int i, frameSize;
	qboolean cullSphere;
	float radScale;

	cullSphere = qtrue;

	frameSize = (int) ( sizeof( mdsFrame_t ) - sizeof( mdsBoneFrameCompressed_t ) + header->numBones * sizeof( mdsBoneFrameCompressed_t ) );

	// compute frame pointers
	newFrame = ( mdsFrame_t * )( ( byte * ) header + header->ofsFrames + ent->e.frame * frameSize );
	oldFrame = ( mdsFrame_t * )( ( byte * ) header + header->ofsFrames + ent->e.oldframe * frameSize );

	radScale = 1.0f;

	if ( ent->e.nonNormalizedAxes ) {
		cullSphere = qfalse;    // by defalut, cull bounding sphere ONLY if this is not an upscaled entity

		// but allow the radius to be scaled if specified
//		if(ent->e.reFlags & REFLAG_SCALEDSPHERECULL) {
//			cullSphere = qtrue;
//			radScale = ent->e.radius;
//		}
	}

	if ( cullSphere ) {
		if ( ent->e.frame == ent->e.oldframe ) {
			switch ( R_CullLocalPointAndRadius( newFrame->localOrigin, newFrame->radius * radScale ) )
			{
			case CULL_OUT:
				tr.pc.c_sphere_cull_md3_out++;
				return CULL_OUT;

			case CULL_IN:
				tr.pc.c_sphere_cull_md3_in++;
				return CULL_IN;

			case CULL_CLIP:
				tr.pc.c_sphere_cull_md3_clip++;
				break;
			}
		} else
		{
			int sphereCull, sphereCullB;

			sphereCull  = R_CullLocalPointAndRadius( newFrame->localOrigin, newFrame->radius * radScale );
			if ( newFrame == oldFrame ) {
				sphereCullB = sphereCull;
			} else {
				sphereCullB = R_CullLocalPointAndRadius( oldFrame->localOrigin, oldFrame->radius * radScale );
			}

			if ( sphereCull == sphereCullB ) {
				if ( sphereCull == CULL_OUT ) {
					tr.pc.c_sphere_cull_md3_out++;
					return CULL_OUT;
				} else if ( sphereCull == CULL_IN )   {
					tr.pc.c_sphere_cull_md3_in++;
					return CULL_IN;
				} else
				{
					tr.pc.c_sphere_cull_md3_clip++;
				}
			}
		}
	}

	// calculate a bounding box in the current coordinate system
	for ( i = 0 ; i < 3 ; i++ ) {
		bounds[0][i] = oldFrame->bounds[0][i] < newFrame->bounds[0][i] ? oldFrame->bounds[0][i] : newFrame->bounds[0][i];
		bounds[1][i] = oldFrame->bounds[1][i] > newFrame->bounds[1][i] ? oldFrame->bounds[1][i] : newFrame->bounds[1][i];

		bounds[0][i] *= radScale;   //----(SA)	added
		bounds[1][i] *= radScale;   //----(SA)	added
	}

	switch ( R_CullLocalBox( bounds ) )
	{
	case CULL_IN:
		tr.pc.c_box_cull_md3_in++;
		return CULL_IN;
	case CULL_CLIP:
		tr.pc.c_box_cull_md3_clip++;
		return CULL_CLIP;
	case CULL_OUT:
	default:
		tr.pc.c_box_cull_md3_out++;
		return CULL_OUT;
	}
}

/*
=================
RB_CalcMDSLod

=================
*/
float RB_CalcMDSLod( refEntity_t *refent, vec3_t origin, float radius, float modelBias, float modelScale ) {
	float flod, lodScale;
	float projectedRadius;

	if ( refent->reFlags & REFLAG_FULL_LOD ) {
		return 1.0f;
	}

	// compute projected bounding sphere and use that as a criteria for selecting LOD

	projectedRadius = RB_ProjectRadius( radius, origin );
	if ( projectedRadius != 0 ) {

//		ri.Printf (PRINT_ALL, "projected radius: %f\n", projectedRadius);

		lodScale = r_lodscale->value;   // fudge factor since MDS uses a much smoother method of LOD
		flod = projectedRadius * lodScale * modelScale;
	} else
	{
		// object intersects near view plane, e.g. view weapon
		flod = 1.0f;
	}

	if ( refent->reFlags & REFLAG_FORCE_LOD ) {
		flod *= 0.5;
	}
//----(SA)	like reflag_force_lod, but separate for the moment
	if ( refent->reFlags & REFLAG_DEAD_LOD ) {
		flod *= 0.8;
	}

	flod -= 0.25 * ( r_lodbias->value ) + modelBias;

	if ( flod < 0.0 ) {
		flod = 0.0;
	} else if ( flod > 1.0f ) {
		flod = 1.0f;
	}

	return flod;
}

/*
=================
R_ComputeFogNum

=================
*/
static int R_ComputeFogNum( mdsHeader_t *header, trRefEntity_t *ent ) {
	int i, j;
	fog_t           *fog;
	mdsFrame_t      *mdsFrame;
	vec3_t localOrigin;

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		return 0;
	}

	// FIXME: non-normalized axis issues
	mdsFrame = ( mdsFrame_t * )( ( byte * ) header + header->ofsFrames + ( sizeof( mdsFrame_t ) + sizeof( mdsBoneFrameCompressed_t ) * ( header->numBones - 1 ) ) * ent->e.frame );
	VectorAdd( ent->e.origin, mdsFrame->localOrigin, localOrigin );
	for ( i = 1 ; i < tr.world->numfogs ; i++ ) {
		fog = &tr.world->fogs[i];
		for ( j = 0 ; j < 3 ; j++ ) {
			if ( localOrigin[j] - mdsFrame->radius >= fog->bounds[1][j] ) {
				break;
			}
			if ( localOrigin[j] + mdsFrame->radius <= fog->bounds[0][j] ) {
				break;
			}
		}
		if ( j == 3 ) {
			return i;
		}
	}

	return 0;
}
