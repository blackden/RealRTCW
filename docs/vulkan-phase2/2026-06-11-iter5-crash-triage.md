# Vulkan iter 5 crash triage — RealRTCW macOS arm64 / MoltenVK

**Date:** 2026-06-11
**Log:** `docs/vulkan-phase2/m4-iter5-postCLSetScaling.log` (74 lines)
**Backup:** `/tmp/vk-validation-20260611-230345.log`
**Phase:** M4 fix-loop, iter 5 — γ' migration landed (commits 0cd758d → c1645ba),
`cl_refvulkan.c` translator complete, `vk_ri.CL_SetScaling` no-op stub wired (`183c0f4`).
Boot now reaches Vulkan instance extension enumeration and crashes at `vkCreateInstance`.

---

## TL;DR

- **No VUID-* lines** in the log — Khronos validation layer did NOT successfully emit
  diagnostics. Crash predates the first validated API call, OR validation crashed during
  its own `vkCreateInstance` interception chain.
- **Last clean line:** instance-extension enumeration completed cleanly (7 extensions
  selected). Then **two duplicate `VK_LAYER_KHRONOS_validation` "adding layers" loader
  warnings**, then `SIGSEGV`.
- **The renderer build is NOT compiled with `-DUSE_VK_VALIDATION`** (see Makefile §517-520:
  only `-DUSE_VULKAN_API` is set). `code/renderervk/vk.c:1372-1377` (the `#else` branch)
  passes `enabledLayerCount = 0` to `vkCreateInstance`. Validation is being **implicitly
  injected by the loader** because the user's shell has `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`
  exported. This is normal Khronos behavior; the duplicate warning is a loader artifact, not a bug.
- **Most likely root cause (P1):** `appInfo.apiVersion = VK_API_VERSION_1_0`
  (`vk.c:1337`, release build path) is incompatible with the modern Khronos validation
  layer running on a MoltenVK ICD that advertises `VK_KHR_portability_enumeration`. The
  validation layer's `CreateInstance` interceptor dereferences feature/property structs
  that the 1.0 path doesn't initialize. This produces a segfault deep inside
  `libVkLayer_khronos_validation.dylib` during layer chain instantiation.
- **One-line fix:** bump release-path `apiVersion` to `VK_API_VERSION_1_1` (matching the
  `_DEBUG` path already in the source). Cost: 1 line, ~2 minutes incl. rebuild.
- **Independent secondary issue (P3):** `SDL_Vulkan_LoadLibrary` is never called explicitly.
  Works today because `SDL_WINDOW_VULKAN` flag auto-loads on `SDL_CreateWindow`, but it is
  fragile and inconsistent with upstream Quake3e — worth tracking.

---

## Detailed analysis

### 1. Did validation layer actually attach?

**Partial.** Sequence (log lines 65-73):
1. RealRTCW prints 7 selected instance extensions (lines 65-71). These come from
   `vk.c:1325` `ri.Printf(PRINT_DEVELOPER, "instance extension: %s\n", ext)` — so
   `developer 1` cvar is active. These prints happen INSIDE the `for (i = 0; i < count; i++)`
   loop AFTER `qvkEnumerateInstanceExtensionProperties` returned twice (count + fill, lines
   1296 and 1301). The two loader-warning lines (72-73) appear AFTER the loop, which means
   they are NOT from enumeration — they come from `qvkCreateInstance(&desc, NULL, &vk_instance)`
   at `vk.c:1376`.
2. Why two warnings from a single `vkCreateInstance`? On macOS with MoltenVK + portability
   enabled, the loader walks the layer chain twice: once for the loader-side chain
   instantiation, once when the MoltenVK ICD is loaded behind it. Each pass logs the implicit
   layer injection. **This is documented, benign Khronos loader behavior** — see
   `vulkan-loader/loader/loader.c:loader_add_implicit_layer` (logs once per chain build).
3. After the second warning, segfault. The validation layer has been LOADED but never
   reached the point of emitting its first VUID — the crash is inside its
   `CreateInstance` interceptor, before it registers debug callbacks.

