# M9 Phase 1: MDC loader + .md3→.mdc fallback — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unblock Vulkan map load on RealRTCW by vendor-porting the MDC mesh loader and the `.md3→.mdc` filename-fallback into `code/renderervk/`, so `RE_RegisterModel("models/players/player/lower.md3")` returns a non-zero handle instead of failing with `DEFAULT_MODEL`.

**Architecture:** Vendor port option γ from `notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md`. Single source of truth lives in `code/renderervk/` under `REALRTCW_ALLOW_VENDOR_EDIT` guard (mirrors M5.10 precedent in `notes/decisions/2026-06-12-m5-10-hunk-temp-overflow-workaround.md`). Phase 1 ships **loader-only** — MDC files register cleanly, but rendering them is Phase 2 (any MDC model will be invisible until then). Phase 1 exit criterion is server-side: server no longer crashes on map load because `DEFAULT_MODEL` resolves. Phase 3 covers MDS skeletal (separate plan, separate model class). Reference port for code-shape: `~/fedorov_tech/refs/iortcw/SP/code/rend2/tr_model.c`.

**Tech Stack:** C99, RealRTCW engine (RTCW SP fork), `code/renderervk/` (Quake3e-lineage Vulkan renderer), Apple Silicon macOS arm64 build via `Makefile` with `BUILD_RENDERER_VULKAN=1 USE_INTERNAL_LIBS=0 REALRTCW_ALLOW_VENDOR_EDIT=1`. Smoke test harness: `scripts/mac/playtest.sh --vulkan --auto +<cmd>`.

---

## File Structure

**Existing (read-only references during port):**
- `code/qcommon/qfiles.h:200-282` — `MDC_IDENT`, `mdcHeader_t`, `mdcSurface_t`, `mdcTriangle_t`, `mdcSt_t`, `mdcXyzCompressed_t`, `mdcTag_t`, `mdcTagName_t` (already engine-shared, accessible to any renderer — no vendor work needed for binary-format types).
- `code/renderer/tr_model.c:51-170` — legacy `R_RegisterMD3` with `.md3→.mdc` fallback (reference for fallback shape).
- `code/renderer/tr_model.c:868-1080` — legacy `R_LoadMDC` (~210 LOC, the function to vendor-port).
- `code/renderer/tr_local.h:958-973` — legacy `MOD_MDC` enum value + `mdc[MD3_MAX_LODS]` slot in `model_t` (reference for type-layout vendor edit).
- `~/fedorov_tech/refs/iortcw/SP/code/rend2/tr_model.c:564-900` — iortcw rend2 reference port (closest precedent for «MDC in a non-GL1 backend»). Diff against this when renderervk's surface API forces deviation from the legacy shape.

**Create (RealRTCW-authored, vendor-prefix convention):**
- `code/renderervk/realrtcw_tr_mdc.c` — vendor-ported `R_LoadMDC`. Guarded by `#ifdef REALRTCW_ALLOW_VENDOR_EDIT` so an upstream re-vendor diff omits this file cleanly. New file gets the `realrtcw_` prefix per `notes/decisions/2026-06-08-vendor-prefix-convention.md`.

**Modify (under `REALRTCW_ALLOW_VENDOR_EDIT` guard with inline comment cross-referencing this decision-note):**
- `code/renderervk/tr_local.h` — add `MOD_MDC` enum value and `mdcHeader_t *mdc[MD3_MAX_LODS]` slot in `model_t`. Forward-declare `R_LoadMDC`.
- `code/renderervk/tr_model.c` — extend `R_RegisterMD3` loader loop with the `.md3→.mdc` filename-fallback. Dispatch by ident: `MD3_IDENT → R_LoadMD3` (existing), `MDC_IDENT → R_LoadMDC` (new). Mirror legacy lines 85-102 + 104-124, simplified to «.md3 first, .mdc fallback» (no `r_compressModels` cvar — see Task 4 rationale).
- `Makefile` — add `realrtcw_tr_mdc.c` to the Vulkan renderer object list (`RVKOBJ` or equivalent — engineer locates by following the M7 precedent at the prior commit that added `realrtcw_log2pad_shim.h` adjacents).

---

## Task 1: Add MDC slot to renderervk model_t

**Files:**
- Modify: `code/renderervk/tr_local.h` (model_t struct + modtype_t enum + forward decl)

