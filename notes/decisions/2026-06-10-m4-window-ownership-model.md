# M4 — Vulkan window-init contract: who creates SDL_Window?

**Date:** 2026-06-11 (rewritten — earlier draft had wrong Quake3e attribution)
**Branch:** `macos-arm64-vulkan` in worktree `~/fedorov_tech/RealRTCW-vulkan-wt`
**Milestone:** Phase 2 / M4 (first run + validation triage)
**Status:** **θ' LANDED** 2026-06-11 (commits `d52b71a` → `dbe754d`, see `docs/vulkan-phase2/m4-iter4-postθ.log`). `R_Init` now reaches `VKimp_Init`; the `SDL_window not initialized` fatal is gone. M4 fix-loop continues — next crash is segfault during `GLimp_SetMode` post-`VKimp_Init( )` print (earlier than the documented CL_SetScaling site at `tr_init.c:543`; root cause to be triaged next iter). **γ'** documented as the correct principled answer for Phase 3 once Vulkan is feature-complete.

## TL;DR — layered decision

| Horizon | Option | Why this layer |
|---|---|---|
| **M4 (now)** | **θ'** | symmetric with our OpenGL handoff, zero code duplication, 2-line vendor-edit, preserves green OpenGL safety net |
| **Phase 3 (after Vulkan reaches parity)** | **γ'** | the principled answer — single `sdl_glimp.c` engine-side, matches Quake3e topology 1:1, removes the class of two-globals bugs M3.5 fought |
| **Phase 5+ / new engine** | platform layer | greenfield ideal (Bevy/Godot/O3DE-style) — separate platform/RHI/game-logic layers with dependency injection |

Reading order if you're picking this up cold: read §1 (the architectural mismatch), then §2 (the 6 options compared), then §3 (the θ' implementation), then §4 (the γ' cleanup that should follow Vulkan-parity), then §6 (recon facts — so you don't re-discover them).

---

## §1 — Why we are here: the architectural mismatch

During M4 iter 3, `R_Init` reached `ri.VKimp_Init` and aborted with our own fatal guard at `code/client/cl_refvulkan.c:405`:

> `vk_VKimp_Init: SDL_window not initialized -- engine startup order is broken`

This is **architectural**, not a missing line. The vendored Quake3e Vulkan renderer expects `ri.VKimp_Init` to **create** the window. Our translator stub only checks for it. Two contracts in one codebase, glued in the middle.

### The topology delta (Quake3e vs us)

| Concern | Quake3e | RealRTCW (our fork) |
|---|---|---|
| Where `sdl_glimp.c` is linked | engine binary (`Q3OBJ`) | renderer DLL (`Q3ROBJ`, `Q3VKOBJ`) — **not** in engine |
| `SDL_window` storage | one global, engine-side | **two distinct globals**: engine-side `code/sdl/sdl_input.c:60` + per-DLL `code/sdl/sdl_glimp.c:67` |
| OpenGL handoff | inside one TU (`sdl_glimp.c`), no vtable hop | renderer DLL creates window → passes pointer via `ri.IN_Init(SDL_window)` cross-DLL vtable round-trip |
| `VKimp_Init` defined in | engine-side `sdl_glimp.c:694` | **nowhere** — only a guard-stub in `cl_refvulkan.c:405` |
| `IN_Init` signature | `void IN_Init(void)` | `void IN_Init(void *windowData)` |

**Root cause:** we vendored Quake3e's **renderer** without their **engine-side window TU**. The OpenGL split-TU pattern we inherited from iortcw doesn't extend naturally to Vulkan because there is no `VKimp_Init` analog in our renderer-DLL `sdl_glimp.c`.

---

## §2 — The six options on the table

Six were enumerated by an explorer-agent mapping pass (see `docs/vulkan-phase2/2026-06-10-m4-window-init-architecture-map.md` §3 for full citations). Summary with the architectural-justification axis spelled out:

| | Symmetric with our GL | 1:1 with Quake3e | Vendor-edit | Code dup | Risk to green OpenGL path | M4 scope |
|---|---|---|---|---|---|---|
| **α'** — new engine TU `realrtcw_sdl_vk_glimp.c` | ✗ asymmetric forever | partial | none | ~200 LoC (or refactor helper) | low | medium |
| **β'** — inline window code into `cl_refvulkan.c` | ✗ asymmetric forever | further from Quake3e than α' | none | ~200 LoC | low | small but SRP violation |
| **γ'** — move `sdl_glimp.c` engine-side, full Quake3e topology | new model (engine-owned) | **1:1** | yes, structural | **zero** (single source) | **high** during refactor | **large — Phase 3 work** |
| **ε'** — engine calls `ri.GLimp_Init` back to DLL with VK-flag | partial | far | medium | small | medium | medium (couples VK to GL bring-up) |
| **ζ'** — pre-call `GLimp_Init` from engine before `R_Init` | partial | far | medium | small | medium | medium (misleading function naming) |
| **θ'** — add `VKimp_Init` to renderer-DLL `sdl_glimp.c`, vendor-edit `tr_init.c` to call it | **✓ matches GL pattern** | not topologically (DLL-side vs engine-side) but mechanically yes | **2 lines** in `tr_init.c` | **zero** (shared `GLimp_StartDriverAndSetMode` via `qboolean vulkan` flag) | **low** | **small** |

### Why each was even considered (architectural motivation)

- **α'** — preserve vendored renderer integrity (no edits to `code/renderervk/`) by keeping all RealRTCW glue in `code/sdl/`. Same family of decisions as M3.5 vtable adapter: absorb impedance mismatch engine-side, not in vendored code.
- **β'** — minimize artifacts: put translator + window-init in one file. Tempting for "small change", catastrophic for SRP.
- **γ'** — principled correctness: match upstream topology, single `SDL_window`, kill the class of bugs caused by two-globals. The right answer if you had time and OpenGL didn't matter.
- **ε'** — reuse existing GL window-creation infrastructure with a flag-bit. Symmetric backend handling, but couples Vulkan into a function literally named `GLimp_*`.
- **ζ'** — variant of ε' shifted up the call stack. Same coupling problem.
- **θ'** — synthesis: keep our renderer-DLL-owns-window pattern (symmetric with GL), but add the Vulkan branch *inside* the DLL so window-creation is in the right place architecturally. Mechanically mirrors what Quake3e does, just at the DLL-TU boundary instead of engine-TU boundary.

---

## §3 — M4 decision: **θ'**

### What changes

1. **`code/sdl/sdl_glimp.c`** (single file compiled into both renderer DLLs via Makefile per-target rules — `Makefile:2123` for OpenGL, `:2177` for Vulkan):
   - Add `void VKimp_Init( glconfig_t *config )` next to existing `GLimp_Init` (lines around `:1217`).
   - Refactor `GLimp_StartDriverAndSetMode(int mode, qboolean fullscreen, qboolean noborder, qboolean gl3Core)` at `:956` to take a 5th argument `qboolean vulkan`. Branch flag bit at `SDL_CreateWindow:788`: `SDL_WINDOW_VULKAN` when `vulkan == qtrue`, `SDL_WINDOW_OPENGL` otherwise. Skip `SDL_GL_SetAttribute` / `SDL_GL_CreateContext` block (`:760-880`) on the Vulkan path.
   - In new `VKimp_Init`: call refactored `GLimp_StartDriverAndSetMode(..., qtrue)`, resolve `qvkGetInstanceProcAddr = SDL_Vulkan_GetVkGetInstanceProcAddr()` (SDL3 form), call `ri.IN_Init(SDL_window)` — exactly the existing handoff used by `GLimp_Init` at `:1324`.

2. **`code/renderervk/tr_init.c`** (vendor-edit, needs `REALRTCW_ALLOW_VENDOR_EDIT=1`):
   - Line `:535`: change `ri.VKimp_Init(&glConfig)` → `VKimp_Init(&glConfig)` (local call, no vtable hop).
   - Line `:1985`: same swap for `VKimp_Shutdown`.

3. **`code/client/cl_refvulkan.c`**:
   - `vk_VKimp_Init` becomes intentionally-NULL slot (or remove from the translator output entirely).
   - Document at the slot why it's NULL: "renderer-DLL owns Vulkan window creation; this slot is unused. See `notes/decisions/2026-06-10-m4-window-ownership-model.md`."

### Why θ' is the local maximum *for this milestone*

- **Symmetric with our OpenGL contract.** Reader's mental model: "renderer DLL always creates the window, hands to engine via `ri.IN_Init`". One pattern, two backends. No backend-dependent search-in-files.
- **Zero code duplication.** `GLimp_StartDriverAndSetMode` becomes the single window-init helper, parameterized by backend flag. Quake3e does exactly this at `refs/Quake3e/code/sdl/sdl_glimp.c:715`.
- **OpenGL path untouched.** All the M3.5 work that landed the OpenGL boot stays green. Vulkan is purely additive.
- **2-line vendor-edit.** Not a structural change to vendored code — just a redirect to a function in a sibling TU. Future Quake3e renderer merges flow through.
- **Smallest reversible change.** If θ' turns out wrong, revert is `git restore code/renderervk/tr_init.c code/renderervk/sdl_glimp.c`. No engine binary surgery.

