# M4 iter 8 — comprehensive NULL-slot sweep of `refimport_t`

**Date:** 2026-06-11
**Author:** triage subagent
**Scope:** every slot in the BIG `refimport_t` (Quake3e shape, declared at
`code/renderercommon/tr_public.h` lines 144–244), cross-referenced against
`code/client/cl_refvulkan.c::CL_BuildVulkanRefImport()` wirings and against
every `ri.<SLOT>` callsite in `code/renderervk/`.

Iter-by-iter NULL-discovery loop (iter 5 `CL_SetScaling`, iter 7 `vk_Free`,
iter 8 ???) is being replaced by this one-shot sweep.

---

## 1. Iter-8 root cause (TL;DR)

**Crash at `R_Init + 7092` is `ri.GLimp_InitGamma( &glConfig )` in `code/renderervk/tr_init.c:583`.**

Proof chain:

1. `nm` says `_R_Init` lives at DLL offset `0x15c88`. LR in CrashReport =
   `0x10ee7383c`, DLL base = `0x10ee5c000`, so LR offset = `0x1783c`.
   `0x1783c − 0x15c88 = 0x1bb4 = 7092` ✓.
2. Disassembly of `R_Init` around `R_Init + 7088 / +7092`:
   ```
   + 7068  fmov  s0, #2.00000000          ; scale = 2.0f
   + 7072  bl    _vk_initialize
   + 7076  str   wzr, [x21, #0x2c1c]      ; glConfig.deviceSupportsGamma = qfalse
   + 7080  ldr   x8, [x19, #0x170]        ; x8 = ri[+0x170]
   + 7084  mov   x0, x21                  ; x0 = &glConfig
   + 7088  blr   x8                       ; <<-- NULL function call, returns LR=+7092
   + 7092  ldr   w8, [x21, #0x2c1c]       ; reload deviceSupportsGamma
   ```
3. x19 = `&ri` (CrashReport says x20 holds the `ri` symbol; x19 is the
   spilled base register the compiler uses for the indirect call).
   Offset 0x170 (368 bytes) into `refimport_t` is, per layout computed from
   the header:
   ```
   +0x170 ( 368)  GLimp_InitGamma    <-- iter 8 crash site
   ```
4. Source confirms it: `tr_init.c:583` calls `ri.GLimp_InitGamma( &glConfig )`
   unconditionally after `vk_initialize()` returns, before the cvar
   `r_ignorehwgamma` is consulted (lines 581–588). It is **always** invoked
   on the Vulkan boot path; the "OpenGL window slot, not Vulkan" comment in
   `cl_refvulkan.c:254` is wrong.

x0 = &glConfig (== x21 in the register dump) further confirms the signature
matches `void GLimp_InitGamma(glconfig_t *)`. Same `glConfig` pointer is
stored at +7076 and reloaded at +7092 — gamma init mutates
`glConfig.deviceSupportsGamma`, the renderer reads it on the next line.

---

## 2. Per-slot table (BIG `refimport_t` from `code/renderercommon/tr_public.h`)

Wiring status legend:
- **WIRED-direct** — points at engine function (e.g. `Cvar_Get`)
- **WIRED-wrapper** — points at a `vk_*` wrapper
- **NULL-documented** — appears in the "intentionally NULL" comment block
  (`cl_refvulkan.c:235-265`)
- **NULL-omission** — slot exists in struct, has no wiring, is NOT in the
  NULL-comment list (most dangerous: silent landmine)

Severity legend:
- **CRITICAL** — invoked from R_Init / vk_initialize boot path → crash before
  first frame
- **HIGH** — invoked from per-frame hot path (vk.c presentation, tr_cmds.c)
- **MEDIUM** — invoked from vid_restart, screenshot, shutdown, gamma adjust
- **LOW** — present in source but only reachable from cold paths
- **ZERO** — no callers anywhere in `renderervk/`

For brevity, "ALL CALLERS" enumerates renderervk file:line for currently-
NULL slots only. WIRED slots are not enumerated.