- [ ] **Step 1: Locate the modtype_t enum in renderervk tr_local.h**

Run: `grep -n -E "MOD_(BAD|MESH|MDR|IQM|BRUSH)" code/renderervk/tr_local.h`

Expected: a block like
```
typedef enum {
    MOD_BAD,
    MOD_BRUSH,
    MOD_MESH,
    MOD_MDR,
    MOD_IQM,
} modtype_t;
```
Note the exact line numbers — Step 3 needs them.

- [ ] **Step 2: Locate the model_t struct in renderervk tr_local.h**

Run: `grep -n -A 15 "typedef struct model_s" code/renderervk/tr_local.h`

Expected: struct body showing fields like `name`, `type`, `index`, `dataSize`, `numLods`, `md3[MD3_MAX_LODS]`, `modelData` (for MDR/IQM). Note line numbers — Step 4 needs them.

- [ ] **Step 3: Add MOD_MDC to modtype_t**

Edit the enum to add `MOD_MDC` after `MOD_IQM`, guarded:

```c
typedef enum {
    MOD_BAD,
    MOD_BRUSH,
    MOD_MESH,
    MOD_MDR,
    MOD_IQM,
#ifdef REALRTCW_ALLOW_VENDOR_EDIT
    /* RealRTCW M9 fix: MDC compressed mesh support.
     * See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md */
    MOD_MDC,
#endif
} modtype_t;
```

- [ ] **Step 4: Add mdc[] slot to model_t**

Inside the model_t struct, after the existing `md3[MD3_MAX_LODS]` field, add:

```c
#ifdef REALRTCW_ALLOW_VENDOR_EDIT
    /* RealRTCW M9 fix: MDC compressed mesh slot, parallel to md3[].
     * See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md */
    mdcHeader_t *mdc[MD3_MAX_LODS];
#endif
```

- [ ] **Step 5: Add forward declaration of R_LoadMDC**

Near the bottom of `tr_local.h`, alongside other `R_Load*` forward decls (search for `R_LoadMDR` to find the right block), add:

```c
#ifdef REALRTCW_ALLOW_VENDOR_EDIT
qboolean R_LoadMDC( model_t *mod, int lod, void *buffer, int fileSize, const char *mod_name );
#endif
```

- [ ] **Step 6: Verify the build still compiles (no MDC loader yet — just types)**

Run: `make -j8 BUILD_RENDERER_VULKAN=1 USE_INTERNAL_LIBS=0 REALRTCW_ALLOW_VENDOR_EDIT=1 release 2>&1 | tail -30`

Expected: build SUCCEEDS. The `R_LoadMDC` forward decl is unreferenced (no caller yet), which is fine. `mdcHeader_t` resolves via `qfiles.h` inclusion chain (already pulled in by `tr_local.h`).

If the build fails on `mdcHeader_t undefined`, add `#include "../qcommon/qfiles.h"` near the top of `tr_local.h` under the same guard, then rebuild.

- [ ] **Step 7: Commit**

```bash
git add code/renderervk/tr_local.h
git commit -m "feat(renderervk): add MOD_MDC enum + model_t mdc[] slot under vendor-edit guard

Pre-work for M9 Phase 1 (MDC loader port). Types only — no loader, no dispatch.
Forward-decl R_LoadMDC so future commits stay buildable in any partial state.

See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md"
```

---

## Task 2: Vendor-port R_LoadMDC into realrtcw_tr_mdc.c

**Files:**
- Create: `code/renderervk/realrtcw_tr_mdc.c` (~220 LOC after wrapping)
- Read-only reference: `code/renderer/tr_model.c:868-1080`

- [ ] **Step 1: Create the new file with the standard RealRTCW vendor header**

Create `code/renderervk/realrtcw_tr_mdc.c` with this exact opening:

```c
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

```

- [ ] **Step 2: Copy R_LoadMDC body from legacy source**

Open `code/renderer/tr_model.c` and copy lines **868-1080** (the `R_LoadMDC` function body — verify by `grep -n "R_LoadMDC\|^}" code/renderer/tr_model.c | head -10` to confirm the closing brace position). Paste it into the new file after the `#define LL` line.

