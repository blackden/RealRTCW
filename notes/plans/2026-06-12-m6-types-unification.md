# M6 — tr_types.h Unification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Steps use `- [ ]` for tracking.

**Goal:** Unify `code/renderer/tr_types.h` (SMALL, RTCW-extended) and `code/renderercommon/tr_types.h` (BIG, Quake3e-baseline) so engine and renderervk see byte-identical struct layouts. Eliminates layout-mismatch class of bugs at `BeginRegistration` / `AddRefEntityToScene` / `RenderScene`.

**Architecture:**
- Canonical layout = current SMALL (RTCW-extended) + one promotion: `float shaderTime` → `floatint_t shaderTime` (renderervk requires union access for integer-encoded shaderTime trick used by `cg_localents.c`).
- Both `tr_types.h` files get identical canonical content; existing shared `__TR_TYPES_H` guard ensures one wins per TU but they produce equivalent types.
- Engine-side cgame patched at 10 sites where `re->shaderTime = X` (8 float, 2 int) → `.f = X` / `.i = X`.
- Compile-time `_Static_assert(sizeof(refEntity_t) == sizeof(refEntity_t))` cross-included for layout drift detection.

**Tech Stack:** C, RealRTCW Makefile, REALRTCW_ALLOW_VENDOR_EDIT=1 (already set in env).

**Scope note:** `refdef_t` and `polyVert_t` work as-is — renderervk reads only the common prefix of refdef_t (no `glfog` reads) and polyVert_t.modulate has same layout (`byte[4]` vs `color4ub_t` = `byte[4]`). No changes needed for those.

---

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `code/renderer/tr_types.h` | Modify | Canonical SMALL with `floatint_t shaderTime` promotion |
| `code/renderercommon/tr_types.h` | Modify (vendor) | Mirror SMALL content byte-for-byte |
| `code/cgame/cg_event.c` | Modify | 1 shaderTime write → `.f` |
| `code/cgame/cg_effects.c` | Modify | 4 shaderTime writes → `.f` |
| `code/cgame/cg_players.c` | Modify | 1 shaderTime write → `.f` |
| `code/cgame/cg_weapons.c` | Modify | 2 shaderTime writes → `.f` |
| `code/cgame/cg_localents.c` | Modify | 2 shaderTime writes → `.i` |
| `code/client/cl_refvulkan.c` | Modify | Remove M6-aware comment in `vk_re_thunk_AddRefEntityToScene` (now stale) |

---

## Task 1: Promote shaderTime to floatint_t in canonical SMALL header

**Files:**
- Modify: `code/renderer/tr_types.h:174`

- [ ] **Step 1: Verify floatint_t is reachable from tr_types.h**

The SMALL header does NOT currently `#include "../qcommon/q_shared.h"` — it relies on the includer to bring `byte`/`vec3_t`/`qboolean`/`floatint_t` into scope. Engine TUs include `q_shared.h` → `qcommon.h` → `tr_types.h`, so floatint_t is available. Verify by reading.

Run: `grep -n "include" code/renderer/tr_types.h`
Expected: header has no `#include` directives — relies on transitive include from `q_shared.h` (engine path) or BIG's explicit include (renderervk path).

- [ ] **Step 2: Edit tr_types.h:174 — promote shaderTime field**

Replace:
```c
	float shaderTime;               // subtracted from refdef time to control effect start times
```
With:
```c
	floatint_t shaderTime;          // -EC- promoted to union for renderervk integer-encoded shaderTime trick (cg_localents.c)
```

- [ ] **Step 3: Confirm engine TUs still see floatint_t (build check after Task 3)**

Deferred to clean rebuild in Task 6 — at this isolated step we can't yet build (cgame writes still raw float).

---

## Task 2: Sync BIG header to canonical

**Files:**
- Modify: `code/renderercommon/tr_types.h` (vendor edit; REALRTCW_ALLOW_VENDOR_EDIT=1 confirmed)

- [ ] **Step 1: Replace BIG content with canonical SMALL content**

Copy the entire body of `code/renderer/tr_types.h` (lines 38-361 post-Task-1) into `code/renderercommon/tr_types.h`, preserving BIG's existing `#include "../qcommon/q_shared.h"` shim at top (engine-side header is included from contexts where q_shared.h is already brought in; renderervk TUs need BIG to pull q_shared.h explicitly).

