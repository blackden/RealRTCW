# M6 tr_types.h unification (2026-06-12)

Closes M6 milestone (first rendered frame in main menu on Vulkan path). Different shape from M5 — M5 was a vtable translator between two ABI surfaces. M6 went the opposite direction: eliminated the divergence entirely by unifying the type definitions.

## Where we ended

5 commits on `macos-arm64-vulkan` (`609ecc1` → `f6b3555`). Two `tr_types.h` files now produce byte-identical `refEntity_t` / `refdef_t` / `glconfig_t` / `polyVert_t`. Runtime sentinels and compile-time `_Static_assert` lock sizes against future drift.

Smoke verification: `scripts/mac/playtest.sh --vulkan` reaches `--- Common Initialization Complete ---`, main menu first frame renders (textures blurred/missing due to JPG-loader stub + asset path issues — both pre-existing, NOT M6 regressions). Clean quit exit 0.

## Decision — canonical unification, not per-call translator

**Decision.** Make `code/renderer/tr_types.h` (SMALL — RTCW heritage) and `code/renderercommon/tr_types.h` (BIG — vendored Quake3e) produce byte-equivalent typedefs. Canonical layout = SMALL extended with one promotion: `float shaderTime` → `floatint_t shaderTime`. Both files keep their distinct shims (q_shared.h include, COLOR4UB_T_DEFINED fallback) but the typedef bodies are identical.

The shared `__TR_TYPES_H` guard means only one file's definitions win per TU. As long as the layouts agree, that's fine — the surviving definition produces the right struct shape regardless of include order.

**Why.** Three options were live during planning:

1. **Per-call translator.** Allocate a BIG-shape scratch buffer at `vk_re_thunk_AddRefEntityToScene` and `BeginRegistration`, copy fields one by one. Engine code untouched.
2. **Unify BIG ⊇ SMALL.** Canonical = SMALL extended. Both headers produce same layout. Engine code untouched mostly; renderer code rebuilt against new layout.
3. **Hybrid.** Critical structs unify, non-critical structs use translator.

Picked (2) because:

- Single source of truth. Class of "layout drift between SMALL and BIG" bugs disappears — there's no drift surface left.
- Zero per-frame overhead. `AddRefEntityToScene` runs hundreds-thousands times per frame; translator (1) would copy 232 bytes per call.
- Opens Phase 3+ RTCW feature parity in Vulkan path. Renderer now has access to torso-anim fields, glfog, anisotropy hints — they're just unused right now, not amputated.
- Aligns with [[feedback-layered-decisions]] structural-fix-over-workaround preference.

**Trade-off.** Vendor edit in `code/renderercommon/tr_types.h` (REALRTCW_ALLOW_VENDOR_EDIT=1 required at claude launch). Two header files must stay in sync — drift detection via the `_Static_assert` block in `code/client/cl_refvulkan.c` + runtime sentinels in `tr_init.c:1442` (OpenGL) and `tr_init.c:1864` (Vulkan).

