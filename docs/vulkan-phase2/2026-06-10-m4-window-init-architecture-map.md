# M4 Window-Init Wiring — Fact Map

All citations are absolute paths in the worktree
`/Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt/` and the upstream reference
`/Users/ragnar/fedorov_tech/refs/Quake3e/`. No code changes proposed.

## Corrections to the "verified" list

The "verified" anchors are accurate. One nuance worth flagging:

- The comment at `code/sdl/sdl_input.c:60` ("Populated by IN_Init when
  renderer hands us its window pointer") describes the **engine-side**
  global. The handoff is real but is not the renderer "handing" the
  engine anything across a process / DLL boundary in the OO sense — the
  renderer DLL calls back through the engine-installed
  `ri.IN_Init(SDL_window)` slot, passing its own renderer-DLL copy of
  `SDL_window` as the void* arg. The engine's IN_Init then stores it
  into the engine TU's distinct global. This is a value-copy through the
  vtable, not a shared-pointer mechanism. Important because for Vulkan
  there is currently no analogous call.

---

## Section 1 — Current state map

### 1.1  OpenGL path: how `SDL_window` reaches the engine binary

The handoff is a single chain, three hops:

1. Engine `CL_StartHunkUsers` (`code/client/cl_main.c:3341`) eventually
   calls into the renderer's `R_Init`, which on the GL path calls
   `ri.GLimp_Init(&glConfig)` (vtable slot installed at
   `code/client/cl_main.c` near the OpenGL section of CL_InitRef — the
   slot exists on RealRTCW since this is the OpenGL contract).

2. `ri.GLimp_Init` lands in the renderer DLL at
   `code/sdl/sdl_glimp.c:1217 (GLimp_Init)`. That function calls
   `GLimp_StartDriverAndSetMode(...)` which actually creates the window
   at `code/sdl/sdl_glimp.c:788 (SDL_CreateWindow ... = SDL_window)` —
   storing into the **renderer-DLL** copy of `SDL_window` at
   `code/sdl/sdl_glimp.c:67`.

3. Still inside `GLimp_Init`, last line at
   `code/sdl/sdl_glimp.c:1324`:
   ```
   ri.IN_Init( SDL_window );
   ```
   This call jumps back through the engine-installed vtable slot
   (`code/client/cl_main.c:3476 — ri.IN_Init = IN_Init`) into the
   engine's `IN_Init(void *windowData)` at
   `code/sdl/sdl_input.c:1235`. Line 1245:
   ```
   SDL_window = (SDL_Window *)windowData;
   ```
   That is the assignment to the engine TU's `SDL_window`.

So the GL handoff is: renderer creates window → stores in renderer-DLL
global → passes pointer via `ri.IN_Init` back to engine → engine stores
in engine-side global. Two TUs, two globals, one void* round-trip
through the refImport vtable.

### 1.2  `realrtcw_vk_window_bridge.{c,h}` — what it actually does

`code/renderervk/realrtcw_vk_window_bridge.h:12` declares
`void RealRTCW_VkBridgeInit(void);`. The header is included from
`code/sdl/sdl_glimp.c:42` under `#ifdef BUILD_RENDERER_VULKAN`.

`code/renderervk/realrtcw_vk_window_bridge.c:60-196` does **two
unrelated things**, both renderer-DLL-internal:

1. **Tentative-defines 8 cvar pointers + 2 scalars + 1 helper**
   (`r_mode`, `r_fullscreen`, `r_noborder`, `r_colorbits`, `r_depthbits`,
   `r_stencilbits`, `r_stereoEnabled`, `r_swapInterval`, `displayAspect`,
   `haveClampToEdge`, `R_GetModeInfo`) at lines 69-79 and 138-165. These
   are symbols the vendored `code/sdl/sdl_glimp.c` (originally written
   against the iortcw/RTCW SP renderer ABI) expects as externs from
   `code/renderer/tr_local.h`, but Quake3e's vendored `renderervk/`
   dropped them. Bridge re-supplies them so the Vulkan DLL links.

2. **`RealRTCW_VkBridgeInit` at line 177** latches those cvars via
   `ri.Cvar_Get` with the same defaults used in the engine-OpenGL
   `tr_init.c:1241–1306`. Idempotent.

**Call sites:** the only caller is `code/sdl/sdl_glimp.c:1220`, inside
`GLimp_Init`, guarded by `#ifdef BUILD_RENDERER_VULKAN`. That means
**the bridge runs only if `GLimp_Init` runs** in the Vulkan DLL — which
currently never happens because the Vulkan path goes through
`ri.VKimp_Init`, not `ri.GLimp_Init` (see `code/renderervk/tr_init.c:529-535`).

**Status: integrated but unreachable on the Vulkan path.** The cvar
fallback-storage role is link-time-only (resolves undefined symbols),
which works regardless. The runtime init is dead until something calls
`GLimp_Init` on the Vulkan DLL, or until `VKimp_Init` is taught to call
`RealRTCW_VkBridgeInit` itself. This is **not** the OpenGL handoff
analog — it's a vendoring shim for missing cvar storage, with a
runtime-init suffix that has no current caller.

### 1.3  Vulkan-path call chain

1. Engine boot: `CL_StartHunkUsers` (`code/client/cl_main.c:3341`) calls
   `CL_InitRef` (`code/client/cl_main.c:3388`).
2. `CL_InitRef` builds `dllName = "renderer_sp_vulkan_<arch>.dylib"`
   (`code/client/cl_main.c:3409`), `Sys_LoadDll`s it
   (`code/client/cl_main.c:3411`), resolves
   `GetRefAPI` (`code/client/cl_main.c:3426`).
3. Under `#ifdef BUILD_RENDERER_VULKAN` (`code/client/cl_main.c:3487-3500`),
   the translator builds the BIG refImport_t via
   `CL_BuildVulkanRefImport()` (`code/client/cl_refvulkan.c:129`) and
   passes it into `GetRefAPI`. The local small `ri` populated at lines
   3433-3486 is discarded on this path.
4. Inside the renderer DLL: `GetRefAPI` stores the refImport, returns
   refExport. Engine copies refExport into `re` (line 3509).
5. Subsequent engine call to `re.BeginRegistration` (or first render
   command) triggers `R_Init` (`code/renderervk/tr_init.c:1837`). R_Init
   doesn't call VKimp_Init itself directly; the actual call lives in
   `R_InitDisplay` / equivalent. The reachable call is at
   `code/renderervk/tr_init.c:535`:
   ```
   ri.VKimp_Init( &glConfig );
   ```
6. That slot resolves to `vk_VKimp_Init` at
   `code/client/cl_refvulkan.c:405`, which only checks
   `if (!SDL_window) Com_Error(...)` — **it does not create a window**.
   The check fails because nothing on the Vulkan boot path has
   populated the engine's `SDL_window` yet.

There is no wrapper between `ri.VKimp_Init` and the renderer DLL: the
renderer calls the vtable directly. The translator stub assumes the
engine had already created a window via a prior `GLimp_Init`, which
never runs on a pure-Vulkan boot.

### 1.4  What's compiled into the Vulkan DLL re: window creation

`Q3VKOBJ` (`Makefile:2142-2174, 2176-2177`) includes
`$(B)/rendv/sdl_glimp.o` and `$(B)/rendv/sdl_gamma.o`. Per-target
`override CFLAGS += -DBUILD_RENDERER_VULKAN` at `Makefile:3090-3091`
adds that define for this DLL only.

Inside `code/sdl/sdl_glimp.c`, `BUILD_RENDERER_VULKAN` gates exactly
one thing: the include of `realrtcw_vk_window_bridge.h`
(`code/sdl/sdl_glimp.c:38-43`) and the call to
`RealRTCW_VkBridgeInit()` at line 1220 inside `GLimp_Init`. **There is
no `VKimp_Init` function in this TU.** All the window-creation
code at lines 788, 816, 863 etc. is OpenGL-context-coupled
(`SDL_GL_CreateContext`, `SDL_GL_SetAttribute(...)`), invoked only via
`GLimp_Init`. None of it has a Vulkan-flagged variant.

So: the Vulkan DLL has `GLimp_Init`/`GLimp_EndFrame`/etc. linked in but
**unreachable** on the Vulkan path. The DLL has no entry that creates
a Vulkan-flagged `SDL_Window` today.

---

## Section 2 — Quake3e baseline

### 2.1  Quake3e Vulkan chain

1. Engine `CL_InitRef` at `refs/Quake3e/code/client/cl_main.c` builds a
   single `rimp` struct and installs `rimp.VKimp_Init = VKimp_Init` at
   line 3479 (along with the other `VK_*` slots at 3480-3482).
2. Engine passes `&rimp` into `GetRefAPI` at line 3485.
3. Inside the renderer DLL: `R_Init` reaches the same call we see in
   the vendored copy at `code/renderervk/tr_init.c:535` — calls
   `ri.VKimp_Init(&glConfig)`.
4. Resolution: lands in
   `refs/Quake3e/code/sdl/sdl_glimp.c:694 (VKimp_Init)`. That function:
   - registers cvars (lines 704-709),
   - calls `GLimp_StartDriverAndSetMode(..., qtrue /* Vulkan */)` at
     line 715,
   - resolves `qvkGetInstanceProcAddr` from
     `SDL_Vulkan_GetVkGetInstanceProcAddr` at line 735,
   - calls `IN_Init()` at line 748 (no arg — see signature note below).

The crucial line: `GLimp_StartDriverAndSetMode` is shared between
OpenGL and Vulkan via its 4th boolean argument; same window-creation
code path, branching only inside `SDL_CreateWindow` to add
`SDL_WINDOW_VULKAN` vs `SDL_WINDOW_OPENGL`.

### 2.2  TU placement — Quake3e

`refs/Quake3e/Makefile:1219-1224` puts `sdl_glimp.o`, `sdl_gamma.o`,
`sdl_input.o`, `sdl_snd.o` in `Q3OBJ` (the **engine** binary). The
renderer DLL object lists `Q3REND1OBJ` (line 820), `Q3REND2OBJ` (line
860), `Q3RENDVOBJ` (line 936-967) contain **no `sdl_*` files at all**.

`refs/Quake3e/code/sdl/sdl_glw.h:52` declares
`extern SDL_Window *SDL_window;`. Both `sdl_glimp.c` (definer at
line 50) and `sdl_input.c` (consumer) live in the engine binary,
sharing one TU-level global through this header. The renderer DLLs
have no SDL window state and never link sdl_glimp.c.

### 2.3  Topology mismatch (the architectural delta)

| Concern              | Quake3e                          | RealRTCW                                                |
|----------------------|----------------------------------|---------------------------------------------------------|
| `sdl_glimp.c` link target | Engine binary only          | Both renderer DLLs (`renderer/`, `rendv/`); NOT engine  |
| `sdl_input.c` link target | Engine                      | Engine                                                  |
| `SDL_window` storage | Single global in engine          | Two distinct globals: one in engine sdl_input.c, one in each renderer-DLL sdl_glimp.c |
| `VKimp_Init` defined in | Engine binary (sdl_glimp.c)   | Nowhere — stub in engine cl_refvulkan.c that doesn't create a window |
| `IN_Init` signature   | `void IN_Init(void)`             | `void IN_Init(void *windowData)`                        |
| `IN_Init` call site   | Inside `VKimp_Init` / `GLimp_Init` (same TU, no vtable hop) | Inside renderer DLL `GLimp_Init`, calls `ri.IN_Init(SDL_window)` cross-DLL |

**This is the root architectural mismatch.** RealRTCW inherited the
iortcw split where window-creation lives in the renderer DLL and is
handed back to the engine through `ri.IN_Init`. Quake3e keeps
window-creation engine-side and the renderer DLL never touches SDL.
We vendored their **renderer** without bringing their **engine-side
window TU**, and the OpenGL split-TU pattern doesn't extend naturally
to Vulkan because there's no `VKimp_Init` analog of the renderer-DLL
`GLimp_Init`.

---

## Section 3 — Options

Word budget tight; descriptions intentionally compact.

### 3.1  Option α' — new engine-side TU `realrtcw_sdl_vk_glimp.c`

**Files touched:**
- New `code/sdl/realrtcw_sdl_vk_glimp.c` containing engine-side
  `VKimp_Init`, `VKimp_Shutdown`, `VK_GetInstanceProcAddr`,
  `VK_CreateSurface` (or just `VKimp_Init` + delegation to existing
  stubs in cl_refvulkan.c).
- Maybe new `code/sdl/realrtcw_sdl_vk_glimp.h` with shared mode/cvar
  declarations.
- `Makefile`: add `$(B)/client/realrtcw_sdl_vk_glimp.o` to `Q3OBJ`
  under `BUILD_RENDERER_VULKAN`.
- `code/client/cl_refvulkan.c:218`: rewire `vk_ri.VKimp_Init` to the
  new function.

**Code duplicated:** window-creation skeleton (~150-200 lines from
`sdl_glimp.c:540-940`) — `SDL_CreateWindow` with `SDL_WINDOW_VULKAN`
flag, display/mode resolution, fullscreen handling, error-fallback
loop. Either copy or refactor existing `GLimp_StartDriverAndSetMode`
out of renderer DLL into shared helper (then it's not really duplicated).

**Code shared:** none from the renderer DLL's `sdl_glimp.c` (different
TU); IN_Init, all `IN_*` event handling, sdl_snd, sdl_gamma stay where
they are.

**Distance from Quake3e contract:** moderate. Quake3e puts VKimp_Init
in `sdl_glimp.c` next to `GLimp_Init`; we'd put it in a separate
RealRTCW-prefixed engine TU. Functional behavior identical; merge of
their VKimp_Init changes requires manual port. Symmetric with how
we keep their renderervk vendored verbatim.

**Distance from RealRTCW OpenGL contract:** far. OpenGL path creates
window in renderer DLL; Vulkan path creates it in engine. Two
patterns, one codebase, asymmetric. Reader has to know which
renderer is loaded to find the window-creation code.

**OpenGL removal cost:** ~5 min. Delete the OpenGL DLL's
`sdl_glimp.c`/`sdl_gamma.c` from Q3ROBJ, delete `ri.GLimp_*` slots
from CL_InitRef, delete `IN_Init`'s windowData arg path. The new
Vulkan TU stays put.

**Failure mode if misjudged:** Two SDL_window globals diverge. If
engine creates the Vulkan window but later something does
`vid_restart` and the OpenGL DLL's `SDL_DestroyWindow(SDL_window)`
runs on **its** stale global, no-op or double-free depending on
state. Need to be explicit which TU owns lifecycle. Also: if the new
TU ever links into a build that also has GL renderer active, two
`VKimp_Init`-shaped symbols could collide — namespacing matters.

### 3.2  Option β' — inline everything in `cl_refvulkan.c`

**Files touched:**
- `code/client/cl_refvulkan.c` only — expand `vk_VKimp_Init` from
  a 14-line stub to a real ~200-line implementation. Add
  `vk_GLimp_StartDriverAndSetMode`-equivalent below it.
- No Makefile change. No new file.

**Code duplicated:** same ~150-200 lines as α', copied from
`sdl_glimp.c:540-940`, transformed to Vulkan flag.

**Code shared:** nothing from renderer-DLL `sdl_glimp.c`. Could
share macros (`R_MODE_FALLBACK` etc.) by pulling them into a header,
but only one consumer, so probably not worth a header.

**Distance from Quake3e contract:** farther than α'. Their VKimp_Init
sits next to GLimp_Init in `sdl_glimp.c`; ours would live in a
RealRTCW-specific translator TU. Future merges of Quake3e
`sdl_glimp.c` Vulkan changes (e.g. a new MoltenVK env-var, a
modeset bugfix) require us to mirror those edits into a different
file by hand.

**Distance from RealRTCW OpenGL contract:** far, same way as α'.

**File size projection:** 424 lines today → ~620-700 lines after
adding mode-info table reference, cvar registration, the
SDL_CreateWindow loop with fallback, surface-creation already there.
Approaches the threshold where it splits anyway for readability.

**OpenGL removal cost:** ~5 min (same as α' — Vulkan code is already
self-contained).

**Failure mode if misjudged:** file becomes a wall. cl_refvulkan.c
currently holds vtable translation only; mixing window-creation into
the same TU couples two unrelated concerns. When the inevitable
"why is my window 640x480 and not r_mode -2 desktop" bug hits, it'll
be hard to isolate from the vtable code. Also: cl_refvulkan.c is
already explicit about not including `client.h` to dodge the small/big
refimport collision; pulling window code in may reintroduce that
include pressure (e.g. `glconfig` access patterns).

### 3.3  Option γ' — share `sdl_glimp.c` engine-side too

**Files touched:**
- `Makefile`: add `$(B)/client/sdl_glimp.o` and probably
  `$(B)/client/sdl_gamma.o` to `Q3OBJ`. Decide whether to keep
  `$(B)/renderer/sdl_glimp.o` and `$(B)/rendv/sdl_glimp.o` (causes
  symbol collision at engine link if engine also has its copy).
- `code/sdl/sdl_glimp.c`: split into "engine-owned window state" vs
  "renderer-owned GL bring-up". Two TUs, or one TU with heavier
  #ifdef. Currently sdl_glimp.c has 1300+ lines mixing both.
- `code/sdl/sdl_input.c:60`: remove duplicate definition; rely on
  the now-shared engine copy from sdl_glimp.c.
- `code/renderer/sdl_glimp.c` (vendor copy in renderer DLL): heavy
  surgery to remove window-creation, keep only GL-context bring-up
  and surface accessors. Or just leave `Q3ROBJ` empty of sdl_glimp.o
  and stub `ri.GLimp_Init` engine-side too.
- `code/client/cl_main.c`: install `ri.VKimp_Init = VKimp_Init;` and
  `ri.GLimp_Init = GLimp_Init;` from engine side (equivalent of
  Quake3e's `cl_main.c:3479`).

**Linker collisions:**
- Two `SDL_window` definitions (engine sdl_input.c:60 + engine
  sdl_glimp.c:67) → must remove the sdl_input.c one. Trivial.
- Two `SDL_window` definitions (engine sdl_glimp.c:67 + DLL
  sdl_glimp.c:67) → fine because DLLs have separate symbol spaces;
  but if engine **also** has the renderer DLL's copy via static
  linking (not our case), would collide.
- `ri.IN_Init` vtable slot: would no longer be needed for window
  handoff since IN_Init can directly read engine-local `SDL_window`.
  Probably keep slot for `vid_restart` re-init sequencing.

**Code duplicated:** none — sdl_glimp.c becomes single-sourced like
Quake3e. The win is exactly this: matches upstream.

**Code shared:** maximum — engine and both renderer DLLs draw from one
sdl_glimp.c with #ifdef branches (probably `USE_OPENGL_API` /
`USE_VULKAN_API`).

**Distance from Quake3e contract:** close (this is their topology).
Future merges of Quake3e sdl_glimp.c become near-trivial.

**Distance from RealRTCW OpenGL contract:** very far. This is a
refactor of the OpenGL handoff path, not just an addition for Vulkan.
M3.5 had to fight several side effects of two-globals; γ' removes the
problem class but touches the OpenGL build.

**OpenGL removal cost:** medium. Already on Quake3e topology so the
sdl_glimp.c cleanup is "drop the `#ifdef USE_OPENGL_API` branches".
But you already paid the refactor up front in γ' itself.

**Failure mode if misjudged:** breaks OpenGL build during a Vulkan
phase. The OpenGL path is currently green; γ' regresses it during
the work, and OpenGL is the safety net if Vulkan derails. If γ' is
attempted and not finished, both paths are broken simultaneously.
Also: the refactor of sdl_glimp.c into engine-vs-DLL halves is
manual and error-prone (cvar ownership, init ordering, vid_restart
edge cases). Without a clear before/after invariant, you discover
each split decision as a runtime bug.

### 3.4  Other options spotted

**Option ε' — keep vk_VKimp_Init in cl_refvulkan.c BUT teach it to
call into the renderer-DLL `GLimp_Init` via a new `ri.GLimp_Init`
invocation from engine side first.** I.e., piggyback the existing
OpenGL window-creation path: engine calls `re.GLimp_Init`-equivalent
to create an `SDL_WINDOW_VULKAN`-flagged window, picks up the
window pointer through the existing ri.IN_Init handoff, then proceeds
with Vulkan. This re-uses the renderer DLL's existing window
infrastructure with a Vulkan flag-bit branch added near
`sdl_glimp.c:788 (SDL_CreateWindow)`. Files touched: cl_refvulkan.c
+ sdl_glimp.c (small flag-toggle). Distance from Quake3e: very far
(they don't do this). Distance from RealRTCW OpenGL: very close
(reuses the exact same path with a flag).

Failure mode: the renderer DLL's `GLimp_Init` is heavily OpenGL-coupled
beyond just `SDL_CreateWindow` — it does `SDL_GL_SetAttribute` and
`SDL_GL_CreateContext` (`code/sdl/sdl_glimp.c:760-880`). Splitting
"create window" from "create GL context" inside that TU is itself a
refactor approaching γ' in scope.

**Option ζ' — accept that GLimp_Init runs on the Vulkan DLL.** Today
`code/renderervk/tr_init.c:528-535` chooses `VKimp_Init` for the
Vulkan path. We could instead pre-call `GLimp_Init` (with a Vulkan
flag plumbed in) from the engine **before** R_Init, populate the
engine SDL_window via the existing OpenGL-shape `ri.IN_Init` handoff,
and stub VKimp_Init to just confirm. This is α'/β'-shape but
piggybacks on the existing renderer-DLL window code. The cost: vendor
divergence in `sdl_glimp.c` (needs a Vulkan flag inside `GLimp_StartDriverAndSetMode`),
and the renderer-DLL `sdl_glimp.c` still pretends to be GL-side at
function names, which is misleading.

---

## Section 4 — Wildcards

- **SDL2 vs SDL3:** the worktree is on SDL3 (`code/sdl/sdl_input.c:24`,
  `code/sdl/sdl_glimp.c:24`, `code/client/cl_refvulkan.c:44-45`).
  Quake3e upstream is on SDL2 (`refs/Quake3e/code/sdl/sdl_input.c:24`).
  Their VKimp_Init uses `SDL_Vulkan_GetVkGetInstanceProcAddr()` which
  exists in both SDL2 and SDL3, but signatures differ subtly
  (SDL_Vulkan_CreateSurface gained an allocator arg in SDL3 — our
  cl_refvulkan.c:377 already uses the SDL3 form). Any copy from
  Quake3e VKimp_Init has to be translated to SDL3 idioms.

- **`SDL_WINDOW_VULKAN` flag:** required at `SDL_CreateWindow` time on
  SDL3; cannot be toggled after the window exists. This forces the
  flag decision into the place where the window is born, not after.
  Option ε'/ζ' must plumb the flag into `GLimp_StartDriverAndSetMode`'s
  signature. Currently the function is fixed-fullscreen-and-GL.

- **`vid_restart` behavior:** the OpenGL path destroys and recreates
  the window via `GLimp_StartDriverAndSetMode` because GL context
  needs new pixel format. Vulkan typically needs full swapchain
  recreation but **can** survive without window destruction. Whichever
  option owns Vulkan window creation also owns the vid_restart
  contract — and the OpenGL DLL's `vid_restart` path inside
  `sdl_glimp.c:644-658` unconditionally calls `SDL_DestroyWindow`. If
  the Vulkan DLL gets loaded later and its surface holds a reference,
  this is a UAF.

- **Gamma:** `code/sdl/sdl_gamma.c` is built into both renderer DLLs
  (`Makefile:2122, 2176`). Engine doesn't touch gamma. Whichever
  option you pick, gamma stays in the renderer DLL — but if α' puts
  VKimp_Init engine-side and ri.GLimp_InitGamma is a renderer-DLL
  vtable slot, the Vulkan boot path must still invoke the renderer's
  gamma hookup. Currently it's NULL in cl_refvulkan.c (commented
  intentionally NULL at lines 246-247). M4 may surface this.

- **Dual-window risk:** if Option α' or β' creates a Vulkan window
  engine-side BUT the Vulkan DLL's linked `GLimp_Init` somehow gets
  called (e.g. through a `vid_restart` path that calls the wrong
  function pointer), the DLL's `SDL_CreateWindow` at line 788 fires
  and you have two windows alive — the DLL's `SDL_window` (renderer
  copy) and the engine's `SDL_window` (engine copy) point at
  different `SDL_Window*` objects. Audit `vid_restart`
  reachability per option.

