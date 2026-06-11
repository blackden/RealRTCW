# Vulkan M4 — Window Contract (θ') Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Vulkan renderer-DLL create its own SDL window (with `SDL_WINDOW_VULKAN`) and hand it to the engine through the existing `ri.IN_Init` channel — so `R_Init → VKimp_Init` boots past the current "SDL_window not initialized" fatal at `code/client/cl_refvulkan.c:416`.

**Architecture:** Option **θ'** from `notes/decisions/2026-06-10-m4-window-ownership-model.md` §3. Mirror the OpenGL handoff pattern symmetrically: add `VKimp_Init` next to `GLimp_Init` in `code/sdl/sdl_glimp.c` (single TU compiled into both renderer DLLs). Refactor `GLimp_StartDriverAndSetMode` to take a 5th `qboolean vulkan` arg — same helper, different window flag, skip GL-context block on Vulkan. Vendor-edit `code/renderervk/tr_init.c` 2 sites to call the local `VKimp_Init` directly instead of going through `ri.VKimp_Init`. Engine-side `vk_VKimp_Init` becomes a deleted, intentionally-NULL slot.

**Tech Stack:** C (C99), SDL3, Vulkan 1.4 + MoltenVK, RTCW engine on macOS arm64. No unit-test framework in this codebase — verification is `compile + run game + grep log`.

**Gating prerequisites:**
- `REALRTCW_ALLOW_VENDOR_EDIT=1` exported in shell (vendor edits in `code/renderervk/`).
- Worktree clean except for the two existing local commits (`95368ca`, `ae217a7`).
- `~/VulkanSDK/1.4.350.0/setup-env.sh` available for Vulkan build.

**Out-of-scope (deferred to next M4 iter):** the adjacent `CL_SetScaling` NULL-slot crash at `code/renderervk/tr_init.c:543, 556, 562`. Expected to fire after this plan succeeds — handle in a follow-up commit, not this one.

---

## File Structure

| File | Action | Responsibility after change |
|---|---|---|
| `code/sdl/sdl_glimp.c` | modify | Adds `VKimp_Init` + `VKimp_Shutdown` next to `GLimp_Init`. `GLimp_StartDriverAndSetMode` gains a `qboolean vulkan` arg. `GLimp_SetMode` gains the same arg and branches `SDL_WINDOW_VULKAN` vs `SDL_WINDOW_OPENGL`, skipping GL-context work on Vulkan path. |
| `code/renderervk/tr_init.c` | vendor-edit (2 sites) | Call local `VKimp_Init` / `VKimp_Shutdown` directly. Drops the `ri.VKimp_*` vtable hop. Adds 2 `extern` decls at top. |
| `code/client/cl_refvulkan.c` | modify | Removes `vk_VKimp_Init` / `vk_VKimp_Shutdown` stub functions. Removes their `vk_ri.VKimp_*` slot wirings. Adds them to the documented "intentionally NULL" list. |
| `docs/vulkan-phase2/m4-iter4-postθ.log` | create (via run) | Captured iter 4 log from smoke run. Evidence the boot reaches past `R_Init`. |

No new headers — `extern` decls in `tr_init.c` keep the surgery localized.

---

### Task 1: Add `qboolean vulkan` arg to `GLimp_SetMode` and `GLimp_StartDriverAndSetMode`

**Files:**
- Modify: `code/sdl/sdl_glimp.c:547` (`GLimp_SetMode` signature + flag branch)
- Modify: `code/sdl/sdl_glimp.c:555` (initial `flags` value)
- Modify: `code/sdl/sdl_glimp.c:756-880` (skip GL-context block on Vulkan path)
- Modify: `code/sdl/sdl_glimp.c:943-946` (skip `GLimp_DetectAvailableModes` + `qglGetString` on Vulkan)
- Modify: `code/sdl/sdl_glimp.c:956` (`GLimp_StartDriverAndSetMode` signature)
- Modify: `code/sdl/sdl_glimp.c:983` (forward `vulkan` to `GLimp_SetMode`)
- Modify: `code/sdl/sdl_glimp.c:1245, 1251, 1260` (existing `GLimp_Init` callers pass `qfalse`)

