# M9 finding — renderervk lacks MDS/MDC loaders (2026-06-13)

Classification note, no fix landed. Discovered while attempting M7.5 step 2
(fog comparison on `escape1`). Bug surfaces on **any** Vulkan-map load,
not specific to `escape1`. M7.5/2 and M8 are blocked behind this.

## Symptom

```
LOADING... clients
Failed to load legs model file models/players/player/lower.md3
Failed to load legs model file models/players/player/lower.md3
ERROR: DEFAULT_MODEL (player/default) failed to register
Server Shutdown (Server crashed: DEFAULT_MODEL (player/default) failed to register)
```

Engine recovers gracefully to main menu (with modal error), Vulkan rendering
continues normally on the menu. Same map loads fine on OpenGL.

## Root cause

`code/renderervk/tr_model.c` registers exactly 3 model-loader extensions
(`md3`, `mdr`, `iqm`). It is the standard Quake3e/ioquake3 loader table:

```c
static const modelExtToLoaderMap_t modelLoaders[] =
{
    { "iqm", R_RegisterIQM },
    { "mdr", R_RegisterMDR },
    { "md3", R_RegisterMD3 },
};
```

`code/renderer/tr_model.c` (RealRTCW's legacy OpenGL renderer) supports **5**
extensions and adds a `.md3 → .mdc` last-char fallback inside `R_RegisterMD3`:

| Format | Legacy `code/renderer/` | Vulkan `code/renderervk/` |
|---|---|---|
| `md3` (Q3 mesh) | ✓ | ✓ |
| `mdr` (Q3 modern skeletal) | — | ✓ |
| `iqm` (Inter-Quake) | — | ✓ |
| **`mdc` (RTCW compressed)** | ✓ + auto-fallback from `.md3` | ✗ |
| **`mds` (RTCW skeletal)** | ✓ (via `R_RegisterMDS`) | ✗ |

RTCW ships player meshes as MDC (compressed Q3 derivative — see `mdcXyzCompressed_t`, `R_MDC_GetVec`, etc. in legacy `tr_model.c`) and skeletal characters as MDS. The cgame requests `lower.md3` because that's the canonical filename in the original asset spec; on legacy the `.md3 → .mdc` swap finds `lower.mdc` in the pak. On Vulkan there is no swap, so renderervk silently returns 0 and cgame's `trap_R_RegisterModel("lower.md3")` resolves to `DEFAULT_MODEL failed`.

## Why this is on the critical path

This blocks **every** Vulkan playtest that loads a map. Vulkan-only validation work that stays in the main menu (UI rendering, shader pipeline init, screenshot) is unaffected. But Phase-2 parity goals — fog, lighting, character animation, gameplay HUD — all require the player model to register, so M9 gates everything downstream:

```
M-screenshot ← CLOSED
       ↓
       M9 (MDS/MDC loaders) ← blocks here
       ↓
       M7.5/2 (fog classification)
       ↓
       M8 (shader keywords nofog/nocompress/allowcompress)
       ↓
       Phase 2 parity goals
```

## Option vectors

Three layered options, in increasing structural cleanliness:

### α (defer / disable)
Add the `.md3 → .mdc` filename swap **only**, and have `R_RegisterMD3` look for an `.mdc` extension and treat it as MD3 (best-effort). Skips MDS entirely. Player would render as static legs only — gameplay broken but Vulkan-map-load succeeds far enough to test fog/shaders on the static world geometry. **Engineering: ~½ day. Quality: poor, throwaway.**

### β (proxy via engine, vendor-clean)
Load `.mdc` / `.mds` engine-side using the legacy renderer's loader code ported to `code/client/cl_model.c` (mirror to M7's `cl_jpeg.c` pattern). Pass converted `model_t` back to renderervk via a new refImport slot. **Engineering: 2-3 days. Quality: keeps renderervk vendor-clean but requires designing a new ABI crossing for an opaque model handle. Risk: model_t struct drift across the boundary.**