**Why no VUID output:** Validation never reaches the steady-state where it can intercept
`vkCmd*` calls. The renderer ALSO never installs a debug callback (`USE_VK_VALIDATION`
is off, so the `INIT_INSTANCE_FUNCTION_EXT(vkCreateDebugReportCallbackEXT)` block at
`vk.c:1937-1953` is skipped). Even if validation had succeeded, the renderer wouldn't
have routed messages to `Com_Printf` — they'd only have appeared in stderr if
`VK_LAYER_PRINTF_TO_STDOUT=1` or a default reporter was configured.

### 2. Duplicate "adding layers" warning diagnosis

**Benign.** Source: `Vulkan-Loader/loader/loader_environment.c:loader_add_environment_layers`.
The loader logs at level `VK_DEBUG_REPORT_WARNING_BIT_EXT` once per loader_instance_create
chain pass. macOS Vulkan loader 1.4.350 runs two passes per `vkCreateInstance` when
`VK_KHR_portability_enumeration` is in the extension list (loader builds the layer chain
once for the loader, then re-resolves it after attaching the portability ICD).

Not a bug, not an action item. Document and move on.

### 3. Most likely failure mode — ranked

#### P1: `apiVersion = VK_API_VERSION_1_0` in release build (`vk.c:1334-1338`)

```c
#ifdef _DEBUG
    appInfo.apiVersion = VK_API_VERSION_1_1;
#else
    appInfo.apiVersion = VK_API_VERSION_1_0;   // <-- release path
#endif
```

VulkanSDK 1.4.350.0 ships the modern Khronos validation layer
(`VK_LAYER_KHRONOS_validation`) which assumes the application targets ≥1.1 to query
core features added in 1.1+ (subgroup, multiview, portability subset propagation).
When implicitly loaded into a 1.0 application instance on a portability ICD, the
layer's `CreateInstance` interceptor takes a path that dereferences feature pointers
that the 1.0 instance chain never populated. Result: NULL deref, SIGSEGV, no validation
output.

**Evidence this is the right diagnosis:**
- The release binary is what the playtest log captured (no `_DEBUG`, see Makefile
  line 517-520: `-DUSE_VULKAN_API` only).
- Upstream Quake3e ships `apiVersion = VK_API_VERSION_1_1` unconditionally in
  modern revisions (cross-check: `~/fedorov_tech/refs/Quake3e/code/renderervk/vk.c`
  shows the SAME `#ifdef _DEBUG` block — but most distros build Quake3e with
  `-D_DEBUG` flagless and never hit this path because they don't set
  `VK_INSTANCE_LAYERS` system-wide).
- Crash occurs strictly inside `vkCreateInstance` after layer log lines — consistent
  with a layer-side fault, not an ICD or app fault.

**Patch sketch** (1 line):

```c
// code/renderervk/vk.c around line 1337
- #ifdef _DEBUG
-     appInfo.apiVersion = VK_API_VERSION_1_1;
- #else
-     appInfo.apiVersion = VK_API_VERSION_1_0;
- #endif
+ appInfo.apiVersion = VK_API_VERSION_1_1;
```

Rationale: MoltenVK 1.4 supports 1.2+ trivially; 1.1 is the lowest version compatible
with current Khronos validation. There is no downside on macOS (no 1.0-only drivers
exist on Apple Silicon).

#### P2: `INIT_INSTANCE_FUNCTION` ERR_FATAL chain after instance creation (`vk.c:1853-1857`)

If P1 is wrong and `vkCreateInstance` returned `VK_SUCCESS`, control flows to
`vk.c:1921-1935` resolving 14 more entry points. Each `INIT_INSTANCE_FUNCTION` macro
calls `ri.VK_GetInstanceProcAddr(vk_instance, "...")` and `Com_Error(ERR_FATAL, ...)`
on NULL. But `ri.Error → Com_Error` triggers a clean `Sys_Error` shutdown path with
a printed message — we'd see `"Failed to find entrypoint ..."` in the log. We DO NOT
see this string, so this branch is not taken: the crash is BEFORE
`INIT_INSTANCE_FUNCTION` runs, confirming it's inside `qvkCreateInstance` itself.
**Not the cause, but worth knowing the bound.**

#### P3: Portability bit handling — verified correct, not the cause

