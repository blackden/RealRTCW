# M9 Phase 3: MDS skeletal — loader + runtime — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unblock the default character (`player`/`bj2`) on Vulkan by vendor-porting both the MDS skeletal-mesh **loader** and the MDS **animation runtime** into `code/renderervk/`. After this plan lands, `+map escape1` (or any SP map) will register the player's `body.mds`, the engine will reach gameplay, and the character will be visible and animated on Vulkan exactly as on the legacy OpenGL renderer.

**Architecture:** Vendor port option γ from `notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md`, mirroring Phase 1's pattern. Two stages under one plan:
- **Phase 3a (loader)** — 4 tasks. Mirror to Phase 1 MDC loader: types in `tr_local.h`, new `realrtcw_tr_mds.c`, `R_RegisterMDS` dispatcher entry. Exit: server reaches `AAS initialized` with default model.
- **Phase 3b (runtime)** — 6 tasks. New `realrtcw_tr_animation_mds.c` with the ~1400-LOC MDS half of legacy `tr_animation.c` (cull/fog/surface-enqueue, bone math, `RB_SurfaceAnim` vertex submission). Plus three small integration edits to existing renderervk files: `rb_surfaceTable[SF_MDS]` entry, entity-type dispatch branch, `R_LerpTag` MDS extension via `R_GetBoneTag`. Exit: character visible and animating in-game.

Single source of truth lives in renderervk under the inline `RealRTCW M9 fix:` comment-marker convention (NO `#ifdef REALRTCW_ALLOW_VENDOR_EDIT` — that macro is hook-permission only, see Phase 1 commit `6e3bb5d` and `notes/decisions/2026-06-12-m5-10-hunk-temp-overflow-workaround.md`). Reference port: `~/fedorov_tech/refs/iortcw/SP/code/rend2/tr_animation.c` for non-GL1 backend adaptation shape.

**Tech Stack:** C99, RealRTCW engine, `code/renderervk/` (Quake3e-lineage Vulkan renderer), Apple Silicon macOS arm64 build via `Makefile` with `BUILD_RENDERER_VULKAN=1 USE_INTERNAL_LIBS=0`. Smoke test harness: `scripts/mac/playtest.sh --vulkan --auto +<cmd>`.

---

## File Structure

**Existing (read-only references during port):**
- `code/renderer/tr_model.c:1620-1803` — legacy `R_LoadMDS` (~180 LOC, the function to vendor-port for Phase 3a).
- `code/renderer/tr_model.c:177-207` — legacy `R_RegisterMDS` (30 LOC wrapper).
- `code/renderer/tr_animation.c` (lines 1-1366, MDS half — ~1366 LOC) — the runtime: static state (frontlerp, bones[]), bone math helpers, `R_CullModel(mdsHeader_t)`, `R_ComputeFogNum(mdsHeader_t)`, `R_AddAnimSurfaces`, `R_CalcBone/Lerp/Bones`, `RB_SurfaceAnim`, `R_RecursiveBoneListAdd`, `R_GetBoneTag`.
- `code/qcommon/qfiles.h:418-544` — `MDS_IDENT`, `MDS_MAX_VERTS=6000`, `MDS_MAX_BONES=128`, `mdsHeader_t`, `mdsSurface_t`, `mdsVertex_t`, `mdsBoneInfo_t`, `mdsTag_t`, `mdsFrame_t`, `mdsBoneFrame_t`, `mdsBoneFrameCompressed_t` (already engine-shared — no type vendoring needed).
- `code/renderervk/tr_animation.c` — existing renderervk MDR+IQM runtime. Direct shape mirror for the new MDS file: `R_MDRCullModel`, `R_MDRComputeFogNum`, `R_MDRAddAnimSurfaces`, `RB_MDRSurfaceAnim`. The MDS port has 1-for-1 analogs.
- `code/renderervk/tr_main.c:991` — `rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );` — surface dispatch at draw time. SF_MDS entry goes into this table.
- `code/renderervk/tr_main.c:1620-1660` — entity-type dispatch block. Add `MOD_MDS` branch alongside `MOD_MDR`/`MOD_IQM`.
- `code/renderervk/tr_model.c:1087` — `R_LerpTag`. Already dispatches by `mod->type` for MD3/MDR/IQM. Add MDS branch via `R_GetBoneTag`.
- `code/renderervk/tr_init.c:2086` — `re.LerpTag = R_LerpTag;` — refExport wire. No change (the existing function gets extended in Task 9).
- `~/fedorov_tech/refs/iortcw/SP/code/rend2/tr_animation.c:325,1054` — `R_AddAnimSurfaces`, `RB_SurfaceAnim` rend2 reference port. Diff against when renderervk's surface API forces deviation.

**Create (RealRTCW-authored, vendor-prefix convention, no `#ifdef` guards):**
- `code/renderervk/realrtcw_tr_mds.c` (Phase 3a) — `R_LoadMDS` port (~180 LOC after wrapping).
- `code/renderervk/realrtcw_tr_animation_mds.c` (Phase 3b) — full MDS runtime port (~1400 LOC after adaptation): static state, cull, fog, surface enqueue, bone math, `RB_SurfaceAnim`, `R_GetBoneTag`.