| # | Slot | Sig (cond.) | Wiring | Callers in renderervk/ | Severity | Recommended fix |
|---|------|-------------|--------|------------------------|----------|------------------|
|  1 | `Printf` | `void(printParm_t,const char*,...)` | WIRED-wrapper → `CL_RefPrintf` | (many) | n/a | — |
|  2 | `Error` | `void(errorParm_t,const char*,...)` NORETURN | WIRED-direct → `Com_Error` | (many) | n/a | — |
|  3 | `Milliseconds` | `int(void)` | WIRED-direct → `Sys_Milliseconds` | tr_init `gls.initTime` etc. | n/a | — |
|  4 | `Microseconds` | `int64_t(void)` | WIRED-wrapper → `vk_Microseconds` | (timing) | n/a | — |
|  5 | `Hunk_Alloc` (or `Hunk_AllocDebug` if HUNK_DEBUG) | `void*(size_t,ha_pref)` | WIRED-wrapper → `vk_Hunk_Alloc` | (many) | n/a | — |
|  6 | `Hunk_AllocateTempMemory` | `void*(size_t)` | WIRED-wrapper → `vk_Hunk_AllocateTempMemory` | image loaders | n/a | — |
|  7 | `Hunk_FreeTempMemory` | `void(void*)` | WIRED-direct → `Hunk_FreeTempMemory` | image loaders | n/a | — |
|  8 | `Malloc` | `void*(size_t)` | WIRED-wrapper → `vk_Malloc` | vk.c init | n/a | — |
|  9 | `Free` | `void(void*)` | WIRED-wrapper → `vk_Free` (iter 7 fix) | vk.c:1382-83 | n/a | — |
| 10 | `FreeAll` | `void(void)` | WIRED-wrapper → `vk_FreeAll` | shutdown | n/a | — |
| 11 | `Cvar_Get` | `cvar_t*(...)` | WIRED-direct | r_* cvar registration | n/a | — |
| 12 | `Cvar_Set` | `void(name,val)` | WIRED-direct | tr_init | n/a | — |
| 13 | `Cvar_SetValue` | `void(name,float)` | WIRED-direct | tr_init | n/a | — |
| 14 | `Cvar_CheckRange` | `void(cv,min,max,validator)` | WIRED-wrapper (no-op) | R_Register | n/a | — |
| 15 | `Cvar_SetDescription` | `void(cv,desc)` | WIRED-wrapper (no-op) | R_Register | n/a | — |
| 16 | `Cvar_SetGroup` | `void(cv,group)` | WIRED-wrapper (no-op) | R_Register | n/a | — |
| 17 | `Cvar_CheckGroup` | `int(group)` | WIRED-wrapper (no-op) | tr_init | n/a | — |
| 18 | `Cvar_ResetGroup` | `void(group,bool)` | WIRED-wrapper (no-op) | tr_init | n/a | — |
| 19 | `Cvar_VariableStringBuffer` | `void(name,buf,size)` | WIRED-direct | tr_init | n/a | — |
| 20 | `Cvar_VariableString` | `const char*(name)` | WIRED-direct (cast) | tr_init | n/a | — |
| 21 | `Cvar_VariableIntegerValue` | `int(name)` | WIRED-direct | tr_init | n/a | — |
| 22 | `Cmd_AddCommand` | `void(name,fn)` | WIRED-direct (cast) | tr_init `R_Register` | n/a | — |
| 23 | `Cmd_RemoveCommand` | `void(name)` | WIRED-direct | shutdown | n/a | — |
| 24 | `Cmd_Argc` | `int(void)` | WIRED-direct | tr_init cmd handlers | n/a | — |
| 25 | `Cmd_Argv` | `const char*(int)` | WIRED-direct (cast) | tr_init cmd handlers | n/a | — |
| 26 | `Cmd_ExecuteText` | `void(cbufExec_t,text)` | WIRED-direct → `Cbuf_ExecuteText` | tr_init | n/a | — |
| 27 | `CM_ClusterPVS` | `byte*(int)` | **NULL-documented** | `tr_world.c:794` | MEDIUM | Wire engine `CM_ClusterPVS` — see §3.1 |
| 28 | `CM_DrawDebugSurface` | `void(void(*)(int,int,float*))` | **NULL-documented** | `tr_backend.c:1366` | LOW | Wire engine `CM_DrawDebugSurface` if symbol exposed; else cold-path stub OK |
| 29 | `FS_ReadFile` | `int(qpath,**buf)` | WIRED-wrapper | (many) | n/a | — |
| 30 | `FS_FreeFile` | `void(buf)` | WIRED-direct | (many) | n/a | — |
| 31 | `FS_ListFiles` | `char**(name,ext,*n)` | WIRED-direct | shader load | n/a | — |
| 32 | `FS_FreeFileList` | `void(**list)` | WIRED-direct | shader load | n/a | — |
| 33 | `FS_WriteFile` | `void(qpath,buf,size)` | WIRED-direct | screenshot | n/a | — |
| 34 | `FS_FileExists` | `qboolean(file)` | WIRED-direct | screenshot | n/a | — |
| 35 | `CIN_UploadCinematic` | `void(handle)` | WIRED-direct | tr_image | n/a | — |
| 36 | `CIN_PlayCinematic` | `int(...)` | WIRED-direct | tr_image | n/a | — |
| 37 | `CIN_RunCinematic` | `e_status(handle)` | WIRED-direct | tr_image | n/a | — |
| 38 | `CL_WriteAVIVideoFrame` | `void(const byte*,int)` | **NULL-documented** | `tr_init.c:1181,1210` | MEDIUM | Wire engine `CL_WriteAVIVideoFrame` (sig matches) |
| 39 | `CL_SaveJPGToBuffer` | `size_t(...)` | **NULL-documented** | `tr_init.c:1178` | MEDIUM | Stub — AVI motion-jpeg path; deferred till AVI cap. Add explicit no-op or wire `RE_SaveJPGToBuffer` if present |
| 40 | `CL_SaveJPG` | `void(...)` | **NULL-documented** | `tr_init.c:833` | MEDIUM | Wire to engine screenshot writer if available; else write `vk_CL_SaveJPG` stub that calls `FS_WriteFile` with raw TGA fallback OR Com_Printf-warn and skip |
| 41 | `CL_LoadJPG` | `void(...)` | **NULL-documented** | (none) | ZERO | leave NULL |
| 42 | `CL_IsMinimized` | `qboolean(void)` | **NULL-documented** | `tr_cmds.c:89,95`; `tr_init.c:1069`; `vk.c:7371,7517,7543,7607` | **CRITICAL** | Wire to engine. Either expose `CL_VideoMinimized` (if exists) or write `vk_CL_IsMinimized` stub returning `qfalse` (per-frame; cannot stay NULL) — see §3.2 |
| 43 | `CL_SetScaling` | `void(float,int,int)` | WIRED-wrapper (iter 5 fix) | tr_init:543,556,562 | n/a | — |
| 44 | `Sys_SetClipboardBitmap` | `void(const byte*,int)` | **NULL-documented** | `tr_init.c:952` | LOW (screenshot-copy path only) | Add `vk_Sys_SetClipboardBitmap` no-op stub for safety; not hit on boot |
| 45 | `Sys_LowPhysicalMemory` | `qboolean(void)` | WIRED-direct | tr_init | n/a | — |
| 46 | `Com_RealTime` | `int(qtime_t*)` | WIRED-direct | screenshot/log | n/a | — |
| 47 | `GLimp_InitGamma` | `void(glconfig_t*)` | **NULL-documented (WRONGLY)** | **`tr_init.c:583`** (boot, unconditional) | **CRITICAL — iter 8** | Wire `vk_GLimp_InitGamma` stub that sets `config->deviceSupportsGamma = qfalse;` and returns. SDL3 can manage gamma but Vulkan path doesn't use it yet — see §3.3 |
| 48 | `GLimp_SetGamma` | `void(r[256],g[256],b[256])` | **NULL-documented** | `tr_image.c:1714,1717,1724` | HIGH | Called from `R_SetColorMappings`, which runs at startup AND on every `r_gamma` cvar change. Must be non-NULL. Wire `vk_GLimp_SetGamma` no-op (or route to SDL3 gamma ramp once we add support) — see §3.4 |
| 49 | `GLimp_Init` | `void(glconfig_t*)` | **NULL-documented** | `tr_init.c:570` (in `#ifdef USE_VULKAN`-else branch only) | ZERO (Vulkan build) | Leave NULL — guarded by `#ifndef USE_VULKAN` |
| 50 | `GLimp_Shutdown` | `void(qboolean)` | **NULL-documented** | `tr_init.c:2006-7` (NULL-checked in source) | LOW | Source already does `if ( ri.GLimp_Shutdown )` guard. Safe to leave NULL |
| 51 | `GLimp_EndFrame` | `void(void)` | **NULL-documented** | `tr_backend.c:1769` | LOW (only on OpenGL backend path) | Confirm `#ifdef USE_VULKAN` guards this. If yes leave NULL; otherwise add no-op stub |
| 52 | `GL_GetProcAddress` | `void*(const char*)` | **NULL-documented** | `tr_init.c:224,410-412,461-462` (under `#ifndef USE_VULKAN` paths) | ZERO (Vulkan build) | Leave NULL — OpenGL-only |
| 53 | `VKimp_Init` | `void(glconfig_t*)` | WIRED-direct → engine `VKimp_Init` | tr_init:535 | n/a | — |
| 54 | `VKimp_Shutdown` | `void(qboolean)` | WIRED-direct → engine `VKimp_Shutdown` | shutdown | n/a | — |
| 55 | `VK_GetInstanceProcAddr` | `void*(VkInstance,name)` | WIRED-wrapper | vk.c boot | n/a | — |
| 56 | `VK_CreateSurface` | `qboolean(VkInstance,*VkSurfaceKHR)` | WIRED-wrapper | vk.c boot | n/a | — |