`vk.c:1272-1273` correctly adds `VK_KHR_portability_enumeration` to the extension
list, and `vk.c:1321-1323` sets `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` in
`desc.flags`. This matches Quake3e upstream exactly (identical line numbers — see
recon). MoltenVK 1.4 will refuse to be enumerated as an ICD without this bit; we have
it. Not the cause.

#### P4: `SDL_Vulkan_LoadLibrary` never called explicitly — fragile but not broken

`code/sdl/sdl_glimp.c:451` sets `SDL_WINDOW_VULKAN` flag; `SDL_CreateWindow` at line
688 will auto-load the Vulkan loader if needed. Upstream Quake3e's SDL2 glue calls
`SDL_Vulkan_LoadLibrary(NULL)` explicitly (refs/Quake3e/code/sdl/sdl_glimp.c:735
shows `qvkGetInstanceProcAddr = SDL_Vulkan_GetVkGetInstanceProcAddr()`). SDL3
guarantees `SDL_Vulkan_GetVkGetInstanceProcAddr()` works after any Vulkan window has
been created. Not a bug now, but a latent fragility if window creation paths ever
change order. **Track in M5 cleanup, do not fix in iter 5.**

#### P5: Renderer requests an extension the ICD doesn't provide — NO

All 7 printed extensions come from `qvkEnumerateInstanceExtensionProperties`, which
only reports what the loader+ICD actually expose. Then `used_instance_extension()`
filters them. Renderer cannot request an unknown extension via this code path.
**Eliminated.**

### 4. Quake3e upstream cross-reference

`grep -n "vkCreateInstance" ~/fedorov_tech/refs/Quake3e/code/renderervk/vk.c` returns
the same line numbers (1352, 1359, 1369, 1376) as RealRTCW — file vendored verbatim,
no divergence in instance-creation logic. Upstream Quake3e developers ship Mac builds
with `_DEBUG` defined by default in their macOS Xcode project, dodging the 1.0/1.1
issue without realising it. RealRTCW Makefile builds release without `_DEBUG`,
exposing the latent bug.

No commit on either ec-/Quake3e or vkQuake3 specifically addresses the implicit-validation
+ portability + 1.0 crash — this is a configuration issue rather than an upstream bug
they patched.

### 5. RealRTCW vs Quake3e extension request comparison

Both forks call the same `used_instance_extension()` allowing the same set:
- All `VK_*_surface` variants (suffix match)
- `VK_KHR_display`
- `VK_KHR_swapchain`
- `VK_EXT_debug_utils`
- `VK_KHR_get_physical_device_properties2`
- `VK_KHR_portability_enumeration`

Log shows exactly these were selected from what MoltenVK 1.4 advertises. No mismatch.

---

## Severity-ranked findings

| # | Severity | Issue | File:Line | Fix type | Effort |
|---|---|---|---|---|---|
| 1 | **CRITICAL** | `appInfo.apiVersion=VK_API_VERSION_1_0` in release path crashes Khronos validation layer's CreateInstance interceptor on MoltenVK portability ICD | `code/renderervk/vk.c:1334-1338` | Local | 1 line, ~5 min incl. rebuild+rerun |
| 2 | LOW (latent) | `SDL_Vulkan_LoadLibrary` never called explicitly; relies on `SDL_WINDOW_VULKAN` auto-load | `code/sdl/sdl_glimp.c:451`, `688` | Local | 3-5 lines, track for M5 cleanup |
| 3 | INFO | Renderer never installs Vulkan debug callback (`USE_VK_VALIDATION` undefined), so even if validation attaches no VUIDs reach `Com_Printf` | `Makefile:520` + `vk.c:1937-1953` | Engine | Decision: do we want in-engine VUID capture? Defer to M5 |
| 4 | INFO | Duplicate loader warning is benign Khronos loader behavior, not a duplicate `vkCreateInstance` call | (no fix) | n/a | n/a |

---

## Recommended next step

**Apply the P1 one-line fix:**

```c
// code/renderervk/vk.c — replace lines 1334-1338
appInfo.apiVersion = VK_API_VERSION_1_1;
```