Resulting BIG structure:
```c
#ifndef __TR_TYPES_H
#define __TR_TYPES_H

// RealRTCW shim: renderervk TUs include this header without prior q_shared.h
// Engine TUs already have q_shared.h in scope when they pull SMALL tr_types.h
#include "../qcommon/q_shared.h"

// color4ub_t fallback for older RealRTCW q_shared.h without COLOR4UB_T_DEFINED
#ifndef COLOR4UB_T_DEFINED
#define COLOR4UB_T_DEFINED
typedef byte color4ub_t[4];
#endif

/* === canonical body — IDENTICAL to code/renderer/tr_types.h lines 42-360 === */
/* (paste here) */

#endif // __TR_TYPES_H
```

- [ ] **Step 2: Verify identical layout via diff**

Run: `diff <(sed -n '/^typedef\|^#define MAX\|^#define R/,/^} \(refEntity_t\|refdef_t\|glconfig_t\|polyVert_t\)/p' code/renderer/tr_types.h) <(sed -n '/^typedef\|^#define MAX\|^#define R/,/^} \(refEntity_t\|refdef_t\|glconfig_t\|polyVert_t\)/p' code/renderercommon/tr_types.h)`

Expected: empty diff for struct bodies.

- [ ] **Step 3: Commit**

```bash
git add code/renderer/tr_types.h code/renderercommon/tr_types.h
git commit -m "feat(m6): unify tr_types.h — canonical SMALL+floatint_t layout in both copies

Engine and renderervk now see byte-identical refEntity_t/refdef_t/
glconfig_t/polyVert_t. Eliminates layout-mismatch class at the three
ABI struct-copy entry points (BeginRegistration, AddRefEntityToScene,
RenderScene).

Promotes shaderTime: float → floatint_t to satisfy renderervk's
integer-encoded shaderTime access (tr_backend.c:680,907; cg_localents.c
already writes integer values 1434/0). Engine-side .shaderTime writes
follow in next commit."
```

---

## Task 3: Patch engine cgame shaderTime writes

**Files:**
- Modify: `code/cgame/cg_event.c:1317`
- Modify: `code/cgame/cg_effects.c:71`, `:131`, `:200`, `:257`
- Modify: `code/cgame/cg_players.c:3891`
- Modify: `code/cgame/cg_weapons.c:1075`, `:6001`
- Modify: `code/cgame/cg_localents.c:1285`, `:1289`

- [ ] **Step 1: Patch 8 float-valued assignments → `.f`**

For each of these lines, replace:
```c
re->shaderTime = cg.time;                  // cg_event.c:1317
re->shaderTime = cg.time / 1000.0f;        // cg_effects.c:71
re->shaderTime = startTime / 1000.0f;      // cg_effects.c:131
re->shaderTime = cg.time / 1000.0f;        // cg_effects.c:200
ex->refEntity.shaderTime = ex->startTime / 1000.0f;  // cg_effects.c:257
re->shaderTime = cg.time;                  // cg_players.c:3891
re->shaderTime = cg.time / 1000.0f;        // cg_weapons.c:1075
re->shaderTime      = cg.time / 1000.0f;   // cg_weapons.c:6001
```
With `.f =`:
```c
re->shaderTime.f = cg.time;
re->shaderTime.f = cg.time / 1000.0f;
re->shaderTime.f = startTime / 1000.0f;
re->shaderTime.f = cg.time / 1000.0f;
ex->refEntity.shaderTime.f = ex->startTime / 1000.0f;
re->shaderTime.f = cg.time;
re->shaderTime.f = cg.time / 1000.0f;
re->shaderTime.f = cg.time / 1000.0f;
```

Note: `cg.time` is `int`. Assigning int to `floatint_t.f` requires implicit float conversion — already worked when field was `float`, semantics preserved.

- [ ] **Step 2: Patch 2 integer-valued assignments → `.i`**

For `code/cgame/cg_localents.c:1285,1289`:
```c
le->refEntity.shaderTime = 1434;   // → le->refEntity.shaderTime.i = 1434;
le->refEntity.shaderTime = 0;      // → le->refEntity.shaderTime.i = 0;
```

Verify integer-encoded intent: look ~20 lines around to confirm these sites are paired with an `intShaderTime`-flag passing into `RE_AddRefEntityToScene`. Cgame VM doesn't call ref API directly — it goes through trap_R_AddRefEntityToScene. Check trap impl on engine side handles intShaderTime correctly.

Run: `grep -n "intShaderTime\|trap_R_AddRefEntity" code/cgame/cg_localents.c | head -5`
Expected: confirms localents-2 call path passes the integer-shaderTime flag.

If NOT — escalation: probably means localents-2 set shaderTime as int but cgame infrastructure forgets to set the flag → renderer reads it as float and gets garbage (`1434` interpreted as IEEE float is ~2e-42, useless). This is a pre-existing RTCW bug, not introduced by us. Note in commit message, don't fix in M6 scope.