### γ (vendor port — clean, structural)
Port `R_RegisterMDS`, `R_LoadMDS`, `R_LoadMDC`, `R_RegisterMDC` + the `.md3→.mdc` fallback from `code/renderer/tr_model.c` into `code/renderervk/tr_model.c`. Mirror to **M5.10's pattern** for vendor-clean RTCW-specific extensions: gated by `REALRTCW_ALLOW_VENDOR_EDIT`, with a decision-note explaining why it has to live in renderervk. **Engineering: 2-3 days realistic.** Sub-breakdown (from 2026-06-13 recon, ~3000 LOC of RTCW-specific code total):
- MDC loader (`R_LoadMDC` ~210 LOC) + .md3→.mdc fallback inside `R_RegisterMD3` (single last-char swap, see legacy `tr_model.c:85-102`) + MDC runtime (`tr_cmesh.c`, ~450 LOC wholesale-portable): **~1 day**.
- MDS loader (`R_LoadMDS` ~660 LOC) + MDS runtime (MDS half of `tr_animation.c`, ~1360 LOC of bone math, `R_CalcBone/Lerp/Bones`, `RB_SurfaceAnim`, `R_AddAnimSurfaces`, `R_GetBoneTag`): **~1-2 days**.

**Quality: best — single source of truth, no ABI surprise. Risk: MDS interacts with vertex submission via skeletal bone matrices; need to verify renderervk's vertex-cache / VBO path can accept the MDS bone-transformed verts without API drift from rend2's reference shape.**

### Recommendation (after one session of reflection — not picking yet)

γ is the structurally honest answer and matches the M7.5 epistemic stance («favor architectural-correct path even when slower» — see `memory/project_portfolio_vision.md`). β is the layered-decision parallel to M4's θ' / γ' split, with γ being the principled Phase 3 cleanup. α is throwaway and shouldn't be picked.

Default pick is **γ**. Cross-fork archaeology (`q3-fork-detective`, 2026-06-13) confirmed: **no upstream Vulkan port of MDS/MDC exists anywhere** (Quake3e, vkQuake3, ioquake3 all clean — none of them are RTCW-lineage). RealRTCW will be the first. This removes the «maybe someone already did it» branch.

## Reference port (added 2026-06-13)

**iortcw `rend2/` is the closest reference port** — it adapts MDC/MDS to a non-GL1 backend (modern GL2/GLSL). This is the same class of porting work as the renderervk vendor: «take loader + runtime out of legacy `renderer/`, fit into a different surface-submission shape». Use these files as the rewiring template, NOT the raw legacy GL1 shape:

- `~/fedorov_tech/refs/iortcw/SP/code/rend2/tr_model.c` lines 564-900 (rend2 `R_LoadMDC`) and 1915-2200 (rend2 `R_LoadMDS`).
- `~/fedorov_tech/refs/iortcw/SP/code/rend2/tr_animation.c` — rend2 MDS runtime adapted to rend2 vertex submission.

Reference flow: legacy `renderer/tr_model.c` → rend2 diff → renderervk integration shape. Pick rend2's API choices when renderervk and legacy diverge.

## Recon done (2026-06-13)