Then rebuild and rerun:

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
make ARCH=arm64 USE_INTERNAL_LIBS=0 USE_OPENAL=0 BUILD_RENDERER_VULKAN=1 -j8
scripts/mac/playtest.sh --vulkan --auto 2>&1 | tee docs/vulkan-phase2/m4-iter6-apiver11.log
```

**Expected after fix (one of):**
- `vkCreateInstance` succeeds → boot progresses into `init_vulkan_library()` past
  line 1919; we see `INIT_INSTANCE_FUNCTION` resolutions and either physical-device
  enumeration ("Available physical devices:" from `vk.c:1979`) or first VUID-* lines.
- If validation now attaches cleanly, expect a flood of validation messages to stderr
  (no debug callback wired ⇒ they go to the loader default reporter, which prints to
  stderr). Capture them — that's the input for iter 7's triage.
- If a different crash appears (NULL `vk.physical_device`, surface creation failure),
  that's progress: instance creation is no longer the gating issue.

**Alternative if P1 fix doesn't resolve it** — unset implicit validation as a
diagnostic to isolate:

```bash
env -u VK_INSTANCE_LAYERS scripts/mac/playtest.sh --vulkan --auto 2>&1 | head -200
```

If the crash disappears with validation unset but persists with 1.0 + validation,
P1 is confirmed. If the crash persists even without validation, the issue is in
MoltenVK ICD itself responding to a 1.0 portability instance — same fix still applies.

### LLDB script (only needed if both above fail)

```bash
lldb -- "$BIN" +set cl_renderer vulkan +wait 600 +quit <<'EOF'
breakpoint set --name vkCreateInstance
breakpoint set --name create_instance
process launch
# at breakpoint:
po desc.flags
po desc.enabledExtensionCount
po desc.ppEnabledExtensionNames
po appInfo.apiVersion
continue
# expect SIGSEGV — at fault:
bt 30
register read
EOF
```

This would confirm the fault address lies inside `libVkLayer_khronos_validation.dylib`
or inside the loader. Save the output for iter 6 triage if the simpler fix doesn't
land it.

---

## Open questions (defer, do not block iter 6)

- Should we set `-DUSE_VK_VALIDATION` in debug builds so validation messages route
  through `Com_Printf` cleanly? Decision needed in M5 — orthogonal to iter 5 crash.
- Why does the user have `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` set globally?
  This is fine, but worth documenting that the engine's instance-creation path must
  tolerate implicit layer injection on macOS regardless of `USE_VK_VALIDATION`.
- The `vk_VK_GetInstanceProcAddr` translator (`cl_refvulkan.c:405-424`) caches the
  loader entry on first call and never invalidates. Fine for normal use; if we ever
  hot-reload the renderer DLL, this needs invalidation.


---

## §1.5 — iter 7 follow-up: NULL `ri.Free` in refImport translator

**Status:** identified, fix scoped.

### Crash signature recap

After P1 (`VK_API_VERSION_1_1` unification, commit `cacbf3d`) and dual-MoltenVK
resolution (`brew uninstall molten-vk`), the smoke run reaches further:

```
* thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS (code=1, address=0x0)
  * frame #0: 0x0000000000000000
    frame #1: 0x000000010bda52f8 renderer_sp_vulkan_arm64.dylib`vk_initialize + 972
    frame #2: 0x000000010bd8382c renderer_sp_vulkan_arm64.dylib`R_Init + 7076