- [ ] **Step 1: Change `GLimp_SetMode` signature**

Current `code/sdl/sdl_glimp.c:547`:

```c
static int GLimp_SetMode(int mode, qboolean fullscreen, qboolean noborder, qboolean fixedFunction)
```

Change to:

```c
static int GLimp_SetMode(int mode, qboolean fullscreen, qboolean noborder, qboolean fixedFunction, qboolean vulkan)
```

- [ ] **Step 2: Branch the initial SDL window flag on `vulkan`**

Current `code/sdl/sdl_glimp.c:555`:

```c
    Uint32 flags = SDL_WINDOW_OPENGL;
```

Change to:

```c
    Uint32 flags = vulkan ? SDL_WINDOW_VULKAN : SDL_WINDOW_OPENGL;
```

- [ ] **Step 3: Skip the GL-context block when `vulkan == qtrue`**

The GL-context-creation block runs from approximately `code/sdl/sdl_glimp.c:756` (`#ifdef USE_OPENGLES` setting major version) through `:880` (end of the fallback GL context try). All of `SDL_GL_SetAttribute(...)` and `SDL_GL_CreateContext(...)` calls must be guarded.

Wrap the GL-attribute setup that starts at `:756` and ends just before `SDL_CreateWindow` at `:788`, and the GL context creation that follows `SDL_CreateWindow` through `:880`, like this — change the block beginning at `code/sdl/sdl_glimp.c:756` from:

```c
#ifdef USE_OPENGLES
        SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 1 );
#endif

        SDL_GL_SetAttribute( SDL_GL_RED_SIZE, perChannelColorBits );
        /* ... all the SDL_GL_SetAttribute calls through :780 (DOUBLEBUFFER) ... */
        SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
```

to:

```c
        if ( !vulkan )
        {
#ifdef USE_OPENGLES
            SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 1 );
#endif

            SDL_GL_SetAttribute( SDL_GL_RED_SIZE, perChannelColorBits );
            /* ... preserve the entire existing block through DOUBLEBUFFER ... */
            SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
        }
```

Then continue down to the `SDL_GL_CreateContext` retry block that ends at approximately `:880` (the line `SDL_glContext = NULL;` in the `else` branch of `if (!fixedFunction)`). Wrap that *entire* `if (!fixedFunction) { ... } else { SDL_glContext = NULL; }` block in `if ( !vulkan )`:

```c
        if ( !vulkan )
        {
            if (!fixedFunction)
            {
                /* ... preserve existing OpenGL 3.2 core context try-block ... */
            }
            else
            {
                SDL_glContext = NULL;
            }
        }
```

After this edit, the only code that runs inside the `for ( i = 0; i < 16; i++ )` retry loop when `vulkan == qtrue` is the `SDL_CreateWindow` call at `:788` and its companion `SDL_SetWindowPosition` / `SDL_SetWindowIcon`.

- [ ] **Step 4: Skip OpenGL info gathering at end of `GLimp_SetMode`**

Current `code/sdl/sdl_glimp.c:943-946`:

```c
    GLimp_DetectAvailableModes();

    glstring = (char *) qglGetString (GL_RENDERER);
    ri.Printf( PRINT_ALL, "GL_RENDERER: %s\n", glstring );
```

Change to:

```c
    GLimp_DetectAvailableModes();

    if ( !vulkan )
    {
        glstring = (char *) qglGetString (GL_RENDERER);
        ri.Printf( PRINT_ALL, "GL_RENDERER: %s\n", glstring );
    }
```