**Important adaptations on paste:**
1. Change `static qboolean R_LoadMDC(...)` to `qboolean R_LoadMDC(...)` (non-static — dispatcher in `tr_model.c` will call across translation units).
2. Add a `fileSize` parameter: `qboolean R_LoadMDC( model_t *mod, int lod, void *buffer, int fileSize, const char *mod_name )`. The legacy function has no fileSize check (it's older code); renderervk's R_LoadMD3 does. Match the renderervk signature shape so the dispatcher in Task 3 can pass fileSize uniformly. If the body doesn't use fileSize, that's fine — the parameter is for future-proofing the bounds checks against truncated PAK reads. (Cross-check `R_LoadMD3` in `code/renderervk/tr_model.c:28` for the exact signature.)
3. Keep all `ri.` calls as-is. Both renderers share the refImport_t shape.
4. The legacy uses `ri.Hunk_Alloc( size, h_low )` — keep verbatim, renderervk has same `Hunk_Alloc` semantics (verified in M5.10).

- [ ] **Step 3: Close the vendor-edit guard**

Append at end of file:

```c

#endif /* REALRTCW_ALLOW_VENDOR_EDIT */
```

- [ ] **Step 4: Wire the new file into the Vulkan-renderer object list**

Find the renderervk object-list block in `Makefile`. Run: `grep -n "renderervk/tr_model\|RVKOBJ\|RV_OBJ\|RVOBJ" Makefile | head -20`

Identify the variable that collects renderervk .o files (likely `Q3R2OBJ`, `RVKOBJ`, or named per the M7 precedent). Add a line for `realrtcw_tr_mdc.o` matching the existing pattern, e.g.:

```makefile
  $(B)/renderervk/realrtcw_tr_mdc.o \
```

- [ ] **Step 5: Build, expect failure on missing types/symbols**

Run: `make -j8 BUILD_RENDERER_VULKAN=1 USE_INTERNAL_LIBS=0 REALRTCW_ALLOW_VENDOR_EDIT=1 release 2>&1 | grep -E "error:|undefined" | head -30`

Expected: errors about missing identifiers that the legacy loader references but renderervk doesn't have. Most likely candidates (catalog as you find them — there may be others):
- `SF_MDC` surfaceType constant
- `MDC_VERSION` (check if in `qfiles.h` already)
- Possibly `R_MDC_GetVec` / `R_MDC_DecodeXyzCompressed` helpers if they're called from inside `R_LoadMDC`

For each missing symbol, resolve as follows:
- **Format constants (`MDC_VERSION`):** Check `qfiles.h`. If present, no action needed. If absent, add under `REALRTCW_ALLOW_VENDOR_EDIT` in `tr_local.h` with the legacy value.
- **Surface-type tags (`SF_MDC`):** Locate the `surfaceType_t` enum in renderervk `tr_local.h` (`grep -n "SF_MD3\|surfaceType_t" code/renderervk/tr_local.h`). Add `SF_MDC` under a guard alongside `SF_MD3`. Phase 2 (runtime) will use it; Phase 1 only needs the value to exist so the loader compiles.
- **Decompression helpers (`R_MDC_GetVec` etc.):** Vendor-port the helpers into `realrtcw_tr_mdc.c` alongside `R_LoadMDC` (they live near it in legacy `tr_model.c` — check lines 800-870). Keep them `static` since they're only used inside the file.

Iterate until the build succeeds. Each resolution is a small inline edit, not a redesign.

- [ ] **Step 6: Build clean**

Run: `make -j8 BUILD_RENDERER_VULKAN=1 USE_INTERNAL_LIBS=0 REALRTCW_ALLOW_VENDOR_EDIT=1 release 2>&1 | tail -10`

Expected: exit 0, binary updated. `R_LoadMDC` is now compiled but still unreferenced (no dispatcher entry yet — Task 3).

- [ ] **Step 7: Smoke verify nothing regressed**

Run: `scripts/mac/playtest.sh --vulkan --auto +quit 2>&1 | tail -20`

Expected: smoke passes (same exit-clean behavior as before Phase 1 started). The main menu still loads, no MDC failures because no map is loaded.

- [ ] **Step 8: Commit**

```bash
git add code/renderervk/realrtcw_tr_mdc.c code/renderervk/tr_local.h Makefile
git commit -m "feat(renderervk): vendor-port R_LoadMDC into realrtcw_tr_mdc.c

Phase 1 of M9. Loader compiles but is unreferenced — dispatcher entry
lands in next commit. Smoke unchanged (no map load yet).

See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md"
```

---

## Task 3: Wire .md3→.mdc fallback + MDC dispatch in R_RegisterMD3

**Files:**
- Modify: `code/renderervk/tr_model.c::R_RegisterMD3` (lines ~36-93)

- [ ] **Step 1: Re-read the current R_RegisterMD3 in renderervk**

Run: `awk 'NR>=36 && NR<=93' code/renderervk/tr_model.c`

Confirm the shape matches what's expected (single-extension load loop, dispatch on `MD3_IDENT`).

- [ ] **Step 2: Replace the per-LOD load block with fallback-aware version**

Inside the `for ( lod = 0 ; lod < MD3_MAX_LODS ; lod++ )` loop, replace the block from `if ( lod )` down to (but not including) the `if ( loaded )` block. New block:

```c
		if ( lod )
			Com_sprintf(namebuf, sizeof(namebuf), "%s_%d.%s", filename, lod, fext);
		else
			Com_sprintf(namebuf, sizeof(namebuf), "%s.%s", filename, fext);

		fileSize = ri.FS_ReadFile( namebuf, &buf.v );

		/* RealRTCW M9 fix: .md3 → .mdc fallback.
		 * RTCW ships compressed meshes as .mdc with the same base name.
		 * If the .md3 read failed and the extension is .md3, retry with
		 * the last char swapped to 'c'. Dispatcher below picks loader
		 * by ident, so this works for any caller using the canonical
		 * .md3 filename. See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md */
		if ( !buf.v && Q_stricmp( fext, "md3" ) == 0 ) {
			size_t nlen = strlen( namebuf );
			if ( nlen > 0 && namebuf[nlen - 1] == '3' ) {
				namebuf[nlen - 1] = 'c';
				fileSize = ri.FS_ReadFile( namebuf, &buf.v );
			}
		}

		if ( !buf.v )
			continue;

		if ( fileSize < sizeof( md3Header_t ) ) {
			ri.Printf( PRINT_WARNING, "%s: truncated header for %s\n", __func__, name );
			ri.FS_FreeFile( buf.v );
			break;
		}

		ident = LittleLong( *buf.u );
		if ( ident == MD3_IDENT ) {
			loaded = R_LoadMD3( mod, mod->numLods, buf.v, fileSize, name );
		}
		else if ( ident == MDC_IDENT ) {
			/* RealRTCW M9 fix: dispatch MDC compressed meshes to vendor-ported loader.
			 * See realrtcw_tr_mdc.c. */
			loaded = R_LoadMDC( mod, mod->numLods, buf.v, fileSize, name );
			if ( loaded ) {
				mod->type = MOD_MDC;
			}
		}
		else {
			ri.Printf( PRINT_WARNING, "%s: unknown fileid for %s\n", __func__, name );
			loaded = qfalse;
		}
```

**Convention note (codified 2026-06-13 mid-Phase 1):** Vendor edits in renderervk are tracked via inline `RealRTCW M9 fix:` comment marker + decision-note link. **Do NOT wrap them in `#ifdef REALRTCW_ALLOW_VENDOR_EDIT`** — that macro gates only the file-edit hook, not the compiler (Makefile passes no `-D` for it). Wrapping makes the code dead. Matches M5.10 in-tree precedent at `tr_image.c:633`. See commit `6e3bb5d`.

**Why the explicit `mod->type = MOD_MDC` write:** `R_LoadMDC` (legacy) sets `mod->type = MOD_MDC` internally, but we don't trust that — being explicit here matches the dispatcher's intent and survives future loader refactors. (`R_LoadMD3` already sets `MOD_MESH` internally; legacy parity.)

- [ ] **Step 3: Build, expect clean**

Run: `make -j8 BUILD_RENDERER_VULKAN=1 USE_INTERNAL_LIBS=0 REALRTCW_ALLOW_VENDOR_EDIT=1 release 2>&1 | tail -10`

Expected: exit 0. If `Q_stricmp` or `size_t` undefined, add the appropriate include at file top (`q_shared.h` is usually already pulled via `tr_local.h` — should not happen).

- [ ] **Step 4: Smoke — verify menu still loads (no regression)**

Run: `scripts/mac/playtest.sh --vulkan --auto +quit 2>&1 | tail -20`

Expected: clean exit 0, main menu draws (as in M-screenshot smoke). No new warnings about model loads — the fallback only activates when `.md3` read fails.

- [ ] **Step 5: Commit**

```bash
git add code/renderervk/tr_model.c
git commit -m "feat(renderervk): wire .md3→.mdc fallback and MDC ident dispatch

Phase 1 of M9. R_RegisterMD3 now retries with .mdc extension when the
.md3 read fails, then dispatches by file ident (MD3_IDENT → R_LoadMD3,
MDC_IDENT → R_LoadMDC). Resolves the gap where cgame canonically
requests 'lower.md3' but RTCW ships 'lower.mdc' in PAKs.

See notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md"
```

---

## Task 4: Smoke gate — verify lower.md3 registers on Vulkan map load

**Files:** (verification only)

- [ ] **Step 1: Run the map-load smoke**

Run: `scripts/mac/playtest.sh --vulkan --auto +map escape1 +wait 200 +quit 2>&1 | tee /tmp/m9-phase1-smoke.log | tail -50`

(Note: `+map escape1` enters a briefing screen `--auto` can't dismiss. That's fine for Phase 1 — what we're verifying is that the server-side model-registration during `+map` completes without `DEFAULT_MODEL failed`. The briefing screen failing to dismiss is unrelated and tracked separately.)

- [ ] **Step 2: Verify the PRE-Phase-1 error markers are GONE**

Run: `grep -E "DEFAULT_MODEL.*failed|Failed to load legs model|Server crashed" /tmp/m9-phase1-smoke.log`

Expected: **empty output** (no matches). Before Phase 1 these would dominate the tail of the log.

- [ ] **Step 3: Verify MDC loader was actually invoked**

Run: `grep -E "R_LoadMDC|player/lower" /tmp/m9-phase1-smoke.log | head -20`

Expected: NO `R_LoadMDC:` warnings (those only print on errors), AND no «couldn't load» / «unknown fileid» for `player/lower.*`. Silence is success here — the loader returned `qtrue` and the model registered.

To get positive confirmation that the fallback path fired, add a one-time printf instrumented after the fallback in Task 3 Step 2 — only do this if you want positive evidence. NOT REQUIRED for the smoke gate, but cheap. If you do add it:

```c
#ifdef REALRTCW_ALLOW_VENDOR_EDIT
ri.Printf( PRINT_DEVELOPER, "RealRTCW M9: .md3→.mdc fallback hit for %s\n", namebuf );
#endif
```

Then re-grep: `grep "M9: .md3" /tmp/m9-phase1-smoke.log` — expect at least one hit for `player/lower.mdc`.

Remove the printf before committing — it's developer-only noise.

- [ ] **Step 4: Verify the server reached the next stage**

Run: `grep -E "AAS_LoadFiles|gametype:|spawning|Loaded entity" /tmp/m9-phase1-smoke.log | head -10`

Expected: at least one of these markers present. They indicate the server got past the model-registration wall and is entering actual map-load steps (AAS, entity spawn, gametype init). Confirms Phase 1 unblocked the critical path.

- [ ] **Step 5: Phase 1 exit summary**

If Steps 2-4 all pass, Phase 1 is **done**:
- `lower.md3` resolves to `lower.mdc` and registers via `R_LoadMDC`.
- Server no longer crashes on map load.
- MDC entities will be invisible (no Phase 2 runtime yet) but won't fault.

If any step fails, triage by:
- Step 2 still has `DEFAULT_MODEL failed` → the dispatcher in Task 3 isn't reaching `R_LoadMDC` for some reason. Check `mod->type` assignment, dispatcher placement (inside vs outside `for` loop), and that `MDC_IDENT` literal matches what's in the file (`xxd models/players/player/lower.mdc | head -1` should start with `IDPC` little-endian).
- Step 4 reaches AAS_LoadFiles then crashes elsewhere → MDC port is OK; you've hit a different downstream issue (likely Phase 2 or a separate landmine). Open a new finding note; Phase 1 is still done.

- [ ] **Step 6: Final commit — close Phase 1**

If a developer-only printf was added in Step 3, remove it now. Then:

```bash
git status  # expect clean unless the printf was added
git log --oneline -5
git push origin macos-arm64-vulkan
```

Update `notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md` to add a `## Phase 1 closed (<date>)` section noting:
- Commit range (`git log --format="%h" main..HEAD | head -10`).
- The smoke gate that passed.
- Next-tier failures observed in Step 4's downstream log tail (this becomes the catalog input for Phase 2 / Phase 3 planning).

Commit the decision-note update:

```bash
git add notes/decisions/2026-06-13-m9-mds-mdc-loader-gap.md
git commit -m "docs(decisions): M9 Phase 1 closed — MDC loader + fallback shipped

See notes/plans/2026-06-13-m9-phase1-mdc-loader-and-fallback.md"
git push origin macos-arm64-vulkan
```

---

## Follow-on phases (not in this plan)

Phases 2 and 3 each get their own plan document, written after Phase 1 lands and we have concrete observations from the Step 4 downstream log. Sketch only — do not implement from this section.

**Phase 2: MDC runtime (rendering)** — vendor-port `code/renderer/tr_cmesh.c` (~450 LOC) into `code/renderervk/realrtcw_tr_cmesh.c`. Wire `SF_MDC` into the renderervk surface-dispatch switch in `tr_surface.c` / `tr_shade.c`. Adapt vertex submission to renderervk's vertex-cache / VBO shape. Reference: iortcw `rend2/` MDC integration. Exit criterion: previously-invisible MDC entities now visible in correct pose on `escape1`. **Est: 1 day if rend2's reference port maps cleanly; up to 1.5 if vertex-cache API forces deviation.**

**Phase 3: MDS skeletal (loader + runtime)** — vendor-port `R_LoadMDS` (~660 LOC) and the MDS half of `code/renderer/tr_animation.c` (~1360 LOC) into `realrtcw_tr_mds.c` and `realrtcw_tr_mds_anim.c`. Add `MOD_MDS` to modtype_t, `SF_MDS` to surfaceType_t. Surface the bone-tag query API (`R_GetBoneTag`) to game-side via `refImport_t` if cgame needs it (check legacy `cl_main.c` / `cg_main.c` for current call sites). Reference: iortcw `rend2/` MDS port. Exit criterion: character animates on `escape1`. **Est: 1-2 days.**

---

## Self-Review (writing-plans checklist)

**Spec coverage:**
- ✓ Decision note option γ (vendor port under `REALRTCW_ALLOW_VENDOR_EDIT`) — Tasks 1-3.
- ✓ M5.10 precedent shape (guard + inline comment + cross-ref to decision-note) — every guarded block has the comment.
- ✓ Reference port (iortcw rend2) — cited in plan header AND in Task 2 as code-shape reference.
- ✓ Vendor-prefix convention for new files (`realrtcw_tr_mdc.c`) — Task 2 Step 1.
- ✓ Smoke gate per the decision-note recon item 4 — Task 4 verifies the symptom from the decision-note (`DEFAULT_MODEL failed`) is gone.
- ✓ 3-phase shape per the user request — Phase 1 in full, Phases 2/3 sketched as follow-on plans.

**Placeholder scan:**
- No «TBD» / «implement later» / generic «add error handling» language.
- Task 2 Step 5 has open-ended «iterate until build succeeds» — but it's explicitly bounded with a catalog of likely missing symbols and per-symbol resolution rules. Acceptable: vendor-port iteration is inherently discovery-driven and a fully enumerated list is impossible without doing the port. The rules cover all anticipated cases.
- Task 4 Step 3 has an optional dev printf — explicitly marked optional and removable, with cleanup step.

**Type consistency:**
- `R_LoadMDC` signature: `qboolean R_LoadMDC( model_t *mod, int lod, void *buffer, int fileSize, const char *mod_name )` — defined Task 1 Step 5, used Task 2 Step 2 adaptation note, called Task 3 Step 2 (`R_LoadMDC( mod, mod->numLods, buf.v, fileSize, name )`). Consistent.
- `MOD_MDC`: enum defined Task 1 Step 3, assigned Task 3 Step 2. Consistent.
- `MDC_IDENT`: source is `qcommon/qfiles.h` (verified existing); compared Task 3 Step 2.
- File path `code/renderervk/realrtcw_tr_mdc.c` — same name in Tasks 2, Step 4 (Makefile add), and decision-note `## Files touched` row that the engineer will write at Task 4 Step 6.