### NULL-omission audit

Comparing struct fields (lines 146–243 of tr_public.h) against the union of
WIRED slots and NULL-documented slots in `cl_refvulkan.c`, **every slot is
accounted for**. There are no NULL-omission entries (silent landmines).

The NULL-documented list IS the complete set of unwired slots. The iter-8
bug is not a missing entry but a **wrong-categorization**: `GLimp_InitGamma`
is in the "OpenGL window slot, not Vulkan" sublist, yet the Vulkan tr_init.c
actually invokes it.

---

## 3. Recommended batch — wire next iter (iter 9)

Apply these together; collectively they should clear all known boot-path
NULL crashes and remove the per-frame swap-chain landmine.

### 3.1 `vk_CM_ClusterPVS` — wrap engine symbol (MEDIUM, deferred safe)

Path: only hit when `R_AddWorldSurfaces` runs against a loaded BSP
(`tr_world.c:794`). Not hit during R_Init. Engine's `CM_ClusterPVS` is
exported via `cm_public.h`. Wire:

```c
vk_ri.CM_ClusterPVS         = CM_ClusterPVS;  // engine: byte *CM_ClusterPVS(int cluster)
```

If the symbol isn't visible to the engine TU (RealRTCW exposed only via
clipmodel module), use a forward declaration as in `cl_refvulkan.c` for
`VKimp_Init`.