- [ ] **Step 3: Run static type-check by attempting compile of just cgame**

Run: `cd ~/fedorov_tech/RealRTCW-vulkan-wt && make wolfded ARCH=arm64 USE_INTERNAL_LIBS=0 2>&1 | grep "shaderTime\|error" | head -20`
Expected: no `shaderTime`-related compile errors. cgame is part of wolfded; if it builds, types resolve.

If errors: re-inspect site, may have missed a write or `.shaderTime` used as float in a comparison `if (re->shaderTime > X)`.

- [ ] **Step 4: Commit**

```bash
git add code/cgame/
git commit -m "fix(m6): cgame shaderTime writes → floatint_t union access

8 float-valued sites use .f, 2 integer-valued sites (cg_localents.c)
use .i to match RTCW integer-encoded shaderTime convention that
renderervk reads via e.shaderTime.i (tr_backend.c:680,907)."
```

---

## Task 4: Remove stale M6-aware comment from translator

**Files:**
- Modify: `code/client/cl_refvulkan.c:555-561`

- [ ] **Step 1: Update vk_re_thunk_AddRefEntityToScene comment**

Replace:
```c
void vk_re_thunk_AddRefEntityToScene( const void *re_ptr, int intShaderTime ) {
    /* re_ptr originates from engine-side SMALL refEntity_t. SMALL and BIG
     * tr_types.h diverge — boot-critical fields are believed to overlap
     * (proven by vk_BuildRefImport working through R_Init), but scene-
     * rendering field reads beyond the overlap zone are M6 territory. */
    vk_re_big->AddRefEntityToScene( (const refEntity_t *)re_ptr,
                                    intShaderTime ? qtrue : qfalse );
}
```
With:
```c
void vk_re_thunk_AddRefEntityToScene( const void *re_ptr, int intShaderTime ) {
    /* M6 closed: SMALL and BIG tr_types.h now byte-identical; the void*
     * cast crosses a layout-equivalent boundary. See
     * notes/decisions/2026-06-12-m6-types-unification.md */
    vk_re_big->AddRefEntityToScene( (const refEntity_t *)re_ptr,
                                    intShaderTime ? qtrue : qfalse );
}
```

(Deferred to commit alongside Task 6.)

---

## Task 5: Compile-time size-equality assertion

**Files:**
- Modify: `code/client/cl_refvulkan.c` (add static_assert at top, after includes)

- [ ] **Step 1: Add cross-include size assertion**

After the existing `#include` block in `cl_refvulkan.c`, add:
```c
/* M6 lock: SMALL (engine path via cm_public.h → ../renderer/tr_types.h)
 * and BIG (renderercommon/tr_types.h) tr_types.h must produce identical
 * layouts. The shared __TR_TYPES_H guard means only one definition wins
 * per TU; this assertion runs in the cl_refvulkan.c TU where the BIG
 * version is included, but the size match against a hand-rolled SMALL
 * sentinel constant is the cross-check. */
_Static_assert(sizeof(refEntity_t) == 264, "refEntity_t size drift — check tr_types.h unification");
_Static_assert(sizeof(refdef_t) == 760, "refdef_t size drift — check tr_types.h unification");
```

Sentinel values 264 / 760 are guesses — replace with actual sizes after first successful build. The static_assert fails at compile-time with current value, prompting us to lock in the real number.

Actually: simpler — emit the size via a Printf at runtime once, then bake into assert. Per the skill's "no placeholders" rule, prefer the runtime-probe-then-lock variant:

```c
// First build: comment out the asserts, add to CL_BuildVulkanRefImport:
//   Com_Printf("M6 sizes: refEntity_t=%zu refdef_t=%zu\n",
//              sizeof(refEntity_t), sizeof(refdef_t));
// Run smoke once, capture the numbers, then enable asserts with real values.
```

- [ ] **Step 2: Skip the assertion until empirical sizes known**

Mark this task as "deferred to post-smoke" — implement after Task 6 succeeds.

---

## Task 6: Clean rebuild + smoke (INTERACTIVE — needs ragnar)

**Files:** none (build artifacts only)

- [ ] **Step 1: Clean build artifacts**

```bash
cd ~/fedorov_tech/RealRTCW-vulkan-wt && rm -rf build/
```

- [ ] **Step 2: Build vulkan + engine**

```bash
make ARCH=arm64 USE_INTERNAL_LIBS=0 BUILD_RENDERER_VULKAN=1 -j8 2>&1 | tee /tmp/m6-build.log
```