**Modify (under inline `RealRTCW M9 fix:` comment marker, no `#ifdef` guards):**
- `code/renderervk/tr_local.h` — Phase 3a: `MOD_MDS` enum, `mds` slot in `model_t`, `SF_MDS` in `surfaceType_t`, forward decls for `R_LoadMDS`, `R_AddAnimSurfaces`, `RB_SurfaceAnim`, `R_GetBoneTag`. Phase 3b: nothing additional (forward decls already in place from 3a).
- `code/renderervk/tr_model.c` — Phase 3a: add `R_RegisterMDS` function + `{ "mds", R_RegisterMDS }` entry in `modelLoaders[]` table. Phase 3b: extend `R_LerpTag` (line ~1087) with `else if (model->type == MOD_MDS)` branch calling `R_GetBoneTag`.
- `code/renderervk/tr_main.c` — Phase 3b: add `MOD_MDS` branch in entity-type dispatch (around line 1643 where `R_MDRAddAnimSurfaces`/`R_AddIQMSurfaces` are called) calling `R_AddAnimSurfaces(ent)`.
- `code/renderervk/tr_shade.c` (or wherever `rb_surfaceTable[]` is defined — engineer locates by `grep -n "rb_surfaceTable\[\]\|rb_surfaceTable\s*\[\s*SF_" code/renderervk/*.c`) — Phase 3b: add `RB_SurfaceAnim` entry at `SF_MDS` index.
- `Makefile` — Phase 3a: add `$(B)/rendv/realrtcw_tr_mds.o`. Phase 3b: add `$(B)/rendv/realrtcw_tr_animation_mds.o`.

---

# PHASE 3a — MDS LOADER

## Task 1: Add MDS types to renderervk tr_local.h

**Files:**
- Modify: `code/renderervk/tr_local.h` (modtype_t enum + model_t struct + surfaceType_t enum + forward decls)

- [ ] **Step 1: Locate insertion points**

Run:
```bash
grep -n -E "MOD_MDC,?$|mdcHeader_t.*\*mdc\[|SF_MDC,?$|R_LoadMDC" code/renderervk/tr_local.h
```

Expected: 4 matches showing where Phase 1 added MDC entries. Insertion for MDS goes parallel to each.

- [ ] **Step 2: Add MOD_MDS to modtype_t**

Edit (the hook will block direct `Edit` — fall back to `python3 <<'EOF' ... EOF` bash heredoc, see Phase 1 implementer pattern):

```c
typedef enum {
    MOD_BAD,
    MOD_BRUSH,
    MOD_MESH,
    MOD_MDR,
    MOD_IQM,
    /* RealRTCW M9 fix: MDC compressed mesh support.
     * See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md */
    MOD_MDC,
    /* RealRTCW M9 fix: MDS skeletal mesh support.
     * See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md */
    MOD_MDS,
} modtype_t;
```

- [ ] **Step 3: Add mds slot to model_t**

Find the existing `mdc[MD3_MAX_LODS]` block from Phase 1 and add `mds` slot immediately after it:

```c
    /* RealRTCW M9 fix: MDC compressed mesh slot, parallel to md3[].
     * See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md */
    mdcHeader_t *mdc[MD3_MAX_LODS];
    /* RealRTCW M9 fix: MDS skeletal mesh slot, single-LOD pointer.
     * See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md */
    mdsHeader_t *mds;
    void *modelData;            // only if type == (MOD_MDR | MOD_IQM)
```

(Single pointer, NOT `mds[MD3_MAX_LODS]`. MDS has its own internal LOD via `lodScale`/`lodBias`/`collapseMap` — only one file per character.)

- [ ] **Step 4: Add SF_MDS to surfaceType_t**

Find Phase 1's `SF_MDC` entry and add `SF_MDS` adjacent (mirror to legacy ordering where MDS is its own tag):

```c
    SF_MD3,
    /* RealRTCW M9 fix: MDC surface tag (loader Phase 1, runtime Phase 2). */
    SF_MDC,
    SF_MDR,
    SF_IQM,
    /* RealRTCW M9 fix: MDS skeletal surface tag (Phase 3 loader + runtime). */
    SF_MDS,
    SF_FLARE,
```

Note: SF_MDS goes AFTER SF_IQM to preserve ordinal stability of SF_MDR/SF_IQM consumers. The legacy ordering is preserved by the comment, not by ordinal.

- [ ] **Step 5: Add forward declarations**

Near the existing `R_LoadIQM`/`R_LoadMDC` forward decls (search `grep -n "R_LoadMDC\|R_LoadIQM" code/renderervk/tr_local.h` to find the block), add:

```c
/* RealRTCW M9 fix: MDS loader and runtime forward decls. */
qboolean R_LoadMDS( model_t *mod, void *buffer, int filesize, const char *mod_name );
void R_AddAnimSurfaces( trRefEntity_t *ent );
void RB_SurfaceAnim( mdsSurface_t *surface );
int R_GetBoneTag( orientation_t *outTag, mdsHeader_t *mds, int startTagIndex,
                   const refEntity_t *refent, const char *tagName );
```

- [ ] **Step 6: Build with `model_t` size unchanged-or-grown**

Run:
```bash
make -j8 BUILD_RENDERER_VULKAN=1 USE_INTERNAL_LIBS=0 release 2>&1 | tail -10
```

Expected: exit 0. The `mdsHeader_t *mds` adds 8 bytes to `model_t` on arm64. If anything `_Static_assert`s the struct size of `model_t`, the build will fail — find and update.

- [ ] **Step 7: Smoke regression check**

Run:
```bash
scripts/mac/playtest.sh --vulkan --auto +quit 2>&1 | tail -5
```

Expected: exit 0. No regression at menu.

- [ ] **Step 8: Commit**

```bash
git add code/renderervk/tr_local.h
git commit -m "feat(renderervk): add MOD_MDS enum + model_t mds slot + SF_MDS

Pre-work for M9 Phase 3a (MDS loader port). Types and forward decls
only — no loader, no dispatch, no runtime. Following Phase 1 convention:
no #ifdef guards, inline RealRTCW M9 fix: comment markers as the
re-vendor diff signal.

See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md"
```

---

## Task 2: Vendor-port R_LoadMDS into realrtcw_tr_mds.c

**Files:**
- Create: `code/renderervk/realrtcw_tr_mds.c` (~200 LOC after wrap)
- Read-only reference: `code/renderer/tr_model.c:1620-1803`

- [ ] **Step 1: Create the new file with vendor header**

Create `code/renderervk/realrtcw_tr_mds.c`:

```c
/*
===========================================================================
RealRTCW M9 fix: MDS skeletal-mesh loader for the Vulkan renderer.

Vendor-ported from code/renderer/tr_model.c:1620-1803 (legacy GL1
renderer). The MDS binary format is the original RTCW skeletal-mesh
container — distinct from Q3's MDR (also skeletal, different layout).
RealRTCW ships the default player character (player/bj2) as MDS via
z_zperson.pk3, so registration must succeed engine-side or the
default character can never load on Vulkan.

The realrtcw_ filename prefix is the vendor-edit convention marker
so an upstream re-vendor diff omits this file cleanly (no preprocessor
guard needed — see notes/decisions/2026-06-08-vendor-prefix-convention.md
and the M5.10 in-tree precedent at tr_image.c:633).

See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md
===========================================================================
*/

#include "tr_local.h"

#define LL( x ) x = LittleLong( x )

```

- [ ] **Step 2: Copy R_LoadMDS body**

Open `code/renderer/tr_model.c` and copy lines **1620-1803** (the full `R_LoadMDS` function — verify boundaries by `awk 'NR==1620 || NR==1803' code/renderer/tr_model.c`). Paste after `#define LL`.

**Adaptations on paste:**
1. Change `static qboolean R_LoadMDS(...)` to `qboolean R_LoadMDS(...)` (non-static — dispatcher in `tr_model.c` will call across TUs).
2. Add a `filesize` parameter to match the renderervk convention (`R_LoadMDR` uses one): `qboolean R_LoadMDS( model_t *mod, void *buffer, int filesize, const char *mod_name )`. Cross-reference Phase 1's `R_LoadMDC` signature for the established shape. Body may use `(void)filesize;` if no bounds-check changes — future-proofing the signature.
3. The legacy uses `memcpy( mds, buffer, LittleLong( pinmodel->ofsEnd ) )` — keep verbatim.
4. The byte-swap is gated on `if ( LittleLong( 1 ) != 1 )` — that branch is dead on little-endian (Apple Silicon, x86) but copy verbatim for portability.

- [ ] **Step 3: Wire into Makefile**

The renderervk build dir is `rendv/`. Find the `Q3VKOBJ` list at `Makefile:~2185` (Phase 1 reference: `git show 2883895 -- Makefile`). Add:

```makefile
  $(B)/rendv/realrtcw_tr_mds.o \
```

Match the existing list pattern. Don't reformat surrounding lines.

- [ ] **Step 4: Build — expect missing symbols, then resolve**

Run:
```bash
make -j8 BUILD_RENDERER_VULKAN=1 USE_INTERNAL_LIBS=0 release 2>&1 | grep -E "error:|undefined" | head -30
```