### 3.2 `vk_CL_IsMinimized` — per-frame stub (CRITICAL for vk.c hot path)

Six callsites in vk.c presentation logic and tr_cmds.c. Hit every frame.

Engine doesn't have `CL_VideoMinimized` exported; safest stub:

```c
static qboolean vk_CL_IsMinimized( void ) {
    /* SDL3 path: query SDL_GetWindowFlags(SDL_window) & SDL_WINDOW_MINIMIZED.
     * Until we wire that, returning qfalse is safe — renderer just skips
     * minimized-state optimisations. */
    return qfalse;
}
...
vk_ri.CL_IsMinimized        = vk_CL_IsMinimized;
```

This is **the second iter-8 landmine** — even after fixing GLimp_InitGamma,
the very next access (the first frame's `vk_begin_frame`) will NULL-deref
on `vk.c:7371`. Wire it in the same batch.

### 3.3 `vk_GLimp_InitGamma` — iter-8 fix (CRITICAL)

```c
static void vk_GLimp_InitGamma( glconfig_t *config ) {
    /* RealRTCW Vulkan path does not implement HW gamma yet. Declare
     * the device as not supporting it so the renderer falls back to
     * software gamma table (s_gammatable) without trying to push it
     * to a display LUT. Caller will then bypass ri.GLimp_SetGamma()
     * if r_ignorehwgamma is set; otherwise it will call SetGamma
     * which we also stub (§3.4). */
    config->deviceSupportsGamma = qfalse;
}
...
vk_ri.GLimp_InitGamma       = vk_GLimp_InitGamma;
```

Also: REMOVE `GLimp_InitGamma` from the "intentionally NULL OpenGL window
slot" comment list (cl_refvulkan.c:254) — the comment is incorrect for
Quake3e's tr_init.c (line 583 calls it unconditionally on the Vulkan path).

### 3.4 `vk_GLimp_SetGamma` — paired with InitGamma (HIGH)

Even with `deviceSupportsGamma = qfalse`, `R_SetColorMappings` still calls
`ri.GLimp_SetGamma()` under some branches (tr_image.c:1714 unconditional,
1717/1724 depend on `glConfig.deviceSupportsGamma`). Safest:

```c
static void vk_GLimp_SetGamma( unsigned char red[256],
                                unsigned char green[256],
                                unsigned char blue[256] ) {
    /* No HW gamma on Vulkan path yet — silently accept. Software gamma
     * (the same table) is applied directly to texture uploads by
     * R_LightScaleTexture(), independent of this hook. */
    (void)red; (void)green; (void)blue;
}
...
vk_ri.GLimp_SetGamma        = vk_GLimp_SetGamma;
```

### 3.5 `vk_CL_WriteAVIVideoFrame` / `vk_CL_SaveJPG` / `vk_CL_SaveJPGToBuffer` / `vk_Sys_SetClipboardBitmap` (MEDIUM/LOW — defensive)

Not hit on boot, but each is reachable from a screenshot or video-capture
command the user could trigger via the console. Recommend either:

- Wire engine symbols where they exist (`CL_SaveJPG` / `CL_SaveJPGToBuffer`
  may exist as `RE_SaveJPG*` in RealRTCW's renderer-internal code — needs a
  separate check), OR
- Add visible no-op stubs that `Com_Printf( S_COLOR_YELLOW "screenshot/video
  capture not implemented on Vulkan path\n" );` so any attempt produces a
  clear log line instead of a crash.

These can wait until after iter 9 verifies boot. Mark as **deferred-safe**
until first frame renders.

---

## 4. Safe-NULL slots (do not wire)

These have no callers reachable on the Vulkan build's hot or warm paths:

| Slot | Why safe |
|------|----------|
| `GLimp_Init` | Caller is in `#else` branch of `#ifdef USE_VULKAN` (tr_init.c:570) |
| `GLimp_Shutdown` | Source NULL-checks (`if ( ri.GLimp_Shutdown )`) at tr_init.c:2006 |
| `GL_GetProcAddress` | All callers are in OpenGL extension-resolution code, guarded by build/runtime gates |
| `CL_LoadJPG` | Zero callers in `code/renderervk/` |

`GLimp_EndFrame` (tr_backend.c:1769) needs verification — read the surrounding
preprocessor context before final batch. If unguarded on the Vulkan build,
add a no-op stub at the same time as 3.3/3.4.

---

## 5. Quake3e baseline reference

Upstream Quake3e wires all 56 slots in `code/client/cl_main.c::CL_InitRef()`
because it has no DLL boundary in source — the engine binary defines every
symbol directly (Hunk_*, Cvar_*, etc., and GLimp_*/VKimp_* live in
`code/sdl/sdl_glimp.c`). Their `GLimp_InitGamma` is implemented in
`code/sdl/sdl_glimp.c` and uses SDL_SetWindowGammaRamp (SDL2). They never
NULL any slot.

RealRTCW's split is real (engine binary lacks `GLimp_InitGamma` /
`GLimp_SetGamma` symbols — historically only the OpenGL renderer DLL defined
them). The translator's design assumption — "anything starting with GLimp_
is OpenGL-only and can stay NULL" — is wrong for Quake3e's renderervk
because Quake3e ported the gamma plumbing to be backend-agnostic.