**Revisit if.** If a renderer fork wants to keep its own `tr_types.h` substantially different from RealRTCW (e.g., adding new fields the engine doesn't fill), the unification breaks. At that point either expand canonical to superset, or rewind to (1) per-call translator. The unification commits are surgical enough to revert if needed.

## Field-level changes

| Type | Field | Before | After |
|---|---|---|---|
| refEntity_t | shaderTime | `float` | `floatint_t` (union of float/int/uint) |
| refEntity_t | shader/shaderRGBA | anonymous outer union { byte[4], anonymous inner union } | anonymous outer union { byte shaderRGBA[4]; color4ub_t shader; } |
| (other fields) | — | RTCW-extended in SMALL only | RTCW-extended in BOTH (BIG mirrors SMALL) |

The `shaderTime` promotion was forced by `code/renderervk/tr_backend.c:680,907` which read `.f` and `.i` union members — those only compile if the field IS `floatint_t`. The `shader` member type change resolved a separate landmine: `code/renderervk/tr_surface.c:248` passes `e.shader` by-value to `RB_AddQuadStamp(color4ub_t)`, which only type-matches if `shader` IS `color4ub_t` rather than an anonymous union.

## Call-site adjustments

Beyond the header edits, four code adjustments were needed:

| Site | Reason |
|---|---|
| `code/cgame/cg_event.c:1317`, `cg_effects.c:71/131/200/257`, `cg_players.c:3891`, `cg_weapons.c:1075/6001`, `cg_localents.c:1285/1289` | 10 engine writes `re->shaderTime = X` → `.f = X` (all use `.f`, see ABI gap note below) |
| `code/renderer/tr_backend.c:824,953` + `tr_shade.c:891/896` | 4 engine reads `e.shaderTime` (float) → `.f` |
| `code/renderervk/tr_surface.c:277` | Was `tess.vertexColors[numv] = p->verts[i].modulate;` (illegal byte[4]→union assign) → `Com_Memcpy( tess.vertexColors[numv].rgba, p->verts[i].modulate, 4 );`. Vendor edit. |
| `code/renderervk/tr_init.c:1864` | Hardcoded ABI sentinel `sizeof(glconfig_t) != 11332` updated to `7268` (canonical size). Vendor edit. |

## ABI gap — intShaderTime not wired

The renderervk `AddRefEntityToScene(const refEntity_t *re, qboolean intShaderTime)` takes an extra flag absent in SMALL `AddRefEntityToScene(const refEntity_t *re)`. The M5 translator always passes `intShaderTime = qfalse`, so renderervk always reads `e.shaderTime.f`. This means:

- All 10 cgame writes use `.f` (preserves pre-M6 wire byte pattern).
- `cg_localents.c:1285,1289` writes `1434` and `0` as floats. Originally these may have been intended as integer-encoded shaderTime (the renderervk `.i` read path exists for this), but SMALL refImport has no channel to communicate the flag.
- The integer-encoded shaderTime trick is effectively no-op on Vulkan path until SMALL refImport gains an int-time variant. **Out of M6 scope.** Pre-existing behavior preserved.

## Locked sizes

`_Static_assert` block at `code/client/cl_refvulkan.c` top-level (right above `CL_BuildVulkanRefImport`):

```c
_Static_assert( sizeof( refEntity_t ) == 232,  ... );
_Static_assert( sizeof( refdef_t )    == 432,  ... );
_Static_assert( sizeof( glconfig_t )  == 7268, ... );
_Static_assert( sizeof( polyVert_t )  == 24,   ... );
```

If either tr_types.h drifts, build fails here. To deliberately change canonical: mirror change in both headers, update sentinels in `code/renderer/tr_init.c:1442` AND `code/renderervk/tr_init.c:1864`, update these `_Static_assert` constants.

## Cross-links

- [[project-m5-refexport-divergence]] — the ABI translator M5 closed. M6's unification means future M-work in this area can shed the translator complexity if shaderTime flag ever lands.
- [[project-m5-10-hunk-free-temp-memory-landmine]] — landmine M5.10 closed during M5 smoke. Unrelated to M6 (memory allocator, not type layout) but on the same boot path.
- [[project-q3fork-dual-headers]] — observed pattern (parallel tr_public.h/tr_types.h with different ABI shapes in Q3-family forks). RealRTCW is now the first sibling to merge them.
- `notes/plans/2026-06-12-m6-types-unification.md` — the execution plan.

## What's next

M7 candidate scope (visual fidelity, not ABI):
- JPG-loader stub (`vk_CL_LoadJPG` returns NULL) — wire to libjpeg or transcode-on-write at install time.
- Asset paths — `realrtcw_background.tga` not found suggests a search-path issue when running with full RTCW asset pack.
- Texture sampler defaults — current frame is blurred/grayscale, sampling parameters likely defaulted to something low-quality.
- Unknown shader keywords (`nofog`, `nocompress`) — renderervk silently ignores RTCW-extended shader syntax. Each warns once per shader.