Expected: exit 0. Watch for:
- Header errors (typedef redefinition, member not found) → typo in unified content
- cgame errors → missed shaderTime site
- linker errors → struct size mismatch flagged by _Static_assert (if Task 5 enabled)

- [ ] **Step 3: Smoke — needs ragnar in interactive Terminal (sandbox blocks SDL_CreateWindow in claude bash)**

Ragnar runs:
```bash
cd ~/fedorov_tech/RealRTCW-vulkan-wt && ./build/release-darwin-arm64/wolfsp_mac.app/Contents/MacOS/wolfsp_mac \
    +set fs_basepath ~/Games/RealRTCW \
    +set cl_renderer vulkan \
    +set in_initialize 0 \
    2>&1 | tee /tmp/m6-smoke.log
```

Watch for:
- "Common Initialization Complete" (M5.10 baseline)
- "BeginRegistration" succeeds without crash (M6 milestone start)
- Main menu first frame rendered (M6 milestone end)

If crash before menu: capture backtrace, search for refEntity/glconfig field access patterns.
If menu renders but garbled: enum mismatch (refEntityType_t — RT_SPLASH/RT_RAIL_CORE_TAPER values inserted in SMALL but not in BIG; if BIG sync from Task 2 left them out, sprites/beams will render as wrong type). Re-check Task 2 diff.

- [ ] **Step 4: Capture sizes for Task 5 assertion**

If smoke succeeds, grep the boot log for `M6 sizes:` line (added by Task 5 Step 1's Printf). Bake actual values into static_asserts. Commit.

- [ ] **Step 5: Commit**

```bash
git add code/client/cl_refvulkan.c
git commit -m "feat(m6): close — type unification verified, size asserts locked

Smoke boots through main menu first frame on Vulkan path.
_Static_assert pins refEntity_t and refdef_t sizes — future drift in
either tr_types.h breaks the build, not the renderer."
```

---

## Task 7: Codify decision

**Files:**
- Create: `notes/decisions/2026-06-12-m6-types-unification.md`

- [ ] **Step 1: Write decision note**

Standard format per `notes/decisions/2026-06-12-m5-refexport-translator-landed.md` template:
- Context: M6 needed first rendered frame; struct layout divergence between SMALL and BIG `tr_types.h`
- Options considered: per-call translator, unify, hybrid
- Choice: unify (canonical SMALL + shaderTime promotion)
- Why: single source of truth, zero runtime overhead, opens Phase 3 RTCW feature parity in Vulkan path
- Rejected alternatives: per-call translator (perf + permanent feature loss)
- Validation: smoke + static asserts
- Cross-link: [[project-m5-refexport-divergence]], [[project-q3fork-dual-headers]]

- [ ] **Step 2: Update memory entries**

Invoke `codify-findings` skill. Likely updates:
- New: `project_m6_types_unification.md` (CLOSED status)
- Update: `project_q3fork_dual_headers.md` — note canonical-merge approach worked
- Update: `MEMORY.md` index

- [ ] **Step 3: Decide untracked logs in docs/vulkan-phase2/**

Pre-M5.10 triage logs (backtrace*, banks, instrumented, trace, workaround, postwire-smoke). Choose per ragnar:
- `git add docs/vulkan-phase2/2026-06-12-*.log` if useful for archaeology
- `rm` otherwise

- [ ] **Step 4: Push branch**

```bash
git push origin macos-arm64-vulkan
```

---

## Risks and Watchpoints

1. **refEntityType_t enum drift** — if BIG sync misses RT_SPLASH/RT_RAIL_CORE_TAPER additions, sprites/beams render as wrong type. Task 2's diff step catches this.

2. **glconfig extensions_string size** — SMALL uses `4 * MAX_STRING_CHARS = 4096`. Renderer might emit more on rich Metal/MoltenVK extension list. If overflow seen: bump to `BIG_INFO_STRING = 8192` in unified canonical (still a vendor edit, still in budget).

3. **shaderTime int-encoded interpretation by cgame trap** — Task 3 Step 2 has fallback escalation: if cgame side doesn't set `intShaderTime` flag, this is a pre-existing RTCW bug, document and don't fix in M6.

4. **anonymous-union access on shader/shaderRGBA** — SMALL has C11 anonymous union. If build uses pre-C11 toolchain on some path, anonymous union access breaks. Engine builds use `-std=gnu99` or similar — check Makefile.

5. **clean rebuild interaction with vendor-block hook** — vendor edit env is on for THIS claude session; `git commit` runs hook that re-checks. Confirm hook still passes our edits.