---

## 6. Predicted iter-9 outcome

Wiring §3.2 + §3.3 + §3.4 in one batch should:

1. Pass `R_Init` past `tr_init.c:583` (GLimp_InitGamma) into
   `R_SetColorMappings` (tr_image.c:1714) — pass via SetGamma stub.
2. Reach `tr_world` shader-load / first BSP load — possibly hit
   `CM_ClusterPVS` NULL only on map-load (not R_Init), so deferring §3.1 is
   acceptable for a "does it boot to console" smoke test, but recommended in
   the same batch since the fix is one line.
3. Reach first frame submission. With §3.2 (`CL_IsMinimized`), vk.c:7371
   and friends will return `qfalse` and proceed normally.
4. Most likely next failure: a `RE_*` (refexport) gap (Task 4 territory),
   NOT another `refimport_t` NULL. After this batch the import table should
   be complete for boot-and-render-one-frame.

---

## 7. Confidence and coverage

- **Iter-8 pin-down:** high confidence. Disassembly offset matches LR
  arithmetic exactly; signature (single ptr arg in x0, return-then-reload
  pattern on `glConfig.deviceSupportsGamma`) matches source line.
- **NULL-omission audit:** every struct field in tr_public.h §144–243 was
  enumerated and matched to either a wiring or a NULL-comment entry. No
  silent omissions.
- **Caller enumeration:** covered every `*.c`/`*.h` in `code/renderervk/`.
  Macros that *quote* `ri.X` in comments (e.g. `tr_light.c:390` for
  `ri.ftol`) are filtered out — `ri.ftol` is not a real struct member.
- **Severity calls:** based on direct read of tr_init.c boot flow plus
  vk.c presentation/swap-chain code. Per-frame classifications verified by
  reading the surrounding function (e.g. vk.c:7371 is in
  `vk_begin_frame_pre_acquire`, called every frame).

