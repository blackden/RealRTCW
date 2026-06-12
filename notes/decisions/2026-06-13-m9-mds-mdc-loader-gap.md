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
Port `R_RegisterMDS`, `R_LoadMDS`, `R_LoadMDC`, `R_RegisterMDC` + the `.md3→.mdc` fallback from `code/renderer/tr_model.c` into `code/renderervk/tr_model.c`. Mirror to **M5.10's pattern** for vendor-clean RTCW-specific extensions: gated by `REALRTCW_ALLOW_VENDOR_EDIT`, with a decision-note explaining why it has to live in renderervk. **Engineering: 1-2 days. Quality: best — single source of truth, no ABI surprise. Risk: requires understanding MDS/MDC binary layout; legacy code is ~600 lines of bit-swap + Hunk_Alloc juggling.**

### Recommendation (after one session of reflection — not picking yet)

γ is the structurally honest answer and matches the M7.5 epistemic stance («favor architectural-correct path even when slower» — see `memory/project_portfolio_vision.md`). β is the layered-decision parallel to M4's θ' / γ' split, with γ being the principled Phase 3 cleanup. α is throwaway and shouldn't be picked.

Default pick is **γ** unless cross-fork archaeology shows Quake3e or vkQuake3 already vendored the MDS/MDC loaders for some reason (cheap to check next session via the `q3-fork-detective` agent).

## Recon to do before the M9 plan body

1. **MDS/MDC binary spec.** Read `code/renderer/tr_model.c` `R_LoadMDS` and the MDC half top-to-bottom. Take notes on Hunk_Alloc shape and what gets in-place-modified vs copied.
2. **Cross-fork lookup.** Run `q3-fork-detective` on `R_RegisterMDS` / `MDS_IDENT` across iortcw-SP, iortcw-MP, Quake3e, vkQuake3, RealRTCW upstream. See whether anyone has already done the renderervk port.
3. **REALRTCW_ALLOW_VENDOR_EDIT precedent.** Re-read `notes/decisions/2026-06-12-m5-10-hunk-temp-overflow-workaround.md` for the guard pattern and decision-note shape.
4. **MDS/MDC test corpus.** Catalog which files the cgame actually requests on `escape1` and 2-3 other maps — to know what code paths the port must cover for smoke pass.

## Cross-links

- `code/renderer/tr_model.c:88-94` — the `.md3 → .mdc` swap that's missing on Vulkan.
- `code/renderer/tr_model.c:295` — legacy `R_RegisterMDS` slot in loader table.
- `notes/decisions/2026-06-12-m5-10-hunk-temp-overflow-workaround.md` — REALRTCW_ALLOW_VENDOR_EDIT precedent.
- `notes/plans/2026-06-12-m7-jpg-loader-and-m8-shader-keywords.md` — M7.5/2 procedure now gated behind M9.
- [[project-q3fork-dual-headers]] — same family of «RTCW vs Q3-canonical drift» that bit M5/M6.