(The compiler will then warn `glstring` is unused when `vulkan` — that's why we leave the declaration but only the use is guarded. Alternative: move the `const char *glstring;` into the `if` block. Either is acceptable.)

- [ ] **Step 5: Change `GLimp_StartDriverAndSetMode` signature**

Current `code/sdl/sdl_glimp.c:956`:

```c
static qboolean GLimp_StartDriverAndSetMode(int mode, qboolean fullscreen, qboolean noborder, qboolean gl3Core)
```

Change to:

```c
static qboolean GLimp_StartDriverAndSetMode(int mode, qboolean fullscreen, qboolean noborder, qboolean gl3Core, qboolean vulkan)
```

- [ ] **Step 6: Forward `vulkan` to `GLimp_SetMode`**

Current `code/sdl/sdl_glimp.c:983`:

```c
    err = GLimp_SetMode(mode, fullscreen, noborder, gl3Core);
```

Change to:

```c
    err = GLimp_SetMode(mode, fullscreen, noborder, gl3Core, vulkan);
```

- [ ] **Step 7: Update existing `GLimp_Init` callers to pass `qfalse`**

Three call sites at `code/sdl/sdl_glimp.c:1245, :1251, :1260`. Change each:

```c
    if(GLimp_StartDriverAndSetMode(r_mode->integer, r_fullscreen->integer, r_noborder->integer, fixedFunction))
```

to:

```c
    if(GLimp_StartDriverAndSetMode(r_mode->integer, r_fullscreen->integer, r_noborder->integer, fixedFunction, qfalse))
```

Same pattern for the other two callers — the only difference between them is the first three args (mode, fullscreen, noborder).

- [ ] **Step 8: Build OpenGL renderer alone to verify GL path still compiles + links**

Run:

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
make ARCH=arm64 USE_INTERNAL_LIBS=0 USE_OPENAL=1 -j8 2>&1 | tail -40
```

Expected: clean build, no errors. If linker complains about `vulkan` arg mismatch, re-check Step 7 — all three call sites updated.

- [ ] **Step 9: Commit**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
git add code/sdl/sdl_glimp.c
git commit -m "refactor(sdl): plumb qboolean vulkan through GLimp_StartDriverAndSetMode

Prerequisite for VKimp_Init in the next commit. The renderer-DLL helper
now branches the SDL window flag (SDL_WINDOW_VULKAN vs SDL_WINDOW_OPENGL)
and skips GL-context work when vulkan==qtrue. OpenGL path passes qfalse
at all three callers and continues to work as before."
```

---

### Task 2: Add `VKimp_Init` and `VKimp_Shutdown` to `code/sdl/sdl_glimp.c`

**Files:**
- Modify: `code/sdl/sdl_glimp.c` (append two new functions after `GLimp_Init`, around line 1326)

The new functions mirror `GLimp_Init` but:
- call `RealRTCW_VkBridgeInit()` (registers the renderer-side cvars the bridge owns),
- pass `qtrue` to the refactored helper,
- skip GL-specific things (`Sys_GLimpInit`, gamma probe, GL strings).

- [ ] **Step 1: Add `VKimp_Init` definition**

Append directly after the closing brace of `GLimp_Init` (currently `code/sdl/sdl_glimp.c:1325`), before `GLimp_EndFrame`:

```c
#ifdef BUILD_RENDERER_VULKAN
/*
===============
VKimp_Init

θ' window-init path. Owns SDL window creation for the Vulkan renderer
DLL. Mirrors GLimp_Init but takes the Vulkan branch through
GLimp_StartDriverAndSetMode (no GL context creation).

The caller is code/renderervk/tr_init.c (vendored Q3e), which now
invokes this function directly instead of going through ri.VKimp_Init.
See notes/decisions/2026-06-10-m4-window-ownership-model.md §3.
===============
*/
void VKimp_Init( glconfig_t *config )
{
    RealRTCW_VkBridgeInit();

    ri.Printf( PRINT_DEVELOPER, "VKimp_Init( )\n" );

    r_allowSoftwareGL = ri.Cvar_Get( "r_allowSoftwareGL", "0", CVAR_LATCH );
    r_sdlDriver = ri.Cvar_Get( "r_sdlDriver", "", CVAR_ROM );
    r_allowResize = ri.Cvar_Get( "r_allowResize", "0", CVAR_ARCHIVE | CVAR_LATCH );
    r_centerWindow = ri.Cvar_Get( "r_centerWindow", "0", CVAR_ARCHIVE | CVAR_LATCH );

    if( ri.Cvar_VariableIntegerValue( "com_abnormalExit" ) )
    {
        ri.Cvar_Set( "r_mode", va( "%d", R_MODE_FALLBACK ) );
        ri.Cvar_Set( "r_fullscreen", "0" );
        ri.Cvar_Set( "r_centerWindow", "0" );
        ri.Cvar_Set( "com_abnormalExit", "0" );
    }

    ri.Sys_GLimpInit( );

    ri.Cvar_Get("r_availableModes", "", CVAR_ROM);
    ri.Cvar_Get("r_maxResolutionWidth", "0", 0);
    ri.Cvar_Get("r_maxResolutionHeight", "0", 0);

    /* Create the window with SDL_WINDOW_VULKAN; no GL context. */
    if(GLimp_StartDriverAndSetMode(r_mode->integer, r_fullscreen->integer, r_noborder->integer, qfalse, qtrue))
        goto success;

    ri.Sys_GLimpSafeInit( );

    if(GLimp_StartDriverAndSetMode(r_mode->integer, r_fullscreen->integer, qfalse, qfalse, qtrue))
        goto success;

    if( r_mode->integer != R_MODE_FALLBACK )
    {
        ri.Printf( PRINT_ALL, "Setting r_mode %d failed, falling back on r_mode %d\n",
                r_mode->integer, R_MODE_FALLBACK );

        if(GLimp_StartDriverAndSetMode(R_MODE_FALLBACK, qfalse, qfalse, qfalse, qtrue))
            goto success;
    }

    ri.Error( ERR_FATAL, "VKimp_Init() - could not create SDL Vulkan window" );

success:
    /* Populate glconfig fields that downstream Q3e Vulkan code reads.
     * vidWidth / vidHeight are already set by GLimp_SetMode. */
    config->vidWidth        = glConfig.vidWidth;
    config->vidHeight       = glConfig.vidHeight;
    config->windowAspect    = glConfig.windowAspect;
    config->isFullscreen    = glConfig.isFullscreen;
    config->displayFrequency = 60;        /* refined later when swapchain is built */
    config->deviceSupportsGamma = qfalse; /* MoltenVK path -- shader gamma only */

    /* Hand the window pointer to the engine input subsystem via the
     * existing ri.IN_Init handoff (same channel GLimp_Init uses at the
     * end of its body). */
    ri.IN_Init( SDL_window );
}

/*
===============
VKimp_Shutdown
===============
*/
void VKimp_Shutdown( qboolean unloadDLL )
{
    (void)unloadDLL;

    if( SDL_window )
    {
        SDL_DestroyWindow( SDL_window );
        SDL_window = NULL;
    }

    if( unloadDLL )
    {
        SDL_QuitSubSystem( SDL_INIT_VIDEO );
    }
}
#endif /* BUILD_RENDERER_VULKAN */
```

(The `#ifdef BUILD_RENDERER_VULKAN` guard ensures these functions are only present in the Vulkan DLL, not the OpenGL DLL — matching how `realrtcw_vk_window_bridge.h` is included at the top of the file.)

- [ ] **Step 2: Verify the `glconfig_t` field names compile**

Quick sanity check — the field names used above (`vidWidth`, `vidHeight`, `windowAspect`, `isFullscreen`, `displayFrequency`, `deviceSupportsGamma`) must exist in `glconfig_t`. If the build errors on a missing field, search the struct definition:

```bash
grep -n "displayFrequency\|deviceSupportsGamma" /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt/code/renderercommon/tr_types.h
```

If `displayFrequency` doesn't exist, drop that line. The function will still build.

- [ ] **Step 3: Build the Vulkan DLL only (skip running yet — we haven't wired tr_init.c)**

The Vulkan DLL won't link cleanly yet because nothing calls `VKimp_Init`, but it should at least **compile** without errors. Run:

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
source ~/VulkanSDK/1.4.350.0/setup-env.sh
make ARCH=arm64 USE_RENDERER_DLOPEN=1 BUILD_RENDERER_VULKAN=1 USE_OPENAL=1 USE_INTERNAL_LIBS=0 -j8 -k 2>&1 | grep -E "error:|warning: unused" | head -20
```

Expected: zero `error:` lines. Unused-function warnings on `VKimp_Init` / `VKimp_Shutdown` are fine — Task 3 hooks them up.

- [ ] **Step 4: Commit**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
git add code/sdl/sdl_glimp.c
git commit -m "feat(sdl): add VKimp_Init/VKimp_Shutdown to renderer-DLL sdl_glimp.c

Mirrors the GLimp_Init handoff pattern symmetrically: window born in
DLL with SDL_WINDOW_VULKAN, passed to engine via existing ri.IN_Init.
Not wired up yet -- next commit edits tr_init.c to call these directly."
```

---

### Task 3: Vendor-edit `code/renderervk/tr_init.c` to call local `VKimp_Init` directly

**Files:**
- Modify: `code/renderervk/tr_init.c` (add 2 extern decls + edit 2 call sites)

Prerequisite: `REALRTCW_ALLOW_VENDOR_EDIT=1` must be exported. If the pre-commit hook rejects the change, this is the missing knob.

- [ ] **Step 1: Verify env-var is set**

Run:

```bash
echo "${REALRTCW_ALLOW_VENDOR_EDIT:-NOT_SET}"
```

Expected: `1`. If `NOT_SET`, export it before editing:

```bash
export REALRTCW_ALLOW_VENDOR_EDIT=1
```

- [ ] **Step 2: Add `extern` decls near the top of `tr_init.c`**

Locate the `#include` block at the top of `code/renderervk/tr_init.c` (first ~30 lines). Immediately after the last `#include`, before any function definitions, insert:

```c
#ifdef USE_VULKAN
/* θ': window-init functions live in code/sdl/sdl_glimp.c, linked into
 * this DLL via Makefile per-target rules. We call them directly so the
 * window-creation lifecycle stays inside the DLL (symmetric with the
 * OpenGL GLimp_Init handoff). See notes/decisions/
 * 2026-06-10-m4-window-ownership-model.md §3 for the architectural
 * rationale. */
extern void VKimp_Init( glconfig_t *config );
extern void VKimp_Shutdown( qboolean unloadDLL );
#endif
```

- [ ] **Step 3: Replace the `ri.VKimp_Init` call site**

Current `code/renderervk/tr_init.c:528-535`:

```c
#ifdef USE_VULKAN
		if ( !ri.VKimp_Init )
		{
			ri.Error( ERR_FATAL, "Vulkan interface is not initialized" );
		}

		// This function is responsible for initializing a valid Vulkan subsystem.
		ri.VKimp_Init( &glConfig );
```

Change to:

```c
#ifdef USE_VULKAN
		// θ': call the renderer-DLL-local VKimp_Init directly. No vtable
		// hop needed since the function lives in this DLL's sdl_glimp.c.
		VKimp_Init( &glConfig );
```

- [ ] **Step 4: Replace the `ri.VKimp_Shutdown` call site**

Current `code/renderervk/tr_init.c:1983-1986`:

```c
		if ( code != REF_KEEP_WINDOW ) {
			if ( ri.VKimp_Shutdown ) {
				ri.VKimp_Shutdown( code == REF_UNLOAD_DLL ? qtrue : qfalse );
			}
			Com_Memset( &glConfig, 0, sizeof( glConfig ) );
		}
```

Change to:

```c
		if ( code != REF_KEEP_WINDOW ) {
			// θ': direct call to renderer-DLL-local VKimp_Shutdown.
			VKimp_Shutdown( code == REF_UNLOAD_DLL ? qtrue : qfalse );
			Com_Memset( &glConfig, 0, sizeof( glConfig ) );
		}
```

- [ ] **Step 5: Build the Vulkan renderer + engine to verify link clean**

Run:

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
source ~/VulkanSDK/1.4.350.0/setup-env.sh
make ARCH=arm64 USE_RENDERER_DLOPEN=1 BUILD_RENDERER_VULKAN=1 USE_OPENAL=1 USE_INTERNAL_LIBS=0 -j8 -k 2>&1 | tail -30
```

Expected: full build succeeds (engine binary + both renderer DLLs). No undefined-symbol errors for `VKimp_Init` / `VKimp_Shutdown`.

If the OpenGL DLL fails because `VKimp_Init` is `#ifdef BUILD_RENDERER_VULKAN`-only and the OpenGL DLL doesn't define it — that's expected and correct, since the OpenGL DLL's `tr_init.c` would also need to be guarded. But since `USE_VULKAN` is set per-target via the same Makefile rules as `BUILD_RENDERER_VULKAN`, the `extern`s only resolve when needed. If linker errors, check that `USE_VULKAN` is gated the same per-target way `BUILD_RENDERER_VULKAN` is.

- [ ] **Step 6: Commit**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
git add code/renderervk/tr_init.c
git commit -m "vendor(renderervk): call local VKimp_Init/Shutdown instead of ri vtable

θ' decision: window-creation is owned by code/sdl/sdl_glimp.c (which
is linked into this DLL via Makefile per-target rules). Calling
ri.VKimp_Init would round-trip through the engine, which we don't
need since the function is right here.

Two-line vendor divergence from upstream Quake3e (they put VKimp_Init
engine-side and use the vtable). Justified in
notes/decisions/2026-06-10-m4-window-ownership-model.md §3."
```

---

### Task 4: Remove engine-side `vk_VKimp_Init` stub and document it as intentionally NULL

**Files:**
- Modify: `code/client/cl_refvulkan.c:214-219` (remove the wirings)
- Modify: `code/client/cl_refvulkan.c:221-260` (add `VKimp_Init` / `VKimp_Shutdown` to documented NULL list)
- Modify: `code/client/cl_refvulkan.c:405-424` (delete `vk_VKimp_Init` + `vk_VKimp_Shutdown` function bodies)

- [ ] **Step 1: Delete the wirings at `vk_BuildRefImport`**

Current `code/client/cl_refvulkan.c:214-219`:

```c
    /* --- VULKAN WINDOW SYSTEM: code that doesn't exist on engine side.
     *     Defined further down. */
    vk_ri.VK_CreateSurface          = vk_VK_CreateSurface;
    vk_ri.VK_GetInstanceProcAddr    = vk_VK_GetInstanceProcAddr;
    vk_ri.VKimp_Init                = vk_VKimp_Init;
    vk_ri.VKimp_Shutdown            = vk_VKimp_Shutdown;
```

Change to:

```c
    /* --- VULKAN WINDOW SYSTEM: surface + proc-addr live engine-side
     *     because they need post-window-creation hooks. Defined further
     *     down. VKimp_Init / VKimp_Shutdown are intentionally NULL -- see
     *     the NULL-slot list below. */
    vk_ri.VK_CreateSurface          = vk_VK_CreateSurface;
    vk_ri.VK_GetInstanceProcAddr    = vk_VK_GetInstanceProcAddr;
```

- [ ] **Step 2: Add `VKimp_Init` / `VKimp_Shutdown` to the NULL-slot list**

In the comment block starting at `code/client/cl_refvulkan.c:221`, add (alphabetical position — between `GL_GetProcAddress` and the next non-`GL` entry, or wherever fits):

```c
     *       VKimp_Init           -- θ' option: renderer-DLL owns Vulkan
     *                              window creation in its own sdl_glimp.c.
     *                              See notes/decisions/
     *                              2026-06-10-m4-window-ownership-model.md.
     *       VKimp_Shutdown       -- same as VKimp_Init.
```

- [ ] **Step 3: Delete the stub function bodies**

Delete `code/client/cl_refvulkan.c:405-424` entirely — the entire `vk_VKimp_Init` function block and the `vk_VKimp_Shutdown` function block. They are no longer wired and no longer needed.

- [ ] **Step 4: Build to verify no remaining references**

Run:

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
source ~/VulkanSDK/1.4.350.0/setup-env.sh
make ARCH=arm64 USE_RENDERER_DLOPEN=1 BUILD_RENDERER_VULKAN=1 USE_OPENAL=1 USE_INTERNAL_LIBS=0 -j8 -k 2>&1 | grep -E "error:" | head
```

Expected: zero errors. If "unused function `vk_VKimp_Init`" appears — Step 3 missed; re-check.

- [ ] **Step 5: Commit**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
git add code/client/cl_refvulkan.c
git commit -m "feat(cl_refvulkan): null vk_VKimp_Init slot; renderer-DLL owns Vulkan window

θ' decision (notes/decisions/2026-06-10-m4-window-ownership-model.md §3):
window-init lives in code/sdl/sdl_glimp.c linked into the Vulkan DLL.
Engine-side stubs no longer wire to anything and are deleted. The slots
are listed in the documented intentionally-NULL block at the top of
vk_BuildRefImport for grep-discoverability."
```

---

### Task 5: Smoke run — verify boot reaches past `VKimp_Init`

**Files:**
- Create: `docs/vulkan-phase2/m4-iter4-postθ.log` (captured run log)

- [ ] **Step 1: Launch the game with validation layers enabled**

Use the existing capture infrastructure if present, otherwise the raw form. Check first:

```bash
ls /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt/scripts/vk-capture.sh 2>&1
```

If present:

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
./scripts/vk-capture.sh 2>&1 | tee docs/vulkan-phase2/m4-iter4-postθ.log
```

If not present, run raw (this may need adjustment depending on packaging on Ragnar's machine):

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
source ~/VulkanSDK/1.4.350.0/setup-env.sh
VK_LOADER_DEBUG=warn VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
    ./build/release-darwin-arm64/wolfsp.arm64 +set cl_renderer vulkan +set vid_xpos 100 +set vid_ypos 100 \
    2>&1 | tee docs/vulkan-phase2/m4-iter4-postθ.log &
APP_PID=$!
sleep 5
# Let it run long enough to crash on the next landmine, or hit main menu
sleep 5
kill -INT $APP_PID 2>/dev/null
wait $APP_PID 2>/dev/null
```

Expected: the log contains `VKimp_Init( )` (the PRINT_DEVELOPER from new code at Task 2), then `Initializing OpenGL display` (yes, that string still prints from the helper — see Task 1 step 4 — that's cosmetic and we ignore it), then `Display aspect:` ... `...setting mode -2:` ... `<W> <H>`. The window opens. Then the run likely **crashes on `CL_SetScaling` NULL deref** at `code/renderervk/tr_init.c:543` — this is the documented expected next landmine, not a regression.

- [ ] **Step 2: Grep the log for the success signal**

Run:

```bash
grep -nE "VKimp_Init|SDL_CreateWindow failed|SDL_window not initialized|setting mode|CL_SetScaling|main menu|R_Init complete" /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt/docs/vulkan-phase2/m4-iter4-postθ.log
```

Expected output:
- ✅ A `VKimp_Init( )` line.
- ✅ A `...setting mode -2:` or similar line.
- ❌ NO `SDL_window not initialized` fatal (that was the bug we fixed).
- ⚠️ Possibly a `CL_SetScaling` NULL deref / segfault — expected next landmine, log it for the follow-up commit but **don't fix it in this plan**.

If the log shows `SDL_window not initialized` — θ' didn't take effect. Debug: verify Task 3 vendor-edit actually committed, verify `BUILD_RENDERER_VULKAN` is defined in the build (`Makefile:3090`), verify the binary in `build/release-darwin-arm64/` is from this build (timestamp).

- [ ] **Step 3: Commit the log capture**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
git add docs/vulkan-phase2/m4-iter4-postθ.log
git commit -m "docs(vulkan/m4): capture iter 4 log — θ' window-init lands

R_Init now reaches VKimp_Init and creates an SDL_WINDOW_VULKAN window
without tripping the SDL_window-not-initialized guard. Next landmine is
the documented CL_SetScaling NULL slot at tr_init.c:543 -- to be handled
in the next M4 fix-loop iteration."
```

---

### Task 6: Update the decision note + memory with "θ' landed" status

**Files:**
- Modify: `notes/decisions/2026-06-10-m4-window-ownership-model.md` (header status line)
- Modify: `/Users/ragnar/.claude/projects/-Users-ragnar-fedorov-tech-RealRTCW-macOS/memory/project_m4_window_init_decision.md` (status update)

- [ ] **Step 1: Update decision note status**

In `notes/decisions/2026-06-10-m4-window-ownership-model.md`, change the header line:

```
**Status:** decision committed — implementing **θ'** for M4. **γ'** documented as the correct principled answer for Phase 3 once Vulkan is feature-complete.
```

to:

```
**Status:** **θ' LANDED** (see `docs/vulkan-phase2/m4-iter4-postθ.log`). M4 fix-loop continues — next landmine is CL_SetScaling NULL slot. **γ'** documented as the correct principled answer for Phase 3 once Vulkan is feature-complete.
```

- [ ] **Step 2: Update project-memory entry to reflect landed status**

In `/Users/ragnar/.claude/projects/-Users-ragnar-fedorov-tech-RealRTCW-macOS/memory/project_m4_window_init_decision.md`, add a "Status" line near the top:

```
**Status (2026-06-11):** θ' landed. R_Init boots past VKimp_Init. Next landmine: CL_SetScaling.
```

- [ ] **Step 3: Commit decision-note change**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
git add notes/decisions/2026-06-10-m4-window-ownership-model.md
git commit -m "docs(notes): mark θ' window-init as landed for M4"
```

(The memory file update is outside the worktree — no commit needed, it lives in `~/.claude/`.)

---

## Self-Review checklist

- ✅ All file:line citations verified against current code via Read at plan-writing time (not from old memory).
- ✅ Task 1 callers updated count matches: 3 sites in `GLimp_Init`. No other call sites — grep result earlier showed only `GLimp_StartDriverAndSetMode` definition + 3 calls within `GLimp_Init`.
- ✅ Task 2's `VKimp_Init` mirrors `GLimp_Init` body structure 1:1 except for the `qtrue` Vulkan flag and the dropped GL post-success block.
- ✅ Task 3's edits are exactly 2 sites in `tr_init.c` + 1 extern block — matches the "2-line vendor edit" advertised in the decision note.
- ✅ Task 4's NULL list addition keeps grep-discoverability.
- ✅ Task 5's grep checks for both success (VKimp_Init reached) and the expected next landmine (CL_SetScaling) — so the run is judged correctly even if it crashes downstream.
- ✅ Out-of-scope items called out explicitly: `CL_SetScaling` deferred to next iter, `γ'` deferred to Phase 3.
- ✅ Build flags match `[[project_build_flags_macos]]` (`USE_INTERNAL_LIBS=0`, `BUILD_RENDERER_VULKAN=1`, `USE_OPENAL=1`, SDK env sourced).
- ✅ Constraints honored: vendor-prefix exempt (`tr_init.c` already vendored, not RealRTCW-authored), `REALRTCW_ALLOW_VENDOR_EDIT=1` documented as prerequisite, push gating preserved (no `git push` in any step).
