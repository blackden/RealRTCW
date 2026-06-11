# Vulkan M4 — Window Contract Reform γ' Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate `code/sdl/sdl_glimp.c` (and SDL-window ownership) from per-renderer-DLL into the engine binary, matching Quake3e topology. Eliminates the dual-`refimport_t` struct-view mismatch that crashed θ' iter 4 and unifies window-creation under a single engine-side TU. Both backends invoke `ri.GLimp_Init` / `ri.VKimp_Init` through their respective vtables; engine-side functions create the window and call `IN_Init()` directly (no cross-DLL vtable hop for input init).

**Architecture (Reform-γ'):** Full Quake3e topology: single `SDL_window` global, one `sdl_glimp.c` TU compiled engine-side only, OpenGL slot marked as deprecated (will be removed after Vulkan reaches RTCW feature parity per `[[project_portfolio_vision]]`). All three Q2/Q3 sub-decisions (per session 2026-06-11):

- **Q1:** Surgical revert of T3+T4 commits; keep T1 (`qboolean vulkan` refactor) and T2 (`VKimp_Init` body) as foundation for γ'.
- **Q2:** Full γ' + OpenGL deprecated comment block (architectural-correct per portfolio vision).
- **Q3:** `IN_Init` signature returns to Quake3e form `void IN_Init(void)`; all 4-5 call sites updated.

**Tech Stack:** C99, SDL3, Vulkan 1.4 + MoltenVK, RTCW engine on macOS arm64. No unit-test framework; verification is `make + run + grep log`.

**Gating prerequisites:**
- `REALRTCW_ALLOW_VENDOR_EDIT=1` exported (vendor edits in `code/renderervk/`).
- `~/VulkanSDK/1.4.350.0/setup-env.sh` available for Vulkan build.
- Baseline state: `7a805b3` (post-θ' LANDED state); plan begins by reverting parts.

**Out-of-scope (deferred):**
- `CL_SetScaling` NULL-slot crash (next iter after γ' lands).
- `sdl_gamma.c` migration engine-side (keep per-renderer for now; gamma management is renderer-coupled in SDL3).
- ABI versioning, capability negotiation (Phase 3+ portfolio work — see `[[project_portfolio_vision]]`).

---

## File Structure

| File | Action | Post-γ' responsibility |
|---|---|---|
| `code/sdl/sdl_glimp.c` | move into engine binary (Q3OBJ); refactor includes & cvar registration; both `GLimp_Init` and `VKimp_Init` live here engine-side | window creation for both backends, branched on `qboolean vulkan` |
| `code/sdl/sdl_input.c` | drop duplicate `SDL_window` definition; change `IN_Init` signature to `void IN_Init(void)`; include new `sdl_glw.h` | engine input + reads single engine-side `SDL_window` |
| `code/sdl/sdl_glw.h` | **new file** | single `extern SDL_Window *SDL_window` declaration shared by `sdl_glimp.c` + `sdl_input.c` |
| `Makefile` | move `sdl_glimp.o` from Q3ROBJ/Q3VKOBJ → Q3OBJ; remove per-target `BUILD_RENDERER_VULKAN` CFLAGS for `sdl_glimp.o` (now in engine which already gets the define via cl_main.o rule); add `BUILD_RENDERER_VULKAN` define to engine sdl_glimp.o build | build-system reflects single-source `sdl_glimp.c` |
| `code/client/cl_main.c` | restore `vk_ri.VKimp_Init = VKimp_Init;` wiring (was deleted in T4); update `ri.IN_Init` slot — see below | engine-side install both `GLimp_Init` and `VKimp_Init` slots |
| `code/client/cl_refvulkan.c` | restore `vk_ri.VKimp_Init = VKimp_Init` (or call into engine-side `VKimp_Init`); remove the "intentionally NULL" doc entry; new sdl_glimp.c's `VKimp_Init` is now THE engine-side function | the translator wires engine-side `VKimp_Init` as the slot's target |
| `code/renderervk/realrtcw_vk_window_bridge.c` | keep; renderer DLL still uses its own copies of `r_mode` / `r_fullscreen` / etc. globals; `RealRTCW_VkBridgeInit` call site moves from `GLimp_Init` (engine-side now) into the Vulkan DLL's `tr_init.c` (vendor-edit) | renderer-DLL-internal cvar pointer storage; populated independently when DLL loads |
| `code/renderervk/tr_init.c` | restore `ri.VKimp_Init(&glConfig)` call (T3 revert); ADD a single call to `RealRTCW_VkBridgeInit()` near top of `R_Register` so renderer-DLL cvar pointers get populated | vendored Q3e renderer with 1-line bridge-init kickoff |
| `notes/decisions/2026-06-10-m4-window-ownership-model.md` | rewrite: θ' archived as "attempted, hit dual-struct landmine"; γ' is now **landed** decision; §5 platform-layer ideal preserved | authoritative decision note reflecting γ'-LANDED state |

---

### Task 0: Revert T3 + T4 (preparatory cleanup)

**Files:**
- Modify (via revert): `code/renderervk/tr_init.c`, `code/client/cl_refvulkan.c`

- [ ] **Step 1: Verify current HEAD and ensure clean working tree**

Run:
```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
git status
git log --oneline -8
```
Expected: HEAD at `7a805b3 docs(notes): mark theta' window-init as landed for M4`. Working tree clean (no unstaged changes).

- [ ] **Step 2: Revert T4 (delete engine-side stub)**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
git revert --no-edit bd1b06b
```

Expected: revert commit applied; `code/client/cl_refvulkan.c` has `vk_VKimp_Init` and `vk_VKimp_Shutdown` stub functions restored, plus the `vk_ri.VKimp_Init = vk_VKimp_Init;` / `vk_ri.VKimp_Shutdown = vk_VKimp_Shutdown;` wirings restored, plus the forward declarations at `:104-105`. The "intentionally NULL" list entries for VKimp_Init/Shutdown are removed (back to old state).

- [ ] **Step 3: Revert T3 (vendor-edit tr_init.c)**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
export REALRTCW_ALLOW_VENDOR_EDIT=1
git revert --no-edit 57aedc4
```

Expected: `code/renderervk/tr_init.c` extern decls removed (was added at `:24-35`); call sites restored to `ri.VKimp_Init(&glConfig)` and `if (ri.VKimp_Shutdown) ri.VKimp_Shutdown(...)`.

- [ ] **Step 4: Verify build still passes (T1+T2 alone should compile)**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
source ~/VulkanSDK/1.4.350.0/setup-env.sh
export REALRTCW_ALLOW_VENDOR_EDIT=1
make ARCH=arm64 USE_RENDERER_DLOPEN=1 BUILD_RENDERER_VULKAN=1 USE_OPENAL=1 USE_INTERNAL_LIBS=0 -j8 -k 2>&1 | grep -E "error:|undefined reference" | head
```
Expected: zero `error:` and zero `undefined reference`. (At this point: T1's qboolean-vulkan flag is in place, T2's VKimp_Init is defined but now there's NO caller — orphan function. Should compile clean as `static` would prevent the warning but it's not static. Acceptable "unused function" warning if it appears.)

- [ ] **Step 5: Record HEAD SHA for the post-revert baseline**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
git rev-parse HEAD
```

Note this SHA — it's the baseline for Task 1.

---

### Task 1: Create `code/sdl/sdl_glw.h` — single-source declaration for `SDL_window`

**Files:**
- Create: `code/sdl/sdl_glw.h`

Mirrors `refs/Quake3e/code/sdl/sdl_glw.h:52` — the header that lets both `sdl_glimp.c` and `sdl_input.c` (and any other engine TU needing it) share one `SDL_window` global without duplicate definitions.

- [ ] **Step 1: Write the header**

Create `/Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt/code/sdl/sdl_glw.h` with:

```c
/*
===========================================================================
RealRTCW source code is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as published
by the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
===========================================================================
*/
/*
 * Shared single-source declaration of SDL_window, the engine-resident
 * SDL_Window pointer. Defined exactly once in code/sdl/sdl_glimp.c.
 * Consumed by sdl_input.c and any other engine TU that needs to query
 * the current main window.
 *
 * γ' migration (notes/decisions/2026-06-10-m4-window-ownership-model.md):
 * before γ', SDL_window had two definitions — one in each renderer DLL's
 * sdl_glimp.c, plus a third in sdl_input.c. This caused subtle
 * cross-globals bugs (M3.5 dealt with several). After γ', a single
 * engine-side SDL_window is the source of truth; this header is how
 * other engine TUs see it.
 */

#ifndef __SDL_GLW_H__
#define __SDL_GLW_H__

#ifdef USE_LOCAL_HEADERS
#	include "SDL3/SDL.h"
#else
#	include <SDL3/SDL.h>
#endif

extern SDL_Window *SDL_window;

#endif /* __SDL_GLW_H__ */
```

- [ ] **Step 2: Verify no immediate breakage (file just sits there until included)**

```bash
ls -la /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt/code/sdl/sdl_glw.h
```

Expected: file exists, ~30 lines. No build verification needed at this step — nothing includes it yet.

- [ ] **Step 3: Commit**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
git add code/sdl/sdl_glw.h
git commit -m "feat(sdl): add sdl_glw.h header for single-source SDL_window declaration

Mirrors Quake3e (refs/Quake3e/code/sdl/sdl_glw.h:52). Allows engine-side
sdl_glimp.c and sdl_input.c to share one SDL_window global. Used by the
upcoming gamma' migration where sdl_glimp.c moves engine-side and the
duplicate SDL_window in sdl_input.c is removed."
```

---

### Task 2a: Renderer-internal split — extract `qgl*` probing into new `code/renderer/r_glimp.c`

**Why this task exists:** During γ' Task 2 implementation attempt, recon revealed RealRTCW's `sdl_glimp.c` (1487 lines) embeds ~600 lines of renderer-internal GL probing that Quake3e keeps separate. Moving sdl_glimp.c engine-side AS-IS would break the OpenGL renderer DLL link (other renderer TUs extern the `qgl*` pointers defined inline in sdl_glimp.c). Task 2a slims sdl_glimp.c first by extracting all renderer-internal GL code into a new `code/renderer/r_glimp.c` TU that lives in the OpenGL renderer DLL only. Then Task 2 can move the slim sdl_glimp.c engine-side cleanly.

**Files:**
- Create: `code/renderer/r_glimp.h` (header with `GLimp_RendererInit` / `GLimp_RendererShutdown` declarations)
- Create: `code/renderer/r_glimp.c` (~600 lines extracted from sdl_glimp.c)
- Modify: `code/sdl/sdl_glimp.c` (delete the extracted ~600 lines — leaves ~900 lines of pure platform glue)
- Modify: `code/renderer/tr_init.c` (call `GLimp_RendererInit` from `R_Init` after `ri.GLimp_Init` returns; call `GLimp_RendererShutdown` from `R_Shutdown`)
- Modify: `Makefile` (add `$(B)/renderer/r_glimp.o` to Q3ROBJ — Vulkan DLL doesn't need it)

**What stays in `code/sdl/sdl_glimp.c` after Task 2a (~900 lines):**
- Top-level types (`rserr_t`)
- `SDL_window` global, `SDL_glContext` global
- `r_allowSoftwareGL`, `r_sdlDriver`, `r_allowResize`, `r_centerWindow` cvars (engine-side window cvars)
- `GLimp_DetectAvailableModes` function
- `GLimp_SetMode` (keeps SDL_CreateWindow + SDL_GL_CreateContext + GL3-core context probe — Quake3e does context creation engine-side too)
- `GLimp_StartDriverAndSetMode` (with `qboolean vulkan` from T1)
- `GLimp_Init` (slim — no more qglGetString probing; just window+context creation, calls IN_Init at end)
- `VKimp_Init`, `VKimp_Shutdown` (from T2)
- `GLimp_EndFrame`, `GLimp_LogComment`, etc.
- `GLimp_Shutdown`

**What moves to `code/renderer/r_glimp.c` (~600 lines):**
- All `qgl*` function pointer DEFINITIONS (e.g., `qglActiveTextureARB`, `qglClientActiveTextureARB`, etc.) — around lines 85-107 of current sdl_glimp.c
- `GLimp_GetProcAddresses` function (~140 lines, around `sdl_glimp.c:403-507`)
- `GLimp_ClearProcAddresses` function (~30 lines, around `sdl_glimp.c:509-540`)
- `GLimp_InitExtensions` function (~200 lines, around `sdl_glimp.c:1018-1217`)
- The post-window-creation `qglGetString(GL_VENDOR/RENDERER/VERSION)` + `extensions_string` population block from `GLimp_Init` (around `sdl_glimp.c:1280-1320`)
- New entry points wrapping the above: `GLimp_RendererInit(qboolean fixedFunction)` and `GLimp_RendererShutdown(void)`

- [ ] **Step 1: Read RealRTCW's current sdl_glimp.c to identify exact extract boundaries**

Read `code/sdl/sdl_glimp.c` to locate:
- All `qgl*` function pointer definitions (typically around lines 85-107).
- `GLimp_GetProcAddresses` definition (search by name).
- `GLimp_ClearProcAddresses` definition.
- `GLimp_InitExtensions` definition.
- Inside `GLimp_Init`: the trailing block that calls `qglGetString` and `GLimp_InitExtensions` (typically the last ~50 lines of GLimp_Init body).

Capture exact line ranges for each section in your work notes.

- [ ] **Step 2: Create `code/renderer/r_glimp.h`**

```c
/*
===========================================================================
RealRTCW source code is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as published
by the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
===========================================================================
*/
#ifndef __R_GLIMP_H__
#define __R_GLIMP_H__

#include "tr_local.h"

/* γ' Task 2a (notes/decisions/2026-06-10-m4-window-ownership-model.md):
 * Renderer-internal GL state probing. Engine-side sdl_glimp.c creates the
 * SDL window and GL context; this TU populates qgl* function pointers and
 * the renderer-side glConfig string fields, and detects GL extensions.
 *
 * Called from R_Init() after ri.GLimp_Init() returns to the renderer DLL.
 */

/* Probe qgl* pointers, populate glConfig.vendor/renderer/version/extensions
 * strings, detect GL extensions. Returns qfalse if qgl* probe failed. */
qboolean GLimp_RendererInit( qboolean fixedFunction );

/* Clear qgl* pointers. Called from R_Shutdown. */
void GLimp_RendererShutdown( void );

#endif /* __R_GLIMP_H__ */
```

- [ ] **Step 3: Create `code/renderer/r_glimp.c` — extract code from sdl_glimp.c**

Create the file with:
- License header (matching `code/renderer/tr_init.c` style)
- `#include "tr_local.h"` and `#include "r_glimp.h"`
- Move (cut-paste from sdl_glimp.c, then delete from sdl_glimp.c in Step 4): all qgl* function pointer DEFINITIONS, `GLimp_GetProcAddresses`, `GLimp_ClearProcAddresses`, `GLimp_InitExtensions`. Keep their existing `static` qualifiers as they were inside sdl_glimp.c, EXCEPT make `GLimp_GetProcAddresses` and `GLimp_ClearProcAddresses` non-static if needed — they're called only from inside r_glimp.c, so they CAN stay static. `GLimp_InitExtensions` is also internal.
- Define new public entry points:

```c
qboolean GLimp_RendererInit( qboolean fixedFunction )
{
    const char *renderer;

    if ( !GLimp_GetProcAddresses( fixedFunction ) ) {
        ri.Printf( PRINT_ALL, "GLimp_RendererInit: GLimp_GetProcAddresses failed\n" );
        return qfalse;
    }

    renderer = (const char *)qglGetString( GL_RENDERER );
    if ( !renderer || (strstr(renderer, "Software Renderer") || strstr(renderer, "Software Rasterizer")) ) {
        if ( renderer ) {
            ri.Printf( PRINT_ALL, "GL_RENDERER is %s, rejecting context\n", renderer );
        }
        GLimp_ClearProcAddresses();
        return qfalse;
    }

    /* Populate strings (was in sdl_glimp.c GLimp_Init around :1280-1320) */
    Q_strncpyz( glConfig.vendor_string, (char *)qglGetString(GL_VENDOR), sizeof(glConfig.vendor_string) );
    Q_strncpyz( glConfig.renderer_string, (char *)qglGetString(GL_RENDERER), sizeof(glConfig.renderer_string) );
    if ( *glConfig.renderer_string && glConfig.renderer_string[strlen(glConfig.renderer_string) - 1] == '\n' ) {
        glConfig.renderer_string[strlen(glConfig.renderer_string) - 1] = 0;
    }
    Q_strncpyz( glConfig.version_string, (char *)qglGetString(GL_VERSION), sizeof(glConfig.version_string) );

#ifndef USE_OPENGLES
    /* Manually create extension list if using OpenGL 3 */
    if ( qglGetStringi ) {
        int i, numExtensions, extensionLength, listLength;
        const char *extension;
        qglGetIntegerv( GL_NUM_EXTENSIONS, &numExtensions );
        listLength = 0;
        for ( i = 0; i < numExtensions; i++ ) {
            extension = (char *)qglGetStringi( GL_EXTENSIONS, i );
            extensionLength = strlen( extension );
            if ( (listLength + extensionLength + 1) >= sizeof(glConfig.extensions_string) ) {
                break;
            }
            if ( i > 0 ) {
                Q_strcat( glConfig.extensions_string, sizeof(glConfig.extensions_string), " " );
                listLength++;
            }
            Q_strcat( glConfig.extensions_string, sizeof(glConfig.extensions_string), extension );
            listLength += extensionLength;
        }
    } else
#endif
    {
        Q_strncpyz( glConfig.extensions_string, (char *)qglGetString(GL_EXTENSIONS), sizeof(glConfig.extensions_string) );
    }

    GLimp_InitExtensions( fixedFunction );
    return qtrue;
}

void GLimp_RendererShutdown( void )
{
    GLimp_ClearProcAddresses();
}
```

(Use the actual code blocks from sdl_glimp.c — copy verbatim. The above is a structural sketch.)

- [ ] **Step 4: Delete extracted code from `code/sdl/sdl_glimp.c`**

Remove from sdl_glimp.c:
- All qgl* function pointer definitions (the ones moved in Step 3).
- `GLimp_GetProcAddresses` function body.
- `GLimp_ClearProcAddresses` function body.
- `GLimp_InitExtensions` function body.
- The trailing block in `GLimp_Init` (lines ~1280-1320 of current) that does qglGetString + extensions_string + calls GLimp_InitExtensions. Replace with a comment: `/* GL state probing moved to code/renderer/r_glimp.c (γ' Task 2a) — renderer's R_Init calls GLimp_RendererInit() after this function returns. */`.

Also remove the call to `GLimp_GetProcAddresses` and `qglGetString(GL_RENDERER)` from inside `GLimp_SetMode`'s 3.2-core context probe loop (~`sdl_glimp.c:830-860`). The 3.2-core PROBE stays — we still try to get a 3.2 core context — but the qgl* function pointer population happens later in `GLimp_RendererInit`. So just remove the calls; the loop tries `SDL_GL_CreateContext`, succeeds or fails based on context creation alone, doesn't probe GL state.

Wait — actually the loop USES `GLimp_GetProcAddresses` to detect if the new context is software. We need to preserve that detection. **Decision: move the software-renderer rejection logic to `GLimp_RendererInit` instead.** The engine-side `GLimp_SetMode` just tries to create a 3.2 core context, and uses it if SDL_GL_CreateContext succeeds. If the context turns out to be software (only detectable after qglGetString(GL_RENDERER)), the renderer's `GLimp_RendererInit` rejects it and returns qfalse, and the OpenGL renderer reports an error at startup. The fallback-to-default-context loop in current sdl_glimp.c can be simplified or moved.

For SAFETY (preserve current behavior): keep the rejection-loop logic inside sdl_glimp.c BUT make sdl_glimp.c call into r_glimp.c via a NEW function `qboolean GLimp_ProbeContextIsAcceptable(qboolean fixedFunction)`. This function lives in r_glimp.c, returns qtrue if the just-created context has hardware accel.

Actually this is getting complex. **Simpler decision:** in Task 2a, delete the GL-state-probe-during-context-retry loop from sdl_glimp.c. Engine-side just creates ONE context (3.2 core if possible, fall back to default), without checking if it's hardware. r_glimp.c's GLimp_RendererInit later detects software-renderer and reports error.

**Net behavior change:** before: software renderer detection during context creation, retry loop. After: software renderer detection after, no retry. **Trade-off:** simpler code, slightly worse handling of weird drivers, but Apple Silicon Metal-backed GL is hardware-only — software rasterizer detection is academic. Acceptable for portfolio direction (clean architecture > preserved-quirk).

DOCUMENT this behavior change in the commit message.

- [ ] **Step 5: Update `code/renderer/tr_init.c` — call GLimp_RendererInit / Shutdown**

Locate the OpenGL renderer's `R_Init` function in `code/renderer/tr_init.c`. After the call to `InitOpenGL()` (or equivalent — search by symbol name in the file), add:

```c
#include "r_glimp.h"  /* near top of file with other includes */

/* ... inside R_Init, after InitOpenGL/GLimp_Init returns ... */
if ( !GLimp_RendererInit( fixedFunction ) ) {
    ri.Error( ERR_FATAL, "GLimp_RendererInit failed" );
}
```

Locate `R_Shutdown` in the same file. Before the `ri.GLimp_Shutdown(...)` call, add:

```c
GLimp_RendererShutdown();
```

Note: `fixedFunction` argument propagation — `R_Init` should already have `fixedFunction` available (it's part of the OpenGL renderer's init contract). If not directly available, derive from the relevant cvar.

- [ ] **Step 6: Update Makefile — add `$(B)/renderer/r_glimp.o` to Q3ROBJ**

Locate the Q3ROBJ list in the Makefile (near `Makefile:2123` where `sdl_glimp.o` is listed). Add a line:

```makefile
Q3ROBJ += $(B)/renderer/r_glimp.o
```

Do NOT add to Q3VKOBJ. Vulkan DLL doesn't use qgl*.

- [ ] **Step 7: Build BOTH renderers + engine**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
source ~/VulkanSDK/1.4.350.0/setup-env.sh
export REALRTCW_ALLOW_VENDOR_EDIT=1
make ARCH=arm64 USE_RENDERER_DLOPEN=1 BUILD_RENDERER_VULKAN=1 USE_OPENAL=1 USE_INTERNAL_LIBS=0 -j8 -k 2>&1 | tee /tmp/m4-gamma-task2a-build.log | tail -40
```

Then:
```bash
grep -E "error:|undefined reference" /tmp/m4-gamma-task2a-build.log | head -30
```

Expected: zero `error:` and zero `undefined reference`.

Likely issues to triage if errors appear:
- **Undefined reference to `qglActiveTextureARB` (etc.)** in OpenGL renderer DLL: means r_glimp.o wasn't compiled — check Step 6 Makefile edit.
- **Undefined reference in Vulkan DLL** to any `qgl*`: should not happen — Vulkan DLL doesn't touch GL. If it does, find caller and `#ifdef BUILD_RENDERER_VULKAN`-gate it out.
- **Duplicate symbol `qglActiveTextureARB`** in OpenGL DLL link: means defs weren't fully removed from sdl_glimp.c — re-check Step 4.

- [ ] **Step 8: OpenGL smoke run to verify renderer still works**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
./build/release-darwin-arm64-nosteam/RealRTCW.arm64 +set cl_renderer opengl1 +set developer 1 > /tmp/m4-gamma-task2a-gl-stdout.log 2>&1 &
APP_PID=$!
sleep 10
kill -INT $APP_PID 2>/dev/null || true
sleep 3
kill -KILL $APP_PID 2>/dev/null || true
```

Then grep:
```bash
grep -nE "GLimp_Init|GL_RENDERER|GL_VENDOR|GLimp_RendererInit|Segmentation|main menu|extensions_string" /tmp/m4-gamma-task2a-gl-stdout.log | head -20
```

Expected: `GL_RENDERER:` line still present (now printed from r_glimp.c via Com_Printf). Main menu reached. No segfault.

- [ ] **Step 9: Commit**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
git add code/renderer/r_glimp.h code/renderer/r_glimp.c code/sdl/sdl_glimp.c code/renderer/tr_init.c Makefile
git commit -m "refactor(renderer): split renderer-internal GL probing into r_glimp.c (γ' precursor)

Extracts qgl* function pointer definitions, GLimp_GetProcAddresses,
GLimp_ClearProcAddresses, GLimp_InitExtensions, and the post-window
qglGetString/extensions_string probe block from code/sdl/sdl_glimp.c
into a new renderer-internal TU code/renderer/r_glimp.c. New entry
points GLimp_RendererInit / GLimp_RendererShutdown wrap the renderer-
side probing; called from R_Init / R_Shutdown after engine-side
GLimp_Init creates the window+context.

This slims sdl_glimp.c (1487 -> ~900 lines) to a pure platform-glue TU,
clearing the path for Task 2 (move sdl_glimp.c engine-side). Matches
Quake3e topology: engine-side platform glue, renderer-side GL state.

Behavior change: software-renderer detection now happens at R_Init
time (post-context-creation) via GLimp_RendererInit, not during the
context-retry loop inside GLimp_SetMode. Apple Silicon Metal-GL is
hardware-only so this is academic for our platform target.

Vulkan DLL unchanged (r_glimp.o is Q3ROBJ-only)."
```

---

### Task 2: γ' Big Move — `sdl_glimp.c` engine-side + `IN_Init` signature change

**Prerequisites:** Task 2a complete. `sdl_glimp.c` is now ~900 lines of pure platform glue with no renderer-internal `qgl*` definitions or extension detection. The Big Move is now mechanically clean.

**Files:**
- Modify: `Makefile` (move `$(B)/.../sdl_glimp.o` between objs; drop per-target define)
- Modify: `code/sdl/sdl_glimp.c` (extensive: includes, cvar registration, IN_Init direct call, OpenGL deprecated comment block)
- Modify: `code/sdl/sdl_input.c` (drop dup `SDL_window`, change `IN_Init` sig, include `sdl_glw.h`)
- Modify: `code/qcommon/qcommon.h` (update `IN_Init` declaration to `void IN_Init(void)`)
- Modify: `code/null/null_input.c` (no-op already matches `void IN_Init(void)`, just verify)
- Modify: `code/client/cl_main.c` (update `ri.IN_Init` slot — see Step 9)
- Modify: `code/renderer/tr_public.h` (update SMALL refImport_t `IN_Init` signature to `void (*IN_Init)(void)`)
- Modify: `code/renderervk/tr_init.c` (vendor-edit: add `RealRTCW_VkBridgeInit()` call in `R_Register`)

This is THE migration task. One commit (or one small fixup follow-up). High concentration of changes; subagent should read both reference points (`refs/Quake3e/code/sdl/sdl_glimp.c:694-750` for VKimp_Init pattern; `refs/Quake3e/code/sdl/sdl_glimp.c:GLimp_Init` for GL pattern) before editing.

- [ ] **Step 1: Read Quake3e reference for GL context creation split**

Quake3e splits GL context creation between engine-side sdl_glimp.c (window creation) and renderer-side (qgl* function pointer probing via `GLimp_GetProcAddresses` which stays renderer-internal). Read `refs/Quake3e/code/sdl/sdl_glimp.c:GLimp_Init` to see the exact split.

Specifically figure out:
- Does engine-side `GLimp_Init` create the SDL_GL_Context, or does the renderer DLL?
- Where does `qglGetString(GL_RENDERER)` get called?
- How is `GLimp_GetProcAddresses` (renderer-internal helper) handled if `GLimp_Init` lives engine-side?

The answer determines how to split `code/sdl/sdl_glimp.c:547-997` (GLimp_SetMode + GLimp_StartDriverAndSetMode + GLimp_Init body) between engine-side and renderer-internal.

If Quake3e's engine-side `GLimp_Init` ALSO does GL context creation: that's fine, engine-side has linked `SDL_GL_*` helpers. Renderer DLL receives context handle via SDL's current-context-getter when it needs it.

If Quake3e's engine-side `GLimp_Init` only does window+swap-attrs, leaving context creation to renderer: more complex, would need to refactor `GLimp_GetProcAddresses` flow.

**Capture finding in a 2-3 sentence note** in your report; the rest of Task 2 depends on this answer.

- [ ] **Step 2: Update SMALL `refimport_t` IN_Init signature**

Edit `code/renderer/tr_public.h:187`:

```c
void	(*IN_Init)( void *windowData );
```

to:

```c
void	(*IN_Init)( void );
```

This is the engine-OpenGL renderer's vtable. The slot was for cross-DLL handoff; γ' makes it engine-resident with no arg.

- [ ] **Step 3: Update engine-side IN_Init prototype**

Edit `code/qcommon/qcommon.h:1108`:

```c
void IN_Init( void *windowData );
```

to:

```c
void IN_Init( void );
```

- [ ] **Step 4: Update `IN_Init` definition in `code/sdl/sdl_input.c`**

Edit `code/sdl/sdl_input.c:1235-1245`. Change:

```c
void IN_Init( void *windowData )
{
	int appState;

	if( !SDL_WasInit( SDL_INIT_VIDEO ) )
	{
		Com_Error( ERR_FATAL, "IN_Init called before SDL_Init( SDL_INIT_VIDEO )" );
		return;
	}

	SDL_window = (SDL_Window *)windowData;
```

to:

```c
void IN_Init( void )
{
	int appState;

	if( !SDL_WasInit( SDL_INIT_VIDEO ) )
	{
		Com_Error( ERR_FATAL, "IN_Init called before SDL_Init( SDL_INIT_VIDEO )" );
		return;
	}

	/* SDL_window is a single engine-side global (γ' migration); set by
	 * sdl_glimp.c when it creates the window. No cross-DLL handoff. */
```

- [ ] **Step 5: Remove duplicate `SDL_window` definition in `sdl_input.c`**

Edit `code/sdl/sdl_input.c:60`. Delete the line:

```c
SDL_Window *SDL_window = NULL;  /* Exposed externally for cl_refvulkan.c (M3.5 — Vulkan adapter). Populated by IN_Init when renderer hands us its window pointer. */
```

(Or replace with a comment pointing at sdl_glw.h.)

And add `#include "sdl_glw.h"` to the includes block (after the SDL include block, around line 29).

- [ ] **Step 6: Update `IN_Init` call sites**

Three call sites:
1. `code/sdl/sdl_input.c:1296` — inside `IN_Restart`. Change `IN_Init( SDL_window );` to `IN_Init();`
2. `code/sdl/sdl_glimp.c:1336` — inside `GLimp_Init`. Change `ri.IN_Init( SDL_window );` to `IN_Init();` (note: removed `ri.` prefix — sdl_glimp.c now engine-side, calls engine-local function directly).
3. `code/sdl/sdl_glimp.c:1411` — inside `VKimp_Init` (added in T2). Change `ri.IN_Init( SDL_window );` to `IN_Init();`.

- [ ] **Step 7: Refactor `sdl_glimp.c` includes (engine-side compilation)**

Current `code/sdl/sdl_glimp.c:34`:

```c
#include "../renderer/tr_local.h"
```

This pulls in renderer-DLL-specific definitions (SMALL refimport_t, qgl* externs, R_MODE_FALLBACK, etc.). After γ', sdl_glimp.c is engine-side and shouldn't depend on renderer headers.

Replace with engine-side includes:

```c
#include "../client/client.h"
#include "../sys/sys_local.h"
#include "sdl_glw.h"
```

This mirrors `code/sdl/sdl_input.c`'s include pattern. Will reveal compile errors for any renderer-specific symbol used in sdl_glimp.c — fix each in subsequent steps.

- [ ] **Step 8: Resolve compile errors from include change**

Expected breakage after Step 7:
- `qglGetString`, `qglClearColor`, `qglClear` — renderer-internal, can't be called engine-side. **Strategy:** gate these calls in `if ( !vulkan )` branch and either remove them entirely (engine-side can't probe GL state) or guard with `#ifdef BUILD_RENDERER_OPENGL_INTREE` (legacy define for when GL renderer was inline). Recommended: REMOVE them — engine-side sdl_glimp.c is platform glue, not renderer state probe.
- `SDL_GL_CreateContext` / `SDL_GL_DestroyContext` — these are SDL functions, available engine-side. Keep.
- `GLimp_GetProcAddresses` / `GLimp_ClearProcAddresses` — renderer-internal. **Strategy:** move these calls to renderer-side (renderer's R_Init does the qgl* probing). Engine-side just creates the SDL window + GL context.
- `R_MODE_FALLBACK` — define this engine-side near top of sdl_glimp.c (was in renderer's tr_local.h). Quake3e does the same.
- `r_mode`, `r_fullscreen`, `r_noborder`, `r_colorbits`, `r_depthbits`, `r_stencilbits`, `r_stereoEnabled`, `r_allowSoftwareGL`, `r_centerWindow`, `r_allowResize`, `r_sdlDriver`, `r_ext_multisample` — currently expected as externs from `tr_local.h`. Engine-side: declare them as `static cvar_t *` globals in sdl_glimp.c, register via `Cvar_Get` (engine's own, not ri-prefixed) in `GLimp_Init` / `VKimp_Init`.
- `displayAspect`, `haveClampToEdge` — same; declare engine-side, populate in `GLimp_SetMode`.
- `glConfig` — was an extern from renderer's tr_local.h. Engine-side, can't access renderer's `glConfig` directly. **Strategy:** for the populated fields (vidWidth, vidHeight, windowAspect, isFullscreen), engine-side `GLimp_SetMode` returns these via the `glconfig_t *config` arg already passed in, OR via global engine-side `glConfig` if there is one. Quake3e has engine-side `glconfig_t glConfig;` in `sdl_glimp.c`. Adopt same.

Concrete: add to top of engine-side `sdl_glimp.c`, after the include block:

```c
#define R_MODE_FALLBACK 3   /* 640 x 480 */

static cvar_t *r_allowSoftwareGL;
static cvar_t *r_sdlDriver;
static cvar_t *r_allowResize;
static cvar_t *r_centerWindow;
static cvar_t *r_mode;
static cvar_t *r_fullscreen;
static cvar_t *r_noborder;
static cvar_t *r_colorbits;
static cvar_t *r_depthbits;
static cvar_t *r_stencilbits;
static cvar_t *r_stereoEnabled;
static cvar_t *r_ext_multisample;

static float    displayAspect;          /* set in GLimp_SetMode */
static qboolean haveClampToEdge = qtrue;

glconfig_t glConfig;   /* engine-side single source */
```

- [ ] **Step 9: Replace `ri.Cvar_Get` / `ri.Printf` etc. with engine-side equivalents in `sdl_glimp.c`**

Engine-side `sdl_glimp.c` no longer goes through `ri.*` vtable. Replace:
- `ri.Cvar_Get(...)` → `Cvar_Get(...)` (engine's own function)
- `ri.Cvar_Set(...)` → `Cvar_Set(...)`
- `ri.Cvar_VariableIntegerValue(...)` → `Cvar_VariableIntegerValue(...)`
- `ri.Printf(...)` → `Com_Printf(...)` (most cases) or `Com_DPrintf(...)` (for PRINT_DEVELOPER)
- `ri.Error(...)` → `Com_Error(...)`
- `ri.Sys_GLimpInit()` → `Sys_GLimpInit()` (engine has it directly)
- `ri.Sys_GLimpSafeInit()` → `Sys_GLimpSafeInit()`
- `ri.IN_Init(...)` → `IN_Init()` (Step 6 already did this)

Specifically for `Com_DPrintf` vs `Com_Printf`: ri.Printf takes a `printParm_t` first arg (PRINT_ALL, PRINT_WARNING, PRINT_DEVELOPER). Map:
- `ri.Printf( PRINT_ALL, ... )` → `Com_Printf( ... )`
- `ri.Printf( PRINT_DEVELOPER, ... )` → `Com_DPrintf( ... )`
- `ri.Printf( PRINT_WARNING, ... )` → `Com_Printf( "WARNING: " ... )` (engine has no direct PRINT_WARNING; warning prefix as string)

This affects ~10-15 lines in `GLimp_SetMode`, `GLimp_StartDriverAndSetMode`, `GLimp_Init`, `VKimp_Init`. Subagent: grep `ri\.` in sdl_glimp.c and replace each per the mapping above.

- [ ] **Step 10: Register cvars in `GLimp_Init` / `VKimp_Init`**

Replace the `RealRTCW_VkBridgeInit()` call in `VKimp_Init` (was line 1355) with direct `Cvar_Get` calls — same set as bridge registered. Also ensure `GLimp_Init` registers all the cvars sdl_glimp.c needs.

Both functions should start with the same cvar registration block. Extract it to a static helper:

```c
static void GLimp_RegisterCvars( void )
{
	r_allowSoftwareGL = Cvar_Get( "r_allowSoftwareGL", "0", CVAR_LATCH );
	r_sdlDriver       = Cvar_Get( "r_sdlDriver", "", CVAR_ROM );
	r_allowResize     = Cvar_Get( "r_allowResize", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_centerWindow    = Cvar_Get( "r_centerWindow", "0", CVAR_ARCHIVE | CVAR_LATCH );

	r_mode            = Cvar_Get( "r_mode",          "-2", CVAR_ARCHIVE | CVAR_LATCH );
	r_fullscreen      = Cvar_Get( "r_fullscreen",    "1",  CVAR_ARCHIVE | CVAR_LATCH );
	r_noborder        = Cvar_Get( "r_noborder",      "0",  CVAR_ARCHIVE | CVAR_LATCH );
	r_colorbits       = Cvar_Get( "r_colorbits",     "0",  CVAR_ARCHIVE | CVAR_LATCH );
	r_depthbits       = Cvar_Get( "r_depthbits",     "0",  CVAR_ARCHIVE | CVAR_LATCH );
	r_stencilbits     = Cvar_Get( "r_stencilbits",   "0",  CVAR_ARCHIVE | CVAR_LATCH );
	r_stereoEnabled   = Cvar_Get( "r_stereoEnabled", "0",  CVAR_ARCHIVE | CVAR_LATCH );
	r_ext_multisample = Cvar_Get( "r_ext_multisample", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
}
```

Call it as the first line of `GLimp_Init` AND `VKimp_Init`.

- [ ] **Step 11: Port `R_GetModeInfo` engine-side**

`code/renderervk/realrtcw_vk_window_bridge.c:96-165` has `r_vidModes[]` + `R_GetModeInfo`. Engine-side sdl_glimp.c needs this. Two options:
- (A) Port inline into `sdl_glimp.c` (~70 lines).
- (B) Create new file `code/sdl/sdl_vidmodes.c` with the table + function, link engine-side.

Recommended: (A) — minimal file count, same TU that uses it. Insert `r_vidModes[]` + `R_GetModeInfo` just before `GLimp_SetMode` definition. Make them `static`.

The `R_GetModeInfo` ported version: keep verbatim from bridge, just remove the comment block at top (about iortcw porting) since context is now different.

- [ ] **Step 12: Move `RealRTCW_VkBridgeInit()` call site to Vulkan DLL's `tr_init.c`**

The renderer-DLL side still needs its `r_mode` / `r_fullscreen` / etc. global pointers populated. Add a call to `RealRTCW_VkBridgeInit()` near the top of `R_Register()` in `code/renderervk/tr_init.c` (around line 1498, before any cvar registration in R_Register).

Vendor-edit; needs `REALRTCW_ALLOW_VENDOR_EDIT=1`.

Insert at the top of `R_Register`:

```c
static void R_Register( void )
{
#ifdef BUILD_RENDERER_VULKAN
	/* γ' migration: window cvars (r_mode, r_fullscreen, etc.) are now
	 * registered engine-side by sdl_glimp.c. The bridge populates the
	 * renderer-DLL-side cvar_t* globals so internal renderervk code that
	 * dereferences them keeps working. See
	 * notes/decisions/2026-06-10-m4-window-ownership-model.md §4. */
	RealRTCW_VkBridgeInit();
#endif
	/* ... existing R_Register body ... */
```

(The existing R_Register body starts at `code/renderervk/tr_init.c:1500` per earlier grep.)

- [ ] **Step 13: Update Makefile — move sdl_glimp.o into Q3OBJ**

Edit Makefile to relocate `sdl_glimp.o`:

1. **Delete** lines `Makefile:2123` and `Makefile:2177` (the `Q3ROBJ += $(B)/renderer/sdl_glimp.o` and `Q3VKOBJ += $(B)/rendv/sdl_glimp.o` lines).
2. **Delete** lines `Makefile:3090` and adjacent — the per-target `override CFLAGS += -DBUILD_RENDERER_VULKAN` rules for `sdl_glimp.o` and `sdl_gamma.o`. (Keep `sdl_gamma.o` lines — see Step 14.)
3. **Add** `$(B)/client/sdl_glimp.o` to Q3OBJ. Place near the existing `$(B)/client/sdl_input.o \` line (was `Makefile:2066`). Add the line:

```makefile
  $(B)/client/sdl_glimp.o \
```

- [ ] **Step 14: Decide on sdl_gamma.c (Decision: leave per-renderer for now)**

Per "out-of-scope" in the plan goal: `sdl_gamma.c` stays per-renderer (still in Q3ROBJ + Q3VKOBJ). It's gamma management which is renderer-coupled in SDL3 patterns. Future cleanup deferred.

**Do not** move sdl_gamma.o to Q3OBJ. Keep `$(B)/renderer/sdl_gamma.o` and `$(B)/rendv/sdl_gamma.o` (and their per-target CFLAGS rule if present) intact.

- [ ] **Step 15: Engine binary needs `BUILD_RENDERER_VULKAN` define so sdl_glimp.c compiles VKimp_Init**

`code/sdl/sdl_glimp.c`'s `VKimp_Init` is wrapped in `#ifdef BUILD_RENDERER_VULKAN` (from T2). The engine binary needs that define when compiling `code/sdl/sdl_glimp.c`. Check `Makefile:3099-3100`:

```makefile
ifeq ($(BUILD_RENDERER_VULKAN),1)
$(B)/client/cl_main.o: override CFLAGS += -DBUILD_RENDERER_VULKAN
```

Add `$(B)/client/sdl_glimp.o` to the same rule:

```makefile
ifeq ($(BUILD_RENDERER_VULKAN),1)
$(B)/client/cl_main.o: override CFLAGS += -DBUILD_RENDERER_VULKAN
$(B)/client/sdl_glimp.o: override CFLAGS += -DBUILD_RENDERER_VULKAN
endif
```

- [ ] **Step 16: Restore `ri.IN_Init` slot wiring update in `cl_main.c`**

After Step 2 (`refimport_t.IN_Init` signature change to `void (*IN_Init)(void)`), the wiring at `code/client/cl_main.c:3476` needs updating:

```c
ri.IN_Init = IN_Init;
```

This still works (same name, just different signature). Verify the compiler accepts the function pointer. If there's a cast or warning, add explicit cast or update.

For Vulkan path: the BIG refImport_t in `code/renderercommon/tr_public.h` doesn't have `IN_Init` slot (per Phase 1 root cause). With γ', renderer DLLs don't NEED `ri.IN_Init` slot anymore — engine-side `GLimp_Init` / `VKimp_Init` call `IN_Init()` directly. So leave BIG refImport_t alone — the OpenGL `rimp.IN_Init` slot just becomes legacy/unused after γ' but doesn't break anything.

(Optionally: in a later sweep, remove `IN_Init` slot from SMALL refImport_t entirely. Not in this plan.)

- [ ] **Step 17: Build engine + both renderer DLLs**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
source ~/VulkanSDK/1.4.350.0/setup-env.sh
export REALRTCW_ALLOW_VENDOR_EDIT=1
make ARCH=arm64 USE_RENDERER_DLOPEN=1 BUILD_RENDERER_VULKAN=1 USE_OPENAL=1 USE_INTERNAL_LIBS=0 -j8 -k 2>&1 | tee /tmp/m4-gamma-task2-build.log | tail -50
```

Then:

```bash
grep -E "error:|undefined reference" /tmp/m4-gamma-task2-build.log | head -30
```

Expected: zero `error:` and zero `undefined reference`. If errors appear, they're most likely:
- Missing engine-side prototype for a function (e.g., `Sys_GLimpInit` — check `code/sys/sys_local.h:53-54`).
- `glConfig` redefinition (engine-side `glConfig` vs. renderer-side) — wrap engine-side `glConfig` in `#ifdef BUILD_RENDERER_VULKAN` or use a different name like `engine_glConfig`. Actually: engine-side `glConfig` in sdl_glimp.c is only the LOCAL copy the engine TU uses; the renderer DLL has its own `glConfig` in `tr_init.c:36` which is a separate global. Both exist independently because they're in different binaries.

If symbol collision is reported at engine link time (not DLL link), the duplicate definition needs renaming or removal. Diagnose case-by-case.

- [ ] **Step 18: Sanity-check binary structure**

```bash
ls -la /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt/build/release-darwin-arm64-nosteam/RealRTCW.arm64
ls -la /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt/build/release-darwin-arm64-nosteam/renderer_sp_opengl1_arm64.dylib
ls -la /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt/build/release-darwin-arm64-nosteam/renderer_sp_vulkan_arm64.dylib
nm /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt/build/release-darwin-arm64-nosteam/RealRTCW.arm64 | grep -E "_GLimp_Init|_VKimp_Init|_SDL_window|_IN_Init"
```

Expected: timestamps are fresh; `nm` shows `_GLimp_Init`, `_VKimp_Init`, `_IN_Init`, `_SDL_window` symbols in the engine binary (T symbols for defined functions, D/B for globals).

- [ ] **Step 19: Commit**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
git add Makefile code/sdl/sdl_glimp.c code/sdl/sdl_input.c code/qcommon/qcommon.h code/renderer/tr_public.h code/renderervk/tr_init.c code/client/cl_main.c
git commit -m "feat(sdl): gamma' migration - move sdl_glimp.c engine-side, single SDL_window

Migrates code/sdl/sdl_glimp.c from per-renderer-DLL compilation into the
engine binary (Q3OBJ), eliminating the dual-refimport_t struct-view
mismatch that crashed theta' iter 4. Topology now matches Quake3e:
single SDL_window global, one sdl_glimp.c TU, engine-side calls to
IN_Init() with no cross-DLL vtable hop.

Changes:
- sdl_glimp.c includes engine-side headers (client.h, sys_local.h, sdl_glw.h)
  instead of renderer/tr_local.h.
- ri.* calls replaced with engine equivalents (Cvar_Get, Com_Printf, etc).
- RealRTCW_VkBridgeInit() call relocated to renderervk/tr_init.c
  R_Register top — renderer-DLL-side cvar pointers still populated.
- r_vidModes table + R_GetModeInfo ported engine-side (was in bridge).
- IN_Init signature: void IN_Init(void) (was void *windowData). All 3
  call sites updated.
- code/sdl/sdl_input.c: duplicate SDL_window definition removed; now
  includes sdl_glw.h for the single engine-side decl.
- code/renderer/tr_public.h: SMALL refImport_t IN_Init slot signature
  updated for consistency with engine prototype (slot is now legacy,
  unused after gamma' since engine-side calls IN_Init directly).
- Makefile: sdl_glimp.o moved Q3ROBJ/Q3VKOBJ -> Q3OBJ; engine
  cl_main.o + sdl_glimp.o both get BUILD_RENDERER_VULKAN define.

Decision rationale: notes/decisions/2026-06-10-m4-window-ownership-model.md
section 4 (gamma' principled answer)."
```

---

### Task 3: Restore `vk_ri.VKimp_Init` slot wiring in `cl_refvulkan.c`

**Files:**
- Modify: `code/client/cl_refvulkan.c` — restore the wiring that T4 deleted (we already reverted T4 in Task 0, but the wiring still needs to point at the new engine-side function, not the old stub).

After Task 0's revert of T4, `cl_refvulkan.c` has `vk_ri.VKimp_Init = vk_VKimp_Init;` (the old stub function) and `vk_VKimp_Init` defined as the guard-only stub at lines ~405-419.

We need: `vk_ri.VKimp_Init` slot to point at the new engine-side `VKimp_Init` (which lives in `code/sdl/sdl_glimp.c` after Task 2, exported as a non-static engine-side symbol).

- [ ] **Step 1: Add extern declaration in cl_refvulkan.c**

Near the top of `code/client/cl_refvulkan.c`, after the includes block (around line 50), add:

```c
/* γ' migration: VKimp_Init is defined engine-side in code/sdl/sdl_glimp.c
 * (under #ifdef BUILD_RENDERER_VULKAN). The renderer DLL invokes it
 * through this slot; we wire the engine-side symbol directly.
 * See notes/decisions/2026-06-10-m4-window-ownership-model.md §4. */
extern void VKimp_Init( glconfig_t *config );
extern void VKimp_Shutdown( qboolean unloadDLL );
```

- [ ] **Step 2: Rewire the slot**

Find the wiring near `code/client/cl_refvulkan.c:218` (after revert of T4 it should be `vk_ri.VKimp_Init = vk_VKimp_Init;`). Change to:

```c
vk_ri.VKimp_Init                = VKimp_Init;
vk_ri.VKimp_Shutdown            = VKimp_Shutdown;
```

(Note: no `vk_` prefix — pointing at the engine-side function added in Task 2.)

- [ ] **Step 3: Delete the old `vk_VKimp_Init` / `vk_VKimp_Shutdown` stub functions**

These were restored by Task 0's revert of T4 but are now redundant. Delete the function bodies at `code/client/cl_refvulkan.c:405-424` (the bodies; preserve surrounding code).

Also delete the forward declarations at `code/client/cl_refvulkan.c:104-105` if they were restored by the revert.

- [ ] **Step 4: Verify build**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
source ~/VulkanSDK/1.4.350.0/setup-env.sh
export REALRTCW_ALLOW_VENDOR_EDIT=1
make ARCH=arm64 USE_RENDERER_DLOPEN=1 BUILD_RENDERER_VULKAN=1 USE_OPENAL=1 USE_INTERNAL_LIBS=0 -j8 -k 2>&1 | grep -E "error:|undefined reference" | head
```

Expected: zero `error:` and zero `undefined reference`. Engine binary should link `VKimp_Init` correctly (defined in sdl_glimp.c which is in Q3OBJ).

- [ ] **Step 5: Commit**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
git add code/client/cl_refvulkan.c
git commit -m "feat(cl_refvulkan): wire vk_ri.VKimp_Init to engine-side VKimp_Init (gamma')

After the gamma' migration moves sdl_glimp.c engine-side, the Vulkan
renderer DLL invokes VKimp_Init through ri.VKimp_Init vtable slot. The
slot points at the engine-side function directly (no DLL-side stub
needed). The previous vk_VKimp_Init guard-stub is removed."
```

---

### Task 4: OpenGL deprecated comment block

**Files:**
- Modify: `code/sdl/sdl_glimp.c` (add deprecation banner above OpenGL-specific code)
- Modify: `notes/decisions/2026-06-10-m4-window-ownership-model.md` (update §5 to document the sunset policy)

Per Q2 (OpenGL deprecated comment): add a clear marker in code so future readers know the OpenGL path is on the way out.

- [ ] **Step 1: Add deprecation banner in sdl_glimp.c**

At the top of `GLimp_Init` (engine-side now), add a banner comment:

```c
/*
===============
GLimp_Init  [DEPRECATED — slated for removal after Vulkan parity]

DEPRECATED: The OpenGL renderer is preserved as a safety net while the
Vulkan backend reaches feature parity with RTCW. Once Vulkan is feature-
complete and stable (per the project portfolio roadmap), this function
and its supporting OpenGL DLL will be removed in favor of a Vulkan-only
build.

Until then, this is the engine-side entry point for OpenGL renderer
window+context init. Sister function: VKimp_Init.

Tracked: project_portfolio_vision (Phase 3+ sunset).
===============
*/
void GLimp_Init( qboolean fixedFunction )
{
	/* ... existing body ... */
}
```

Also add a one-liner near `GLimp_StartDriverAndSetMode` when called with `vulkan == qfalse`:

```c
if ( !vulkan )
{
	/* OpenGL path — deprecated, see GLimp_Init banner above. */
	/* ... GL setup ... */
}
```

- [ ] **Step 2: Update decision-note §5 with sunset policy**

In `notes/decisions/2026-06-10-m4-window-ownership-model.md`, update §5 (currently "Future ideal — platform layer") to add a new sub-section about OpenGL sunset:

(Insertion text below §5 heading, before existing content)

```markdown
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
```

- [ ] **Step 3: Commit**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
git add code/sdl/sdl_glimp.c notes/decisions/2026-06-10-m4-window-ownership-model.md
git commit -m "docs(opengl): mark OpenGL path as deprecated; add sunset policy

OpenGL renderer remains as a safety net during Vulkan parity work, but
its lifetime is now explicitly time-boxed in the decision note. Code
banner above GLimp_Init points future readers at the policy.

Part of gamma' landing (notes/decisions/2026-06-10-m4-window-ownership-model.md §5a)."
```

---

### Task 5: Smoke run — OpenGL (verify safety net unchanged)

**Files:**
- Create: `docs/vulkan-phase2/m4-gamma-opengl-smoke.log` (captured GL run log)

After γ', OpenGL path should continue working. This task verifies that.

- [ ] **Step 1: Build OpenGL-only (no Vulkan)**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
make ARCH=arm64 USE_INTERNAL_LIBS=0 USE_OPENAL=1 -j8 2>&1 | tail -20
```

Expected: clean build. No `BUILD_RENDERER_VULKAN`, so engine-side `VKimp_Init` won't compile, won't be linked. OpenGL DLL builds normally.

- [ ] **Step 2: Launch OpenGL run**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
./build/release-darwin-arm64-nosteam/RealRTCW.arm64 +set cl_renderer opengl1 +set developer 1 > /tmp/m4-gamma-gl-stdout.log 2> /tmp/m4-gamma-gl-stderr.log &
APP_PID=$!
sleep 10
kill -INT $APP_PID 2>/dev/null || true
sleep 3
kill -KILL $APP_PID 2>/dev/null || true
wait $APP_PID 2>/dev/null || true
cp /tmp/m4-gamma-gl-stdout.log docs/vulkan-phase2/m4-gamma-opengl-smoke.log
```

- [ ] **Step 3: Verify success markers**

```bash
grep -nE "GLimp_Init|GL_RENDERER|main menu|SDL_window not initialized|Segmentation|Assertion" docs/vulkan-phase2/m4-gamma-opengl-smoke.log | head -20
```

Expected:
- ✅ `GLimp_Init` trace present (engine-side now).
- ✅ `GL_RENDERER:` line present (OpenGL context still created).
- ❌ No `SDL_window not initialized` fatal.
- ❌ No segfault (or one only at the user's SIGINT, after main menu).
- ✅ Main menu reached (`Sending heartbeat to...` or similar deep-init log).

If main menu reached on OpenGL: γ' preserved the safety net. Good.
If failure: γ' regressed OpenGL. Diagnose before proceeding to Task 6.

- [ ] **Step 4: Commit log**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
git add docs/vulkan-phase2/m4-gamma-opengl-smoke.log
git commit -m "docs(vulkan/m4): OpenGL smoke run post-gamma' migration

Confirms OpenGL safety net still works after sdl_glimp.c moved engine-side.
Verifies the gamma' migration didn't regress the GL path."
```

---

### Task 6: Smoke run — Vulkan (verify γ' fixes the crash)

**Files:**
- Create: `docs/vulkan-phase2/m4-gamma-vulkan-smoke.log`

- [ ] **Step 1: Build Vulkan**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
source ~/VulkanSDK/1.4.350.0/setup-env.sh
export REALRTCW_ALLOW_VENDOR_EDIT=1
make ARCH=arm64 USE_RENDERER_DLOPEN=1 BUILD_RENDERER_VULKAN=1 USE_OPENAL=1 USE_INTERNAL_LIBS=0 -j8 -k 2>&1 | tail -10
```

Expected: clean build of engine + both renderer DLLs.

- [ ] **Step 2: Run via vk-capture.sh wrapper**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
./scripts/mac/vk-capture.sh -- +set developer 1 > /tmp/m4-gamma-vk-stdout.log 2> /tmp/m4-gamma-vk-stderr.log &
CAPTURE_PID=$!
sleep 12
kill -INT $CAPTURE_PID 2>/dev/null || true
sleep 3
kill -KILL $CAPTURE_PID 2>/dev/null || true
wait $CAPTURE_PID 2>/dev/null || true
LATEST_LOG=$(ls -t /tmp/realrtcw-vk-*.log /tmp/vk-validation-*.log 2>/dev/null | head -1)
cp "$LATEST_LOG" docs/vulkan-phase2/m4-gamma-vulkan-smoke.log
wc -l docs/vulkan-phase2/m4-gamma-vulkan-smoke.log
```

- [ ] **Step 3: Verify γ' success markers**

```bash
grep -nE "VKimp_Init|SDL_CreateWindow|SDL_window not initialized|setting mode|CL_SetScaling|main menu|R_Init complete|Segmentation|VUID-" docs/vulkan-phase2/m4-gamma-vulkan-smoke.log | head -30
```

Expected (γ' SUCCESS):
- ✅ `VKimp_Init( )` print present.
- ✅ `setting mode` line present — window mode resolved.
- ❌ NO `SDL_window not initialized` (theta' fatal gone).
- ❌ NO segfault between VKimp_Init and the next milestone (the theta'-era pos-VKimp segfault is the bug γ' fixes — should NOT appear).
- The run will likely hit `CL_SetScaling` NULL slot crash (documented adjacent landmine in decision note §6). That's expected and proceeds to next M4 iter.

If γ' fixed the bug but CL_SetScaling now fires: ✅ — exactly the post-γ' state we want. Next iter handles CL_SetScaling.

- [ ] **Step 4: Commit log**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
git add docs/vulkan-phase2/m4-gamma-vulkan-smoke.log
git commit -m "docs(vulkan/m4): Vulkan smoke run post-gamma' migration

R_Init now reaches VKimp_Init AND completes window creation without the
post-VKimp dual-struct segfault. The theta'-era crash is fixed.

Next landmine: CL_SetScaling NULL slot at code/renderervk/tr_init.c:543 -
to be handled in the next M4 fix-loop iteration."
```

---

### Task 7: Update decision note + memory entries (γ' LANDED status)

**Files:**
- Modify: `notes/decisions/2026-06-10-m4-window-ownership-model.md` (extensive rewrite — θ' archived, γ' is now the landed answer)
- Modify: `/Users/ragnar/.claude/projects/-Users-ragnar-fedorov-tech-RealRTCW-macOS/memory/project_m4_window_init_decision.md`

- [ ] **Step 1: Rewrite decision note header**

In `notes/decisions/2026-06-10-m4-window-ownership-model.md`, update the **Status** line:

```markdown
**Status:** **γ' LANDED** 2026-06-11 (commits 015c176 → <γ-final-SHA>). Full Quake3e topology adopted. sdl_glimp.c engine-side, single SDL_window, `void IN_Init(void)`. θ' attempted earlier (T1-T6 commits) but hit dual-refimport_t struct-view landmine — superseded by γ'. **§4 (γ' = Phase 3 cleanup) is now §3-current.**
```

- [ ] **Step 2: Update §2 (options table) — add post-mortem column**

Add a new "post-mortem" annotation to the table noting θ' hit a hidden ε'-cost we didn't see when planning. Specifically, add 2-3 lines under the existing comparison table:

```markdown
**Post-mortem on θ' attempt (2026-06-11):** θ' was implemented in 7 commits and ran far enough to reach `VKimp_Init( )`. Crashed immediately after on `ri.IN_Init(SDL_window)` — root cause was a **dual-struct view of refimport_t** (small in `code/renderer/tr_public.h`, big in `code/renderercommon/tr_public.h`). sdl_glimp.c compiled in Vulkan DLL saw small offsets; real ri storage was big. Hidden ε'-cost in θ' equal to or larger than γ' setup, so γ' became the path of least surprise. See θ' log at `docs/vulkan-phase2/m4-iter4-postθ.log`.
```

- [ ] **Step 3: Rewrite §3 (was θ' implementation) into §3a — γ' implementation digest**

Replace existing §3 ("M4 decision: θ'") with a concise digest of what γ' actually shipped, referring to this plan for full step-by-step. ~200 words.

- [ ] **Step 4: Update memory entry**

Edit `/Users/ragnar/.claude/projects/-Users-ragnar-fedorov-tech-RealRTCW-macOS/memory/project_m4_window_init_decision.md` — update **Status** line:

```markdown
**Status (2026-06-11 late evening):** γ' LANDED — full Quake3e topology. sdl_glimp.c engine-side, single SDL_window, void IN_Init(void). Replaces earlier θ' (which hit dual-refimport_t struct-view landmine at runtime). Next: CL_SetScaling NULL slot in next M4 fix-loop iter.
```

Also rewrite the **Decision** section's first three bullet points to reflect γ' as M4-current (was M4=θ', Phase3=γ').

- [ ] **Step 5: Commit**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
git add notes/decisions/2026-06-10-m4-window-ownership-model.md
git commit -m "docs(notes): mark gamma' as landed; archive theta' post-mortem

Decision note updated to reflect:
- gamma' is now the M4 window-init solution (was earlier deferred to Phase 3).
- theta' attempted earlier, hit dual-refimport_t landmine, superseded.
- Post-mortem captured in section 2 so future readers see why we pivoted."
```

Memory file (outside worktree) committed via simple edit — no commit needed in worktree.

---

## Self-Review checklist

- ✅ All file:line citations verified by direct Read at plan-writing time.
- ✅ Reverts (Task 0) target specific θ' commits T3 (`57aedc4`) + T4 (`bd1b06b`); T1 (`d52b71a`) and T2 (`cce9059`) preserved per Q1 decision.
- ✅ The Big Move (Task 2) is one atomic commit — broken intermediate states avoided.
- ✅ Task 2 explicitly accounts for the `qgl*` / `GLimp_GetProcAddresses` engine-vs-renderer split, with Quake3e reference step (Step 1) so subagent verifies before editing.
- ✅ `IN_Init` signature change (Q3 = void return form) propagated through 4 sites: definition, decl in qcommon.h, slot in tr_public.h, 3 call sites.
- ✅ OpenGL deprecated comment (Q2 = full γ' + deprecated) added in code AND decision note.
- ✅ `RealRTCW_VkBridgeInit()` call relocation (Step 12) ensures Vulkan DLL still populates its internal cvar pointers.
- ✅ Both smoke runs (Task 5 = OpenGL, Task 6 = Vulkan) defined with explicit success/failure criteria.
- ✅ Out-of-scope explicitly listed (CL_SetScaling, sdl_gamma migration, ABI versioning) — won't drift in.
- ✅ Push gating honored (no `git push` in any step).
- ✅ Constraints from memory: `REALRTCW_ALLOW_VENDOR_EDIT=1`, vendor-prefix exempt (tr_init.c upstream), use-planning-skill compliance (this IS the plan), portfolio-vision aligned (full γ' chosen for narrative + architectural correctness).

**Risk register:**
- Task 2 is the biggest single task — many file edits in one commit. Mitigation: subagent reviews against this plan step-by-step; Quake3e reference (Step 1) anchors design choices.
- Task 5 (OpenGL smoke) could surface regressions hidden by Task 2 — if so, fix-in-place before Task 6, don't skip.
- `glConfig` engine-side vs renderer-side: both exist in their own binaries, names same but binaries don't link to each other so no collision. Verify at Task 2 Step 17.