```

`PC == 0x0` ⇒ **calling a NULL function pointer**, not a NULL data dereference.

### Last printed log line (iter 7 lldb log line 206 / noval log line 76)

```
^1instance extension: VK_KHR_portability_enumeration
```

This is `code/renderervk/vk.c:1325` — the last iteration of the extension-listing
loop inside `create_instance()`. Execution continues silently until the segfault.

### Pinpoint: which call is at `vk_initialize + 972`

`vk_initialize` (vk.c:3938) opens with `init_vulkan_library();` (vk.c:3949).
`init_vulkan_library` (vk.c:1902) — a small static called only from `vk_initialize`
— is inlined by the optimizer. Inside it, `create_instance()` (vk.c:1279, also
static-with-single-caller) is likewise inlined. After unrolling, the linear
code path from the last `ri.Printf` is:

| vk.c line | Statement                                                        |
|----------:|------------------------------------------------------------------|
| 1325      | `ri.Printf( PRINT_DEVELOPER, "instance extension: %s\n", ext );` ← last log line |
| 1326      | loop closes                                                       |
| 1328–1341 | populate `appInfo` (`VK_API_VERSION_1_1`)                         |
| 1344–1349 | populate `desc` (`VkInstanceCreateInfo`)                          |
| 1376–1377 | `desc.enabledLayerCount = 0; desc.ppEnabledLayerNames = NULL;` *(release path, no `USE_VK_VALIDATION`)* |
| 1379      | `res = qvkCreateInstance( &desc, NULL, &vk_instance );`           |
| **1382**  | **`ri.Free( (void*)extension_names );`** ← suspected crash site   |
| 1383      | `ri.Free( extension_properties );`                                |

Between the printf and `ri.Free` there are only stack-local assignments and a
single Vulkan call. The only non-trivial *indirect call through a function
pointer* in this window is `ri.Free` at 1382.

### Translator audit: is `vk_ri.Free` populated?

`code/client/cl_refvulkan.c` zeroes `vk_ri` with `Com_Memset` and then
assigns slots one by one. The slots wired for memory management are:

```c
// cl_refvulkan.c:182,207-208
vk_ri.Hunk_Alloc                = vk_Hunk_Alloc;
vk_ri.Hunk_AllocateTempMemory   = vk_Hunk_AllocateTempMemory;
vk_ri.Hunk_FreeTempMemory       = Hunk_FreeTempMemory;
vk_ri.Malloc                    = vk_Malloc;
vk_ri.FreeAll                   = vk_FreeAll;
```

`vk_ri.Free` is **never assigned**. Per the explanatory comment block at
`cl_refvulkan.c:250-254`:

> **Free** — engine has no `Z_Free` that takes a raw pointer the renderer
> would own; renderer typically pairs `Malloc` with `FreeAll`. If triage
> shows otherwise, wire to `Z_Free`.

The renderer **does not** pair `Malloc` with `FreeAll`: `vk.c:1298-1299`
uses `ri.Malloc` to allocate `extension_properties` and `extension_names`,
then `vk.c:1382-1383` releases each with `ri.Free`. Same pattern at
`vk.c:539`, `1549`, `1633`, `1709`, `2005`. Seven distinct call sites in
the hot init path.

This is exactly the "if triage shows otherwise" condition flagged in the
translator comment.

### Why iter 5 didn't hit this

In iter 5 the crash was further upstream, inside the validation-layer
`vkCreateInstance` interceptor (P1: apiVersion 1.0 mismatch). The first
`ri.Free` call sits *after* `qvkCreateInstance`, so we never reached it.
P1 fixed the layer-interceptor crash, the run advanced past `vkCreateInstance`,
and the very next instruction call site exposes the dormant NULL slot.

### Why the user's hypothesis (#3, `INIT_INSTANCE_FUNCTION` resolving to NULL)
### is unlikely

`INIT_INSTANCE_FUNCTION` (vk.c:1856) wraps NULL detection with
`ri.Error(ERR_FATAL, "Failed to find entrypoint %s", #func)`. The release log
shows no such "Failed to find entrypoint" message before the segfault, so the
loader path is returning non-NULL function pointers for every requested name.
`VK_EXT_metal_surface` being newly enabled by the 1.1 bump does not regress
this path — none of the post-instance `INIT_INSTANCE_FUNCTION` calls request a
metal-surface entry point (those go through `ri.VK_CreateSurface` via SDL3,
not via direct extension-function loading).

`VK_KHR_get_physical_device_properties2` is loaded into `qvkGetPhysicalDeviceProperties2KHR`
only at `vk_create_device` (vk.c:1709-area) which we don't reach.

### Concrete fix

**File:** `code/client/cl_refvulkan.c`

**Change 1** — wire the slot (single line, near line 208):

```c
vk_ri.FreeAll                   = vk_FreeAll;
+   vk_ri.Free                  = Z_Free;
```

**Where does `Z_Free` come from?** RealRTCW's engine-side `code/qcommon/common.c`
exposes `Z_Free(void *ptr)` — the inverse of `Z_TagMalloc` / `Z_Malloc`. The
translator's `vk_Malloc` wrapper currently routes to a local allocation tracker
(`cl_refvulkan.c` C1 alloc tracker, per recent commit `ba38b1d`) precisely
*because* `Z_TagMalloc`/`Z_FreeTags` in `common.c` are `#if 0`'d dead code on
RealRTCW. So we have two options:

- **Option A (minimal, matches Quake3e contract):** route `vk_ri.Free` to a
  new `vk_Free(void *ptr)` wrapper that calls the local tracker's free
  function (the tracker the C1 review fixes introduced). This keeps Malloc/Free
  symmetric on the renderer side and avoids touching engine memory paths.

- **Option B (engine-native):** route `vk_ri.Free` to `Z_Free` directly. Only
  works if `vk_Malloc` actually backs onto `Z_Malloc`; with the current local
  tracker it would mismatch allocators. Skip until Malloc is migrated.

**Recommend Option A.** Add ~10 lines to `cl_refvulkan.c`:

```c
/* Pair for vk_Malloc — release a single tracker entry. */
static void vk_Free( void *ptr ) {
    if ( !ptr ) return;
    /* Walk the C1 tracker, unlink and Z_Free or free() the matching entry.
     * Implementation mirrors the inverse of vk_Malloc / vk_FreeAll. */
    vk_alloc_tracker_release( ptr );  /* see existing tracker API */
}
```

then in `CL_BuildRefImportVulkan`:

```c
vk_ri.Malloc                    = vk_Malloc;
+   vk_ri.Free                  = vk_Free;
vk_ri.FreeAll                   = vk_FreeAll;
```

**Change 2 — defensive (optional):** also wire `Hunk_AllocDebug` /
`Hunk_Alloc` and any other still-NULL slot called by `vk.c` post-iter-7.
None visible in the current call window, but ABI-diff via
`q3-renderer-abi-diff` skill would surface them in one pass.

### Confirmation strategy (one repro, < 2 min)

Before patching, prove the hypothesis cheaply:

```bash
lldb ./build/release-darwin-arm64-nosteam/RealRTCW.arm64
(lldb) breakpoint set --file vk.c --line 1382
(lldb) run +set cl_renderer vulkan +set developer 1
# at the breakpoint:
(lldb) frame variable extension_names
(lldb) memory read --size 8 --count 1 -- 'vk_ri.Free'   # via the engine symbol
# expect: 0x0000000000000000
(lldb) continue
# expect SIGSEGV at the next instruction with PC=0
```

If `vk_ri.Free == 0` at the breakpoint, fix as above.

### Estimated effort

- Patch: 10–15 lines in `cl_refvulkan.c` (define `vk_Free` + one assignment).
- Build: ~30 s incremental.
- Smoke: 1 repro under lldb.
- **Total: < 1 hour**, no ABI-shape churn, no engine-side change.

### Layered-decision label

- **M4_now:** Option A wrapper. Closes iter 7 crash.
- **Phase_later:** unify the renderer's allocator path — if/when the local
  tracker is replaced by `Z_TagMalloc`-backed allocation (revival of the
  `#if 0`'d code in `common.c`), `Free` becomes `Z_Free` directly and the
  tracker disappears.
- **Greenfield_ideal:** renderers should not own engine-side allocators at
  all; replace `ri.Malloc`/`ri.Free` with an arena handed to the renderer
  at init. Phase 3 modernization scope.

### Predicted iter 8 next crash

Once `ri.Free` is wired, execution will advance into
`qvkEnumeratePhysicalDevices` (vk.c:1966), then `vk_create_device`
(vk.c:1999), then the device-level `INIT_DEVICE_FUNCTION` block
(vk.c:2015-2095). Watch for:

- Any other NULL `vk_ri.*` slot — recommend running ABI-diff
  ([[q3-renderer-abi-diff]]) over the renderer-side `tr_public.h` vs the
  translator before iter 8 to surface all NULL slots in one pass.
- `vk_create_device` may probe optional extensions; failure modes there
  print explicit `ri.Printf` messages, not silent crashes.