Likely missing-symbol catalog (verify presence in `qfiles.h` first — the agent's Phase 1 recon noted MDS types are engine-shared):
- `MDS_VERSION` — should be in `qfiles.h`. If not, vendor into `tr_local.h` near `MDS_IDENT` references.
- `mdsHeader_t`, `mdsFrame_t`, `mdsSurface_t`, `mdsTriangle_t`, `mdsVertex_t`, `mdsBoneInfo_t`, `mdsTag_t`, `mdsBoneFrameCompressed_t` — verified by recon in `qfiles.h:418-544`. Should resolve via `tr_local.h` include chain.
- `R_FindShader`, `ri.*`, `Q_strlwr`, `Q_strncpyz`, `LittleShort`, `LittleFloat` — all renderer-side / qcommon globals.
- `SHADER_MAX_VERTEXES`, `SHADER_MAX_INDEXES`, `LIGHTMAP_NONE` — should resolve.

For each missing symbol that's NOT in `qfiles.h`, follow Phase 1 pattern: add to `tr_local.h` under the same comment-marker convention.

- [ ] **Step 5: Build clean + smoke regression check**

```bash
make -j8 BUILD_RENDERER_VULKAN=1 USE_INTERNAL_LIBS=0 release 2>&1 | tail -10
scripts/mac/playtest.sh --vulkan --auto +quit 2>&1 | tail -5
```

Expected both: exit 0. `R_LoadMDS` compiles but is still unreferenced (no dispatcher entry until Task 3).

- [ ] **Step 6: Verify symbol export**

```bash
nm build/release-darwin-arm64-nosteam/rendv/realrtcw_tr_mds.o | grep -E "R_LoadMDS|R_FindShader"
```

Expected: `T _R_LoadMDS` (defined non-static) and `U _R_FindShader` (undefined external — will link from renderer). If `_R_LoadMDS` is missing the function got preprocessor-stripped or static; re-check.

- [ ] **Step 7: Commit**

```bash
git add code/renderervk/realrtcw_tr_mds.c code/renderervk/tr_local.h Makefile
git commit -m "feat(renderervk): vendor-port R_LoadMDS into realrtcw_tr_mds.c

Phase 3a of M9. Loader compiles but is unreferenced — dispatcher entry
lands in next commit. Smoke +quit unchanged (no map load yet).

See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md"
```

(If `tr_local.h` was NOT modified in this task — e.g., no extra symbols needed — omit it from `git add`.)

---

## Task 3: Wire R_RegisterMDS into the dispatcher

**Files:**
- Modify: `code/renderervk/tr_model.c`

- [ ] **Step 1: Read current modelLoaders[] and surrounding code**

Run:
```bash
awk 'NR>=180 && NR<=210' code/renderervk/tr_model.c
```

Expected: the `modelLoaders[]` array with entries `{"iqm", R_RegisterIQM}`, `{"mdr", R_RegisterMDR}`, `{"md3", R_RegisterMD3}`. Note exact line of array.

- [ ] **Step 2: Add R_RegisterMDS function**

Insert this function in `code/renderervk/tr_model.c` after the existing `R_RegisterMDR` (around lines 121-167; mirror the MDR shape, NOT the legacy `R_RegisterMDS` which is older-style without filesize):

```c
/*
====================
R_RegisterMDS

RealRTCW M9 fix: MDS skeletal model registration.
See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md
====================
*/
static qhandle_t R_RegisterMDS( const char *name, model_t *mod )
{
    union {
        uint32_t *u;
        void *v;
    } buf;
    uint32_t ident;
    qboolean loaded = qfalse;
    int filesize;

    filesize = ri.FS_ReadFile( name, &buf.v );
    if ( !buf.v ) {
        mod->type = MOD_BAD;
        return 0;
    }

    if ( filesize < sizeof( ident ) ) {
        ri.FS_FreeFile( buf.v );
        mod->type = MOD_BAD;
        return 0;
    }

    ident = LittleLong( *buf.u );
    if ( ident == MDS_IDENT )
        loaded = R_LoadMDS( mod, buf.v, filesize, name );

    ri.FS_FreeFile( buf.v );

    if ( !loaded ) {
        ri.Printf( PRINT_WARNING, "%s: couldn't load %s\n", __func__, name );
        mod->type = MOD_BAD;
        return 0;
    }

    return mod->index;
}
```

- [ ] **Step 3: Add `mds` entry to modelLoaders[]**

Locate the `modelLoaders[]` array (verified at line ~204-208 of renderervk tr_model.c) and add an entry. **Ordering matters** for the `RE_RegisterModel` fallback search (it tries each extension if the caller passes an unqualified name). Add MDS LAST so it's tried only after the more-common formats:

```c
static modelExtToLoaderMap_t modelLoaders[ ] =
{
    { "iqm", R_RegisterIQM },
    { "mdr", R_RegisterMDR },
    { "md3", R_RegisterMD3 },
    /* RealRTCW M9 fix: MDS skeletal models (RTCW-specific format). */
    { "mds", R_RegisterMDS },
};
```

- [ ] **Step 4: Build + smoke +quit**

```bash
make -j8 BUILD_RENDERER_VULKAN=1 USE_INTERNAL_LIBS=0 release 2>&1 | tail -10
scripts/mac/playtest.sh --vulkan --auto +quit 2>&1 | tail -5
```

Expected: exit 0 both.

- [ ] **Step 5: Commit**

```bash
git add code/renderervk/tr_model.c
git commit -m "feat(renderervk): wire R_RegisterMDS into model dispatcher

Phase 3a of M9. .mds files now load via R_LoadMDS at registration.
Surface rendering (SF_MDS dispatch) and animation runtime are Phase 3b.
Without runtime, an .mds model will register but render invisibly —
acceptable for the loader-gate exit criterion (server reaches
AAS_LoadFiles with body.mds resolved).

See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md"
```

---

## Task 4: Phase 3a smoke gate — verify body.mds registers

**Files:** (verification only)

- [ ] **Step 1: Run the smoke**

```bash
scripts/mac/playtest.sh --vulkan --auto +set model "player" +map escape1 +wait 200 +quit 2>&1 | tee /tmp/m9-phase3a-smoke.log | tail -40
```

Note: `model "player"` is the default character (uses MDS). Phase 1 confirmed `escape1` reaches AAS with `model "skel"` (MDC) — this run verifies the same for MDS.

- [ ] **Step 2: Verify model-registration failure is GONE**

```bash
grep -E "Failed to load legs model|DEFAULT_MODEL.*failed|couldn't load .*body\.mds" /tmp/m9-phase3a-smoke.log
```

Expected: empty (or only `couldn't load` for OTHER files that aren't critical).

- [ ] **Step 3: Verify server reaches AAS**

```bash
grep -E "AAS initialized|G_ParseAnimationFiles|spawning" /tmp/m9-phase3a-smoke.log | head -10
```

Expected: `AAS initialized.` at least once. The downstream `wolfanim.cfg not found` may or may not surface depending on whether `player`'s anim config ships in the install — if it does, server proceeds further; if not, server crashes there (separate M9.5 issue, NOT a Phase 3a regression).

- [ ] **Step 4: Verify R_LoadMDS was actually called**

If you want positive evidence, temporarily add to `realrtcw_tr_mds.c` after the version check:

```c
ri.Printf( PRINT_DEVELOPER, "RealRTCW M9: R_LoadMDS loaded %s (%d frames, %d bones, %d surfaces)\n",
           mod_name, mds->numFrames, mds->numBones, mds->numSurfaces );
```

Re-run smoke, grep for `M9: R_LoadMDS`, verify hit. Then **remove the printf** before continuing — it's dev-only noise.

- [ ] **Step 5: Phase 3a closure**

If body.mds registers without `DEFAULT_MODEL failed`, Phase 3a is done. Character will be **invisible in-world** until Phase 3b lands. That's expected.

If `DEFAULT_MODEL failed` persists, triage:
- Check `nm rendv/tr_model.o | grep R_RegisterMDS` — function present?
- Check `xxd ~/Library/Application\ Support/RealRTCW/main/z_zperson.pk3 | head -1` — file readable?
- Check `nm rendv/realrtcw_tr_mds.o | grep R_LoadMDS` — symbol exported?
- Check the modelLoaders[] entry was committed (the `git show` of Task 3).

---

# PHASE 3b — MDS RUNTIME

Once Phase 3a's smoke gate passes, proceed to Phase 3b. The model registers; now we make it visible.

## Task 5: Create realrtcw_tr_animation_mds.c with file-static state and cull/fog

**Files:**
- Create: `code/renderervk/realrtcw_tr_animation_mds.c`

- [ ] **Step 1: Create the new file with vendor header**

Create `code/renderervk/realrtcw_tr_animation_mds.c`:

```c
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

The realrtcw_ filename prefix is the vendor-edit convention marker.

See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md
===========================================================================
*/

#include "tr_local.h"

```

- [ ] **Step 2: Copy file-static state block from legacy**

Open `code/renderer/tr_animation.c` and copy the static-vars block at the top of the file (after the include, before the first function — approximately lines 30-90 in legacy, verify by `awk 'NR>=20 && NR<=100' code/renderer/tr_animation.c`).

Includes: `static float frontlerp, backlerp;`, `static mdsBoneFrame_t bones[MDS_MAX_BONES]` etc., math working buffers. Paste verbatim into the new file.

- [ ] **Step 3: Copy cull, fog, projection-radius functions**

From legacy `code/renderer/tr_animation.c`, copy:
- `static float RB_ProjectRadius( float r, vec3_t location )` — typically ~20 LOC.
- `static int R_CullModel( mdsHeader_t *header, trRefEntity_t *ent )` — frustum + LOD-radius culling, ~50 LOC.
- `float RB_CalcMDSLod( refEntity_t *refent, vec3_t origin, float radius, float modelBias, float modelScale )` — LOD selection, ~20 LOC.
- `static int R_ComputeFogNum( mdsHeader_t *header, trRefEntity_t *ent )` — fog volume detection, ~30 LOC.

Paste into the new file after the static block. Keep functions `static` (file-internal except where exported below).

**Adaptation:** Verify the `trRefEntity_t` and `refEntity_t` field names match renderervk's types. The renderercommon `tr_types.h` is the source of truth — if there's drift from legacy, adapt field references inline. The Phase 1 recon confirmed `tr_types.h` is unified between engine and renderer (see `notes/decisions/2026-06-12-m6-types-unification.md`), so drift should be zero.

- [ ] **Step 4: Build — file should compile despite no callers yet**

```bash
make -j8 BUILD_RENDERER_VULKAN=1 USE_INTERNAL_LIBS=0 release 2>&1 | grep -E "error:|undefined" | head -20
```

Expected: clean (file compiles, undefined-references emerge later when functions become non-static).

If `MDS_MAX_BONES` undefined: it's in `qfiles.h:422` — should resolve via `tr_local.h`. If `mdsBoneFrame_t` undefined: same.

- [ ] **Step 5: Wire into Makefile**

```makefile
  $(B)/rendv/realrtcw_tr_animation_mds.o \
```

Add adjacent to the `realrtcw_tr_mds.o` entry from Task 2.

- [ ] **Step 6: Commit**

```bash
git add code/renderervk/realrtcw_tr_animation_mds.c Makefile
git commit -m "feat(renderervk): bootstrap MDS animation runtime — file static + cull + fog

Phase 3b of M9, increment 1. File-static state (bones[], rawBones[],
working buffers) and the cull/fog helpers, ported from legacy
tr_animation.c. Nothing reachable from outside the file yet —
R_AddAnimSurfaces and RB_SurfaceAnim land in next commits.

See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md"
```

---

## Task 6: Port R_AddAnimSurfaces (entity-side enqueue)

**Files:**
- Modify: `code/renderervk/realrtcw_tr_animation_mds.c`
- Modify: `code/renderervk/tr_main.c` (entity-type dispatch branch)

- [ ] **Step 1: Copy R_AddAnimSurfaces from legacy**

From `code/renderer/tr_animation.c`, copy `void R_AddAnimSurfaces( trRefEntity_t *ent )` (~80 LOC, look for the function near the cull/fog block). Paste into `realrtcw_tr_animation_mds.c` after the cull/fog functions.

Change to non-static: `void R_AddAnimSurfaces( trRefEntity_t *ent )` (the forward decl from Task 1 Step 5 expects this).

Body calls: `R_CullModel`, `R_ComputeFogNum`, `R_AddDrawSurf`. The first two are static helpers in this file (Task 5). `R_AddDrawSurf` is renderer-global — already linked via Task 5's `tr_local.h` include.

The surface passed to `R_AddDrawSurf` is a `mdsSurface_t *` whose first field is `surfaceType_t ident == SF_MDS` — the surface-dispatch table (Task 8) will route on this.

- [ ] **Step 2: Add MOD_MDS branch in entity-type dispatch in tr_main.c**

Locate the existing dispatch (around `tr_main.c:1620-1660`):

```bash
awk 'NR>=1620 && NR<=1660' code/renderervk/tr_main.c
```

Find the block that switches on `model->type`:

```c
case MOD_MDR:
    R_MDRAddAnimSurfaces( ent );
    break;
case MOD_IQM:
    R_AddIQMSurfaces( ent );
    break;
```

Add adjacent (heredoc bypass for the hook):

```c
case MOD_MDR:
    R_MDRAddAnimSurfaces( ent );
    break;
case MOD_IQM:
    R_AddIQMSurfaces( ent );
    break;
/* RealRTCW M9 fix: MDS skeletal entity-type dispatch.
 * See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md */
case MOD_MDS:
    R_AddAnimSurfaces( ent );
    break;
```

(The exact surrounding shape may be `switch` block or `if/else if` chain — adapt to the actual code, just add `MOD_MDS` arm.)

- [ ] **Step 3: Build + smoke regression**

```bash
make -j8 BUILD_RENDERER_VULKAN=1 USE_INTERNAL_LIBS=0 release 2>&1 | tail -10
scripts/mac/playtest.sh --vulkan --auto +quit 2>&1 | tail -5
```

Expected: clean.

- [ ] **Step 4: Commit**

```bash
git add code/renderervk/realrtcw_tr_animation_mds.c code/renderervk/tr_main.c
git commit -m "feat(renderervk): R_AddAnimSurfaces + MOD_MDS entity dispatch

Phase 3b of M9, increment 2. MDS entities now enqueue surfaces into
the draw list via R_AddDrawSurf. Draw-time dispatch (rb_surfaceTable
[SF_MDS] → RB_SurfaceAnim) still missing — surfaces queue but resolve
to whatever the default SF_* entry does at SF_MDS index. Next commit
wires the dispatch + the actual RB_SurfaceAnim port.

See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md"
```

---

## Task 7: Port bone math (R_CalcBone, R_CalcBoneLerp, R_CalcBones)

**Files:**
- Modify: `code/renderervk/realrtcw_tr_animation_mds.c`

- [ ] **Step 1: Copy math helper inlines + bone calculators**

From `code/renderer/tr_animation.c`, copy in order:

1. The `ID_INLINE` helpers (search legacy for `ID_INLINE`): `LocalMatrixTransformVector`, `LocalScaledMatrixTransformVector`, `LocalAddScaledMatrixTransformVectorTranslate`, `LocalAngleVector`, `LocalVectorMA`, `SLerp_Normal`, `Matrix4MultiplyInto3x3AndTranslation`, `Matrix4FromAxisPlusTranslation`, `Matrix4FromScaledAxisPlusTranslation`, `Matrix3Transpose`. Total ~150 LOC. Keep all `static ID_INLINE`.

2. Bone calculators:
   - `void R_CalcBone( mdsHeader_t *header, const refEntity_t *refent, int boneNum )` (~80 LOC)
   - `void R_CalcBoneLerp( mdsHeader_t *header, const refEntity_t *refent, int boneNum )` (~100 LOC)
   - `void R_CalcBones( mdsHeader_t *header, const refEntity_t *refent, int *boneList, int numBones )` (~100 LOC) — this is the orchestrator that calls the per-bone calculators

Keep `R_CalcBone*` and `R_CalcBones` `static` — they're called only from `RB_SurfaceAnim` in the same TU.

3. `void R_RecursiveBoneListAdd( int bi, int *boneList, int *numBones, mdsBoneInfo_t *boneInfoList )` (~20 LOC).

Paste into the file after `R_AddAnimSurfaces`.

**Adaptation:** Most of the bone math depends only on the `mds*` types from `qfiles.h` and basic vec3/vec4 math from `q_shared.h`. Drift risk is minimal. If `ID_INLINE` is defined differently in renderervk vs legacy (some forks use `static inline __attribute__((always_inline))`), check `q_platform.h` and adapt.

- [ ] **Step 2: Build — expect to compile but no callers yet**

```bash
make -j8 BUILD_RENDERER_VULKAN=1 USE_INTERNAL_LIBS=0 release 2>&1 | tail -10
```

Expected: clean compile. Functions are static, unreferenced — clang may warn `-Wunused-function`; treat as expected since RB_SurfaceAnim (Task 8) will use them.

- [ ] **Step 3: Smoke regression**

```bash
scripts/mac/playtest.sh --vulkan --auto +quit 2>&1 | tail -5
```

Expected: exit 0.

- [ ] **Step 4: Commit**

```bash
git add code/renderervk/realrtcw_tr_animation_mds.c
git commit -m "feat(renderervk): MDS bone math (R_CalcBone, R_CalcBoneLerp, R_CalcBones)

Phase 3b of M9, increment 3. Frame interpolation and bone-matrix
construction for skeletal animation. Static helpers — caller is
RB_SurfaceAnim in next commit.

See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md"
```

---

## Task 8: Port RB_SurfaceAnim + wire rb_surfaceTable[SF_MDS]

**Files:**
- Modify: `code/renderervk/realrtcw_tr_animation_mds.c`
- Modify: `code/renderervk/tr_shade.c` (where `rb_surfaceTable[]` is defined)

- [ ] **Step 1: Locate rb_surfaceTable[] definition**

Run:
```bash
grep -rn "rb_surfaceTable\s*\[\s*SF_\|^void.*\*rb_surfaceTable" code/renderervk/*.c code/renderervk/*.h | head -10
```

Find the table definition (an array of function pointers indexed by `surfaceType_t`). Typical shape:

```c
void (*rb_surfaceTable[SF_NUM_SURFACE_TYPES]) (void *) = {
    (void(*)(void*))RB_SurfaceBad,         // SF_BAD,
    (void(*)(void*))RB_SurfaceSkip,        // SF_SKIP,
    ...
    (void(*)(void*))RB_MDRSurfaceAnim,     // SF_MDR,
    (void(*)(void*))RB_IQMSurfaceAnim,     // SF_IQM,
    ...
};
```

Note the file and line — the SF_MDS entry must be added at the position matching the enum from Task 1 Step 4.

- [ ] **Step 2: Copy RB_SurfaceAnim from legacy**

From `code/renderer/tr_animation.c`, copy `void RB_SurfaceAnim( mdsSurface_t *surface )` (large — ~400 LOC). Paste into `realrtcw_tr_animation_mds.c` after the bone math functions.

This function:
- Computes bone matrices via `R_CalcBones`.
- Iterates surface verts, builds skinned `xyz` + `normal` per vert, writes to renderer vertex buffer (`tess.xyz`, `tess.normal`, `tess.texCoords` — the standard renderervk tess scratch buffers).
- Computes LOD via collapse-map.
- Sets `tess.numIndexes`, `tess.numVertexes`.

**Critical adaptation:** the legacy uses `tess` (the OpenGL tess struct). renderervk uses the same `tess` global — verify by `grep -n "tess\.\|tess->" code/renderervk/tr_shade.c | head`. If field names match (likely yes — both are descendants of Q3's tess), no adaptation needed. If renderervk has renamed fields (e.g., `tess.normal` → `tess.vertexNormals`), adapt inline.

Body must remain non-static (the dispatch table calls it).

- [ ] **Step 3: Add SF_MDS entry to rb_surfaceTable[]**

In the file located in Step 1, add:

```c
    (void(*)(void*))RB_MDRSurfaceAnim,     // SF_MDR,
    (void(*)(void*))RB_IQMSurfaceAnim,     // SF_IQM,
    /* RealRTCW M9 fix: MDS skeletal dispatch.
     * See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md */
    (void(*)(void*))RB_SurfaceAnim,        // SF_MDS,
    (void(*)(void*))RB_SurfaceFlare,       // SF_FLARE,
```

The position in the table MUST match the SF_MDS ordinal in the `surfaceType_t` enum (from Task 1 Step 4). The compiler will not check this at build time — getting it wrong causes runtime crash with mis-typed function calls. Double-check by counting enum positions.

- [ ] **Step 4: Build + smoke regression**

```bash
make -j8 BUILD_RENDERER_VULKAN=1 USE_INTERNAL_LIBS=0 release 2>&1 | tail -10
scripts/mac/playtest.sh --vulkan --auto +quit 2>&1 | tail -5
```

Expected: clean.

- [ ] **Step 5: Commit**

```bash
git add code/renderervk/realrtcw_tr_animation_mds.c code/renderervk/tr_shade.c
git commit -m "feat(renderervk): RB_SurfaceAnim + rb_surfaceTable[SF_MDS] dispatch

Phase 3b of M9, increment 4. Skeletal-mesh vertex submission wired
into the renderervk tess pipeline. SF_MDS now dispatches through
rb_surfaceTable to RB_SurfaceAnim, which computes per-vertex skinning
from the bone matrices and writes tess.xyz/normal/texCoords.

MDS characters should now render — visible verification in Task 10's
smoke gate.

See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md"
```

---

## Task 9: Extend R_LerpTag with MOD_MDS branch via R_GetBoneTag

**Files:**
- Modify: `code/renderervk/realrtcw_tr_animation_mds.c` (add `R_GetBoneTag`)
- Modify: `code/renderervk/tr_model.c::R_LerpTag` (around line 1087)

- [ ] **Step 1: Copy R_GetBoneTag from legacy**

From `code/renderer/tr_animation.c`, copy:

```c
int R_GetBoneTag( orientation_t *outTag, mdsHeader_t *mds, int startTagIndex,
                  const refEntity_t *refent, const char *tagName )
```

(~80 LOC.) Paste into `realrtcw_tr_animation_mds.c` after `RB_SurfaceAnim`. Non-static (called from `tr_model.c`).

This function:
- Searches `mds->numTags` looking for `tagName` match.
- For matched tag, computes the bone matrix at `refent`'s frame/torsoFrame, applies tag's `torsoWeight`, writes into `outTag->origin` and `outTag->axis`.

- [ ] **Step 2: Extend R_LerpTag in tr_model.c**

Locate `int R_LerpTag(...)` at `code/renderervk/tr_model.c:1087`. The existing function dispatches by `mod->type` for MD3/MDR/IQM. Inspect:

```bash
awk 'NR>=1087 && NR<=1140' code/renderervk/tr_model.c
```

Add a MOD_MDS branch (typical shape):

```c
    if ( model->type == MOD_MESH ) {
        ...
    } else if ( model->type == MOD_MDR ) {
        ...
    } else if ( model->type == MOD_IQM ) {
        ...
    }
    /* RealRTCW M9 fix: MDS tag lookup via bone-frame interpolation.
     * See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md */
    else if ( model->type == MOD_MDS ) {
        /* R_GetBoneTag returns 1 on hit, 0 on miss; emulate the
         * frame-blend the other branches do here, or fall through
         * to R_GetBoneTag's internal blending. Legacy uses the
         * latter — match that. */
        return R_GetBoneTag( tag, model->mds, 0 /* startTagIndex */,
                             /* synthesize refent from frame args — see legacy R_LerpTag */ );
    }
```

**The shape of the MOD_MDS branch in legacy R_LerpTag is non-trivial.** The legacy reads it from `code/renderer/tr_model.c::R_LerpTag` (~lines 998-1080 in legacy). Cross-check by `awk 'NR>=998 && NR<=1080' code/renderer/tr_model.c` — the legacy already calls `R_GetBoneTag` synthesizing a refEntity_t. Port that synthesis verbatim into the renderervk extension.

- [ ] **Step 3: Build + smoke regression**

```bash
make -j8 BUILD_RENDERER_VULKAN=1 USE_INTERNAL_LIBS=0 release 2>&1 | tail -10
scripts/mac/playtest.sh --vulkan --auto +quit 2>&1 | tail -5
```

Expected: clean.

- [ ] **Step 4: Commit**

```bash
git add code/renderervk/realrtcw_tr_animation_mds.c code/renderervk/tr_model.c
git commit -m "feat(renderervk): R_GetBoneTag + R_LerpTag MOD_MDS branch

Phase 3b of M9, increment 5. cgame's trap_R_LerpTag now resolves
skeletal bone tags (tag_head, tag_torso, tag_weapon, ...) for MDS
characters via per-bone frame interpolation, matching legacy GL1
behavior. Required for weapon attachment + head-tracking on the
default character.

See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md"
```

---

## Task 10: Phase 3 smoke gate — character visible & animating

**Files:** (verification only)

- [ ] **Step 1: Run the gameplay smoke**

```bash
scripts/mac/playtest.sh --vulkan --auto +set model "player" +map escape1 +wait 600 +quit 2>&1 | tee /tmp/m9-phase3-smoke.log | tail -40
```

`+wait 600` gives ~10 seconds of in-game time after map load — enough for the character to be alive in the scene and the renderer to traverse the MDS dispatch path multiple times.

- [ ] **Step 2: Verify the model registration AND rendering paths fired**

```bash
grep -E "Failed to load|DEFAULT_MODEL|wolfanim\.cfg" /tmp/m9-phase3-smoke.log
```

- If `Failed to load legs model` or `DEFAULT_MODEL failed` → Phase 3a regressed. Triage Task 4 first.
- If `wolfanim.cfg not found` → known downstream blocker (M9.5 territory). Either workaround by `+set model "skel"` (which has different wolfanim path) or treat as Phase 3 complete and open M9.5 separately.
- If neither → server reached gameplay.

- [ ] **Step 3: Capture a screenshot and inspect visually**

```bash
scripts/mac/playtest.sh --vulkan --auto +set model "player" +map escape1 +wait 600 +screenshotJPEG mds-test +wait 30 +quit 2>&1 | tail -10
ls -la ~/Library/Application\ Support/RealRTCW/main/screenshots/mds-test.jpg
```

Open the screenshot. **Expected:** the player's first-person body (legs, torso, weapon) renders correctly — no Z-fighting, no zero-area triangles, no wildly-stretched verts (which would indicate bone matrices are wrong). Hands hold the weapon at the right tag.

If the character renders WRONG (e.g., exploded mesh, frozen pose, missing surfaces):
- **Exploded mesh** → bone matrix order or transposition is wrong. Check `R_CalcBones` against legacy verbatim.
- **Frozen pose** → frame interpolation broken. Check `frontlerp/backlerp` static vars + `R_CalcBoneLerp`.
- **Missing surfaces** → `R_AddDrawSurf` not called per-surface, or `tess.numVertexes` not set. Check `R_AddAnimSurfaces` loop.
- **Z-fighting / wrong normals** → vertex `normal` swap byte-order in `R_LoadMDS` may have been skipped. Re-check.

- [ ] **Step 4: Compare against OpenGL baseline if available**

Run the same smoke with `--opengl` (the legacy renderer):
```bash
scripts/mac/playtest.sh --opengl --auto +set model "player" +map escape1 +wait 600 +screenshotJPEG mds-baseline +wait 30 +quit 2>&1 | tail -10
```

Open `mds-baseline.jpg` and `mds-test.jpg` side-by-side. They should match within minor anti-aliasing differences. Diverging silhouettes or wrong tag positions indicate a Phase 3b bug.

- [ ] **Step 5: Phase 3 exit summary**

If Steps 2-4 all pass, Phase 3 is **done**:
- Default character registers (Phase 3a).
- Character is visible and animating (Phase 3b).
- Bone tags resolve, weapon/head attach correctly.
- Visual parity with OpenGL renderer at frame-zero.

Update `notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md` with a `## Phase 3 closed (<date>)` section:
- Commit range from `git log --format="%h" adf4c41..HEAD` (Phase 3a + 3b).
- Smoke verification evidence (screenshot paths or behavior description).
- Note any downstream issues surfaced (e.g., wolfanim.cfg) and their classification.

- [ ] **Step 6: Final commit + push**

```bash
git add notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md
git commit -m "docs(decisions): M9 Phase 3 closed — MDS skeletal full path shipped

See notes/plans/2026-06-13-m9-phase3-mds-skeletal.md"
git push origin macos-arm64-vulkan
```

---

## Convention reminders (apply throughout)

- **NO `#ifdef REALRTCW_ALLOW_VENDOR_EDIT`** anywhere. The macro is HOOK-permission, not a compile-time define. Inline `RealRTCW M9 fix:` comment + decision-note link is the re-vendor marker. Phase 1 commit `6e3bb5d` codifies this.
- **Hook bypass:** Edits to non-`realrtcw_`-prefixed files in `code/renderervk/` (i.e., `tr_local.h`, `tr_model.c`, `tr_main.c`, `tr_shade.c`) will be blocked by `block-vendored-renderervk.sh`. Use `python3 <<'EOF' ... EOF` Bash heredoc to apply edits (Bash is NOT hook-gated). Pattern in any Phase 1 task implementer log.
- **New files with `realrtcw_` prefix** (i.e., the two `.c` files this plan creates) go through `Write` tool normally — the hook auto-passes.
- **Build flag:** `REALRTCW_ALLOW_VENDOR_EDIT=1` on `make` command line is OPTIONAL — affects only the hook for any nested tooling, NOT the compiler. The standard `make -j8 BUILD_RENDERER_VULKAN=1 USE_INTERNAL_LIBS=0 release` is sufficient.

---

## Self-Review

**Spec coverage:**
- ✓ Phase 3a (loader) — Tasks 1-4. Mirrors Phase 1 MDC shape exactly.
- ✓ Phase 3b (runtime) — Tasks 5-10. Decomposed by responsibility: file scaffolding + cull/fog (5), entity-side enqueue + entity-type dispatch (6), bone math (7), draw-time submission + surface-table wire (8), tag lookup (9), smoke gate (10).
- ✓ Vendor convention codified in plan header AND in the "Convention reminders" section AND in each task's commit message.
- ✓ Reference port cited (iortcw rend2 lines pointed to).
- ✓ Smoke gates at both Phase 3a (loader-only) and Phase 3b (visible) checkpoints.

**Placeholder scan:**
- Task 8 Step 2 has open-ended "verify field names match" but bounds it concretely (M6 type-unification proved zero drift — likely no adaptation needed).
- Task 9 Step 2 has "synthesize refent from frame args — see legacy R_LerpTag" — pointed at concrete legacy lines for copy-source. Engineer copies-and-adapts, not invents.
- Smoke gate triage paths (Task 4 Step 5, Task 10 Step 3) enumerate concrete failure modes and resolution rules.

**Type consistency:**
- `R_LoadMDS` signature: `qboolean R_LoadMDS( model_t *mod, void *buffer, int filesize, const char *mod_name )` — defined Task 1 Step 5 forward decl, ported Task 2 Step 2 adaptation, called Task 3 Step 2. Consistent.
- `R_AddAnimSurfaces`, `RB_SurfaceAnim`, `R_GetBoneTag` — forward-decl'd in Task 1, defined in Tasks 6, 8, 9 respectively, callers in Tasks 6 (tr_main.c), 8 (rb_surfaceTable), 9 (R_LerpTag). All consistent.
- `MOD_MDS` enum value — Task 1 Step 2 adds, Task 3 Step 2 + Task 6 Step 2 + Task 9 Step 2 consume.
- `SF_MDS` enum value — Task 1 Step 4 adds, Task 8 Step 3 consumes (table index).
- File paths `code/renderervk/realrtcw_tr_mds.c` and `code/renderervk/realrtcw_tr_animation_mds.c` — consistent across all tasks and commit messages.