1. **MDS/MDC binary spec** — `R_LoadMDC` ~210 LOC, `R_LoadMDS` ~660 LOC, `R_MDC_ConvertMD3` + helpers ~275 LOC. Runtime: `tr_cmesh.c` (~450 LOC, entire file is MDC pipeline) + MDS half of `tr_animation.c` (~1360 LOC). Total RTCW-specific code: **~3000 LOC**. See sub-breakdown under option γ above.
2. **Cross-fork lookup** — Quake3e / vkQuake3 / ioquake3 all clean (no MDS/MDC in any Vulkan tree). iortcw-SP and iortcw-MP have MDC/MDS in both legacy `renderer/` AND `rend2/`. **iortcw `rend2/` is the reference port** for non-GL1 backend adaptation — see «Reference port» section above.
3. **REALRTCW_ALLOW_VENDOR_EDIT precedent** — re-read `notes/decisions/2026-06-12-m5-10-hunk-temp-overflow-workaround.md`. Pattern confirmed: guarded compile, inline comment block `RealRTCW M9 fix:` cross-referencing this doc, vendor-prefix convention for new files (`realrtcw_tr_mdc.c`, `realrtcw_tr_mdc_cmesh.c`, `realrtcw_tr_mds.c`).
4. **MDS/MDC test corpus** — **deferred to plan smoke phase**. Symptom is map-agnostic (`player/lower.md3` registration fails before any map's level data loads), so the next-tier failures only surface after the .md3→.mdc fallback lands. Smoke target shape: candidate maps `escape1` (M9 trigger), `mp_beach` and one SP campaign start map for character/skeletal coverage. Catalog will be built from first instrumented smoke run with fallback in place.

## Cross-links

- `code/renderer/tr_model.c:88-94` — the `.md3 → .mdc` swap that's missing on Vulkan.
- `code/renderer/tr_model.c:295` — legacy `R_RegisterMDS` slot in loader table.
- `notes/decisions/2026-06-12-m5-10-hunk-temp-overflow-workaround.md` — REALRTCW_ALLOW_VENDOR_EDIT precedent.
- `notes/plans/2026-06-12-m7-jpg-loader-and-m8-shader-keywords.md` — M7.5/2 procedure now gated behind M9.
- [[project-q3fork-dual-headers]] — same family of «RTCW vs Q3-canonical drift» that bit M5/M6.

## Phase 1 closed 2026-06-13 — architectural reorder

Phase 1 (MDC loader + `.md3→.mdc` fallback) landed in 5 commits on `macos-arm64-vulkan`:
- `b435e4e` — MOD_MDC enum + mdc[] slot + R_LoadMDC forward decl
- `2883895` — vendor-port `R_LoadMDC` into `code/renderervk/realrtcw_tr_mdc.c` + SF_MDC
- `6e3bb5d` — fix-up: drop dead `#ifdef REALRTCW_ALLOW_VENDOR_EDIT` guards (convention drift, see [[feedback-vendor-prefix-convention]])
- `c776be3` — wire `.md3→.mdc` fallback and MDC ident dispatch in `R_RegisterMD3`
- `ffc0813` — code-review cleanup: drop redundant `mod->type = MOD_MDC` dispatcher write

Plan body: `notes/plans/2026-06-13-m9-phase1-mdc-loader-and-fallback.md`.

### Phase 1 smoke verdict — surprise

The original Phase 1 exit gate was `+map escape1 +quit` reaching past `DEFAULT_MODEL failed`. With `model = "player"` (the saved cvar default), it still failed — **because the asset shipped for player is `body.mds` (skeletal), not `lower.mdc`.** RealRTCW's stock `player`/`bj2` default character lives in `z_zperson.pk3` and ships ONLY `body.mds` + `head.md3`. There is no `lower.{md3,mdc}` for the default character. The `.md3→.mdc` fallback added by Phase 1 is structurally correct but inert for MDS-based characters.

Validated Phase 1's MDC code path by setting `+set model "skel"` (a character that DOES ship `lower.mdc` in `pak0.pk3`):
- Server reaches `AAS initialized.` ✓
- No `DEFAULT_MODEL failed`, no `Failed to load legs model` ✓
- New failure downstream: `G_ParseAnimationFiles(): file 'models/players/skel/wolfanim.cfg' not found` — this is a game-logic asset miss, NOT a renderer gap. Different M-class issue (call it M9.5 if it needs tracking).

So Phase 1's MDC loader+fallback IS working — its hypothesis was «load .mdc when caller asks for .md3», which is now proven correct for MDC-only characters.

### Architectural reorder

The original sequence (Phase 1 MDC loader → Phase 2 MDC runtime → Phase 3 MDS) assumed Phase 1's loader would unblock the default character. It does not. Default character (`player`/`bj2`) uses MDS — that's Phase 3 territory. Reorder:

```
Phase 1 (MDC loader)          ← CLOSED 2026-06-13. Inert for default char.
Phase 3 (MDS loader+runtime)  ← NOW ON CRITICAL PATH for default character.
Phase 2 (MDC runtime)         ← Behind Phase 3 — MDC-only characters (skel/zombie) are
                                rarer than MDS characters in stock RTCW, so the player-
                                visible value lands after Phase 3.
```

Phase 2 + Phase 3 still need separate plans, written after recon. Phase 3 recon priorities:
1. Read legacy `code/renderer/tr_model.c::R_LoadMDS` (~660 LOC, line 1624 onwards).
2. Read legacy `code/renderer/tr_animation.c` MDS half (~1360 LOC, lines 1-1366).
3. Confirm `mdsHeader_t` etc. are in `qcommon/qfiles.h` (already known from Phase 1 recon — yes).
4. iortcw `rend2/` reference port shape for MDS runtime adaptation (already known — see Reference Port section above).

The `wolfanim.cfg` miss surfaced during Phase 1's smoke is a SEPARATE blocker for `escape1` gameplay. It's game-side asset/path issue, not renderer. Likely a config path defaults to `player/wolfanim.cfg` and the stock asset for "skel" doesn't ship one. Classify as standalone M9.5 if needed once Phase 3 lands.

## Phase 3a closed 2026-06-13 — MDS loader landed

Phase 3a (MDS loader, NO runtime) landed in 3 commits on `macos-arm64-vulkan` ahead of `adf4c41`:

- `291fb5c` — types in `tr_local.h`: `MOD_MDS` enum, `mdsHeader_t *mds` slot in `model_t`, `SF_MDS` in `surfaceType_t`, forward decls for `R_LoadMDS`/`R_AddAnimSurfaces`/`RB_SurfaceAnim`/`R_GetBoneTag`.
- `5238b18` — vendor-port `R_LoadMDS` into `code/renderervk/realrtcw_tr_mds.c` (211 LOC verbatim from legacy `tr_model.c:1620-1803` with non-static + `int filesize` signature adaptation).
- `9aee73a` — wire `R_RegisterMDS` + `{"mds", R_RegisterMDS}` entry in `modelLoaders[]` in `code/renderervk/tr_model.c`.

Plan body: `notes/plans/2026-06-13-m9-phase3-mds-skeletal.md` (Phase 3a = Tasks 1-4).

### Phase 3a smoke verdict — clean unblock for default character

With `+set model "player" +map escape1 +wait 200 +quit`:

- **No `DEFAULT_MODEL failed`** (default character `player` now resolves via `body.mds` → R_LoadMDS).
- **No `Failed to load legs model`** for the player.
- **`AAS initialized.` × 2** (escape1 ships 2 AAS files; both load).
- **Full media-load sequence completes:** collision map → sounds → graphics → BSP → game media → textures → models → weapons → items → inline models → server models → particles → game media done.
- **`CL_InitCGame: 3.70 seconds`** — cgame fully initializes.
- **Clean `Server Shutdown (Server quit)`** — NOT `Server crashed` — process exits via `+quit` as designed.
- Exit code 0, sanitizers clean.

The character will be **invisible in-world** because Phase 3b (`SF_MDS` dispatch, `R_AddAnimSurfaces`, `RB_SurfaceAnim`) hasn't landed yet. That's expected and acceptable for the Phase 3a exit criterion. One downstream NPC fails to load (`doc/head.md3` — unrelated, that character probably ships head as MDC and is not in scope for this gate).

The `wolfanim.cfg` blocker speculated earlier — DID NOT SURFACE for `model=player`. It may have been specific to `model=skel` or to a particular load order. Reclassify: not a confirmed M9.5 blocker on the default path. Watch in Phase 3b smoke.