### Cost accepted

- Not topologically identical to Quake3e (they put `VKimp_Init` engine-side). Future merges of Quake3e's *engine-side* `VKimp_Init` changes require manual port into our DLL-side copy. Mitigated by §4 — γ' eventually closes this gap.
- Vendor-edit requires the env-var gate and prefix-convention awareness. `tr_init.c` is vendored upstream, exempt from prefix rule (we're editing, not creating).

---

## §4 — Why **γ'** is the correct principled answer, and when to do it

θ' is "right for now". γ' is "right, full stop". The reason we are *not* doing γ' today:

> γ' refactors the **OpenGL** handoff path during **Vulkan** work. The OpenGL path is currently the safety net — if Vulkan derails, we drop back to GL to ship. Doing γ' now means **both paths break simultaneously** if anything in the refactor goes wrong.

### What γ' would look like (so future-us doesn't re-discover)

1. Move `code/renderervk/sdl_glimp.c` (currently per-DLL) and `code/sdl/sdl_gamma.c` into engine binary objs (`Q3OBJ` in Makefile).
2. Remove `$(B)/renderer/sdl_glimp.o`, `$(B)/rendv/sdl_glimp.o` from `Q3ROBJ` / `Q3VKOBJ` to avoid duplicate-symbol at engine link.
3. Delete the duplicate `SDL_window` definition at `code/sdl/sdl_input.c:60` — rely on the now-single global from `sdl_glimp.c:67` via the header `sdl_glw.h` pattern (cf. `refs/Quake3e/code/sdl/sdl_glw.h:52`).
4. In `code/client/cl_main.c`, install vtable slots engine-side just like `refs/Quake3e/code/client/cl_main.c:3479`:
   ```c
   rimp.GLimp_Init = GLimp_Init;
   rimp.VKimp_Init = VKimp_Init;
   /* + GLimp_Shutdown, GLimp_EndFrame, etc. */
   ```
5. Change `IN_Init` signature from `void IN_Init(void *windowData)` back to `void IN_Init(void)` — it can now read engine-local `SDL_window` directly. Update all 5-ish call sites.
6. Decide what to do with our M3.5 vtable adapter (`cl_refvulkan.c`): the BIG-refImport translation logic is still needed (Quake3e refImport_t has fields RealRTCW doesn't); but the *window-init* slots can shed their stubs.

### Pre-conditions for γ' (gating rules)

Do **not** start γ' until **all** of these are green:
1. Vulkan renderer reaches feature parity with the RTCW OpenGL renderer (all maps render, fog system works, brightness/gamma, HUD, menus, vid_restart cycle).
2. RealRTCW's OpenGL renderer is officially flagged for removal in the roadmap (per `[[project_realrtcw_macos]]` policy update: "OpenGL preserved until Vulkan reaches full feature parity").
3. A working branch backup of pre-γ' state exists with a known-good build of both backends.

### Estimated γ' effort

Per the architecture map (§3.3 of `docs/vulkan-phase2/2026-06-10-m4-window-init-architecture-map.md`): "biggest right-thing, but rewrites OpenGL handoff mid-Vulkan-work. Phase 3 candidate, not M4." Concrete file count: ~5-7 files touched, ~300-500 LoC moved (no net new code; mostly relocation + `#ifdef` cleanup). Risk: medium-high if attempted without a green Vulkan baseline; low once Vulkan is the primary path.

---

## §5 — Future ideal: platform layer (Phase 5+ aspiration, NOT a near-term plan)

## §5a — OpenGL renderer sunset policy (γ'-landed addition)

With γ' landed, the OpenGL path remains functional but its lifetime is
explicitly time-boxed. Conditions for removal:

1. Vulkan renderer reaches RTCW feature parity (all maps render, fog,
   HUD, vid_restart cycle, gamma, brightness).
2. ASAN + validation-layer clean on the Vulkan path.
3. At least one full campaign playthrough on Vulkan without regressions.

Once these conditions are met, the OpenGL slot is removed:
- `code/renderer/` directory deleted.
- `Q3ROBJ` Makefile object list removed.
- `BUILD_RENDERER_OPENGL` macro turned permanently off (or eliminated).
- `code/sdl/sdl_glimp.c` `if ( !vulkan )` branches deleted.
- Engine binary no longer carries dual-backend dispatch logic.

This is part of [[project_portfolio_vision]] Phase 3 modernization.

---

If we were greenfield in 2026 (Bevy / Godot / O3DE style), the right answer is **neither θ' nor γ'** — it is a **separate platform layer**:

```
+-----------------------------------------------+
|  Game logic                                   |
+-----------------------------------------------+
|  Renderer (RHI / render commands → pixels)    |
+-----------------------------------------------+
|  Platform layer (window, input, audio, FS,    |
|                  timers, OS abstraction)      |
+-----------------------------------------------+
|  OS / hardware                                |
+-----------------------------------------------+
```

- Platform creates window, owns the handle (opaque pointer).
- Renderer takes `{window_handle, frame_data}` → produces pixels. Stateless toward window beyond the handle.
- Engine orchestrates: asks platform for a window with properties X, gives the handle to renderer, gives the same handle to input subsystem.
- No one "owns" — everyone has a clear role + dependency-injected resources.

**Why not now:** requires extracting `code/sdl/`, input, audio device, timers from engine TUs into a separate layer with stable internal ABI, and re-routing every consumer. Weeks of work. ROI in a Q3-fork context: low (Q3 codebase will never approach Bevy-quality layering without a full rewrite).

**When this matters:** if Phase 3+ ever brings additional render backends (Metal-native? D3D12 cross-port? software fallback?), the ownership question reopens at scale. At that point a platform layer becomes worth its cost.

---

## §6 — Reconnaissance facts (verbatim, so future-you doesn't re-discover)

These are the file:line citations the explorer agent surfaced during 2026-06-10/11 mapping. Cached here because the next session will otherwise burn a subagent re-finding them.

### OpenGL handoff chain (the working pattern we copy in θ')

1. `code/client/cl_main.c:3341` — `CL_StartHunkUsers` → eventually calls `R_Init` via vtable.
2. `code/sdl/sdl_glimp.c:1217` — `GLimp_Init(qboolean fixedFunction)`. Renderer-DLL-side.
3. `code/sdl/sdl_glimp.c:956` — `GLimp_StartDriverAndSetMode(int mode, qboolean fullscreen, qboolean noborder, qboolean gl3Core)`. The function θ' will extend with `qboolean vulkan`.
4. `code/sdl/sdl_glimp.c:788` — `SDL_CreateWindow(...)`. The flag site. SDL3 requires `SDL_WINDOW_VULKAN` at creation, cannot be toggled later.
5. `code/sdl/sdl_glimp.c:67` — renderer-DLL `SDL_Window *SDL_window = NULL;`
6. `code/sdl/sdl_glimp.c:1324` — `ri.IN_Init( SDL_window );` (the handoff call).
7. `code/client/cl_main.c:3476` — `ri.IN_Init = IN_Init;` (engine-installed vtable slot).
8. `code/sdl/sdl_input.c:60` — engine-side `SDL_Window *SDL_window = NULL;` (the second global).
9. `code/sdl/sdl_input.c:1235` — `IN_Init(void *windowData)`.
10. `code/sdl/sdl_input.c:1245` — `SDL_window = (SDL_Window *)windowData;` (the assignment that lands the value-copy).

### Vulkan call chain (current broken state)

1. `code/client/cl_main.c:3409` — `dllName = "renderer_sp_vulkan_<arch>.dylib"`.
2. `code/client/cl_main.c:3487-3500` — `#ifdef BUILD_RENDERER_VULKAN` block calling `CL_BuildVulkanRefImport()`.
3. `code/client/cl_refvulkan.c:129` — `CL_BuildVulkanRefImport()`.
4. `code/renderervk/tr_init.c:535` — `ri.VKimp_Init(&glConfig)`. θ' edits this line.
5. `code/renderervk/tr_init.c:1985` — `ri.VKimp_Shutdown(...)`. θ' edits this line.
6. `code/client/cl_refvulkan.c:405` — `vk_VKimp_Init` stub (guard-only, fails because nothing populated engine `SDL_window`).
7. `code/client/cl_refvulkan.c:233` — `vk_ri.CL_SetScaling` documented as intentionally NULL — **adjacent landmine, M4 will hit this next after window is fixed.** `code/renderervk/tr_init.c:543, 556, 562` call `ri.CL_SetScaling`.

### Vendor shim: `realrtcw_vk_window_bridge.{c,h}`

NOT a window handoff. Link-time cvar fallback storage only.
- `code/renderervk/realrtcw_vk_window_bridge.c:60-196` — tentative-defines 8 cvar pointers + 2 scalars + `R_GetModeInfo` that vendored `sdl_glimp.c` expects as externs but Quake3e's `renderervk/` dropped.
- `code/renderervk/realrtcw_vk_window_bridge.c:177` — `RealRTCW_VkBridgeInit()` runtime caller, only called from `code/sdl/sdl_glimp.c:1220` inside `GLimp_Init`. **Unreachable on Vulkan path today.**
- Implication for θ': the new `VKimp_Init` in DLL `sdl_glimp.c` should call `RealRTCW_VkBridgeInit()` itself to get those cvars registered. Currently this is the only way they get init'd.

### Quake3e baseline (the reference, so we don't re-misattribute)

- `refs/Quake3e/code/client/cl_main.c:3479` — `rimp.VKimp_Init = VKimp_Init;` (engine-side vtable wire — proves Quake3e is engine-owned-window).
- `refs/Quake3e/code/sdl/sdl_glimp.c:50` — single `SDL_Window *SDL_window`.
- `refs/Quake3e/code/sdl/sdl_glimp.c:694` — engine-side `VKimp_Init(glconfig_t *config)`.
- `refs/Quake3e/code/sdl/sdl_glimp.c:715` — `GLimp_StartDriverAndSetMode(..., qtrue /* Vulkan */)` — the shared helper pattern θ' mirrors.
- `refs/Quake3e/code/sdl/sdl_glimp.c:735` — `qvkGetInstanceProcAddr = SDL_Vulkan_GetVkGetInstanceProcAddr();`
- `refs/Quake3e/code/sdl/sdl_glw.h:52` — `extern SDL_Window *SDL_window;` (the single-source pattern γ' would adopt).
- `refs/Quake3e/Makefile:1219-1224` — `sdl_glimp.o`, `sdl_gamma.o`, `sdl_input.o`, `sdl_snd.o` all in `Q3OBJ`. None in `Q3RENDVOBJ` (lines 936-967).

### Makefile coordinates

- `Makefile:2066` — `$(B)/client/sdl_input.o` → engine binary (our engine-side SDL).
- `Makefile:2123` — `$(B)/renderer/sdl_glimp.o` → OpenGL renderer DLL.
- `Makefile:2177` — `$(B)/rendv/sdl_glimp.o` → Vulkan renderer DLL.
- `Makefile:3090` — `$(B)/rendv/sdl_glimp.o: override CFLAGS += -DBUILD_RENDERER_VULKAN`. The `override` keyword is critical (latent M3 bug surfaced earlier).

### SDL3 specifics (don't get burned)

- `SDL_WINDOW_VULKAN` must be set at `SDL_CreateWindow` time — cannot be toggled.
- `SDL_Vulkan_CreateSurface` gained an allocator arg in SDL3 (already in `cl_refvulkan.c:377`).
- `SDL_Vulkan_GetVkGetInstanceProcAddr` exists in both SDL2 and SDL3, signature compatible.
- IN_Init has `SDL_WasInit(SDL_INIT_VIDEO)` check at `code/sdl/sdl_input.c:1239` — order-of-operations between Cmd_AddCommand (passed in M3.5) and SDL_INIT_VIDEO already validated.

---

## §7 — Implementation handoff

Plan to be written next: `docs/superpowers/plans/2026-06-11-vulkan-m4-window-contract.md` (via `superpowers:writing-plans` skill).

After θ' lands and Vulkan reaches feature parity, see §4 for γ' migration.

## Related

- [[2026-06-09-m3.5-vtable-adapter-proper]] — same family of decisions: preserve vendored renderer integrity by absorbing impedance mismatch in engine-side glue, not by editing vendored code. M3.5 chose engine-side translator (`cl_refvulkan.c`); θ' chooses minimal DLL-side glue + 2-line vendor edit. Symmetric philosophy.
- [[2026-06-08-vendor-prefix-convention]] — RealRTCW-authored files in vendored dirs must start with `realrtcw_`. `tr_init.c` is vendored upstream, exempt. New code in renderer-DLL `sdl_glimp.c` is also vendored — vendor-edit gating applies.
- [[roadmap-vulkan-program]] — Phase 2 parity work (current), Phase 3 modernization (γ' migration), Phase 4 premium, Phase 5+ greenfield aspirations.
- `docs/vulkan-phase2/2026-06-10-m4-window-init-architecture-map.md` — authoritative architecture map from explorer-agent run. This decision note is the digest; the map has full citations.