- **MoltenVK / libvulkan.dylib loading:** SDL3 expects the Vulkan
  loader present before `SDL_Vulkan_GetVkGetInstanceProcAddr`. The
  engine binary currently loads SDL3 first thing; whichever TU owns
  VKimp_Init has to ensure SDL_INIT_VIDEO has run. Quake3e's
  IN_Init has a check (`SDL_WasInit(SDL_INIT_VIDEO)`) — RealRTCW's
  does too (`code/sdl/sdl_input.c:1239`). Order of operations
  between Cmd_AddCommand (which currently progresses past in M3.5)
  and SDL_INIT_VIDEO is worth re-checking under each option.

- **CL_SetScaling slot:** `code/renderervk/tr_init.c:543, 556, 562`
  calls `ri.CL_SetScaling(...)` immediately after `ri.VKimp_Init`
  returns. The translator at `code/client/cl_refvulkan.c:233`
  documents CL_SetScaling as intentionally NULL. M4 will trip on
  this regardless of which option you pick — orthogonal to window
  init, but adjacent in the call chain. Same for `CL_IsMinimized`
  on the render-side polling path.

- **IN_Init signature drift:** RealRTCW's IN_Init takes
  `void *windowData`, Quake3e's takes `void`. Any option that imports
  Quake3e's VKimp_Init wholesale must adapt the IN_Init call. Cheapest
  fix: keep RealRTCW signature and pass the just-created window from
  the new VKimp_Init context. Not load-bearing but easy to miss.
