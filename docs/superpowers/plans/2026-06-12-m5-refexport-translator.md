# M5 refexport_t Translator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a renderer→engine vtable translator (`CL_BuildVulkanRefExport`) so the engine's SMALL `refexport_t` view sees the right function in every slot after the Vulkan renderer DLL returns its BIG `refexport_t`. Closes the `re.RegisterShader("white")` → `RE_LoadWorldMap` slot-misalignment crash that gates Vulkan boot at end-of-M4.

**Architecture:** Mirror of γ'-era `CL_BuildVulkanRefImport`, opposite direction. Split across two translation units because SMALL and BIG share `__TR_TYPES_H` guard and cannot coexist in one TU:

```
┌─ cl_refvulkan.c (sees BIG via renderercommon/tr_public.h) ──────┐
│   existing: CL_BuildVulkanRefImport (engine→renderer)            │
│   NEW: caches BIG refexport_t pointer + exposes opaque thunks    │
│        that invoke BIG slots, with all engine-facing args        │
│        passed as void*/scalars (no SMALL types here)             │
└──────────────────────────────────────────────────────────────────┘
                              ↓ link-time symbols
┌─ cl_refvulkan_export.c (sees SMALL via renderer/tr_public.h) NEW ┐
│   CL_BuildVulkanRefExport(void *big): fills static SMALL         │
│     refexport_t with wrappers; wrappers take SMALL-typed args,   │
│     call into thunks in cl_refvulkan.c                           │
└──────────────────────────────────────────────────────────────────┘
                              ↓ used by
   cl_main.c:CL_InitRef — between GetRefAPI return and `re = *ret`
```

**Scope boundary (important):** This plan handles **function-pointer slot translation only**. Struct-shape divergence (`refEntity_t`, `refdef_t`, `glconfig_t` may differ between SMALL and BIG `tr_types.h`) is deferred to **M6 — Scene-Build Struct Translation**. M5 success = menu/UI usable (those calls pass only scalars and strings). Level-load may still crash inside `re.RenderScene` / `re.AddRefEntityToScene` from struct layout mismatch — that's M6 territory, and we add deliberate logging in M5 to surface where M6 needs to land.

**Tech Stack:** C99, RealRTCW engine binary + vendored Quake3e renderervk DLL, gmake on macOS arm64, MoltenVK.

---

## §0 What the dep-map revealed (2026-06-12)

| Group | Definition | Count | Strategy |
|-------|------------|-------|----------|
| **A. Identity** | name + signature match in SMALL and BIG | 21 | direct slot assignment, no wrapper |
| **B. Sig mismatch** | name matches, signature differs | 7 | wrapper translates args |
| **C. RTCW-only** | in SMALL, NOT in BIG | 8 | NULL stub with log-on-first-call probe |
| **D. Q3e-only** | in BIG, NOT in SMALL | 10 | dropped — engine never references them |

**Group B detail:**

| Slot | SMALL signature | BIG signature | Wrapper strategy |
|------|----------------|---------------|------------------|
| `Shutdown` | `(qboolean destroyWindow)` | `(refShutdownCode_t code)` | `destroyWindow ? REF_DESTROY_WINDOW : REF_KEEP_CONTEXT` |
| `AddRefEntityToScene` | `(refEntity_t *)` | `(refEntity_t *, qboolean intShaderTime)` | pass `qfalse` for `intShaderTime` |
| `AddPolyToScene` | `(hShader, numVerts, verts)` | `(hShader, numVerts, verts, num)` | pass `1` for `num` |
| `AddLightToScene` | `(org, intensity, r, g, b, overdraw)` | `(org, intensity, r, g, b)` | drop `overdraw` arg |
| `LerpTag` | `(tag, refent, tagName, startIndex)` | `(tag, model, startFrame, endFrame, frac, tagName)` | reshape: read `model/oldframe/frame/backlerp` from `refEntity_t` |
| `DrawStretchRaw` | `(const byte *data, ...)` | `(byte *data, ...)` | cast away const |
| `UploadCinematic` | `(int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty)` | `(...byte *data...)` | cast away const (same shape as DrawStretchRaw) |

**Group C — RTCW-only slots (need NULL stub + log probe):**
`RegisterSmartSkin`, `GetSkinModel`, `GetShaderFromModel`, `AddPolysToScene`, `AddCoronaToScene`, `SetFog`, `DrawStretchPicGradient`, `ZombieFXAddNewHit`.

For these no renderer counterpart exists in vendored renderervk. Boot-priority strategy: NULL pointer + a one-shot Com_Printf inside a tripwire wrapper, so the smoke log shows which RTCW-specifics actually fire before we invest in routing them.

**Group D — Q3e-only slots (informational only, no action):**
`AddAdditiveLightToScene`, `AddLinearLightToScene`, `inPVS`, `ThrottleBackend`, `FinishBloom`, `SetColorMappings`, `CanMinimize`, `GetConfig`, `VertexLighting`, `SyncRender` — these exist in BIG refexport but engine never references them, so the SMALL refexport simply omits them.

**Known landmine (NOT M5 scope):** `tr_types.h` diverges between `code/renderer/` and `code/renderercommon/`. Struct-typed args passed through the function-pointer wrappers (`refEntity_t *`, `refdef_t *`, `glconfig_t *`) may differ in field layout. The fact that `vk_BuildRefImport` works through R_Init proves the most-used types overlap on early fields — but anything past UI/menu (scene rendering with real entities) likely needs struct conversion. **Track this as M6.**

---

## File Structure

**Modify:**
- `code/client/cl_refvulkan.h` — add BIG-pointer cache + opaque thunk decls + `CL_BuildVulkanRefExport` symbol
- `code/client/cl_refvulkan.c` — add BIG-pointer setter + opaque thunks for Group A+B BIG slot invocation
- `code/client/cl_main.c:3517` — insert translator call between `GetRefAPI` return and `re = *ret`
- `Makefile` — add `cl_refvulkan_export.o` to engine link list (one line near existing `cl_refvulkan.o`)

**Create:**
- `code/client/cl_refvulkan_export.c` — SMALL-side translator: includes engine `renderer/tr_public.h`, exports `CL_BuildVulkanRefExport(void *big_export)`, defines SMALL-shaped wrappers calling into cl_refvulkan.c thunks
- `docs/vulkan-phase2/2026-06-12-m5-baseline-stub-refexport.log` — smoke log after Task 2 (skeleton wired, all-NULL refexport behavior)
- `docs/vulkan-phase2/2026-06-12-m5-postwire-smoke.log` — smoke log after Task 6 (full wrapper set in place)

---

## Tasks

### Task 0: Pre-flight verification of slot enumeration

**Files:**
- Read: `code/renderer/tr_public.h`
- Read: `code/renderercommon/tr_public.h`
- Read: `code/renderervk/tr_init.c` (GetRefAPI body, lines 2050-2200)
- Read: `code/renderer/tr_init.c` (GetRefAPI body, lines 1604-1700)

- [ ] **Step 1: Sanity-check Group A identity slots by name**

Run: `cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt && grep -oE 're\.\w+' docs/superpowers/plans/2026-06-12-m5-refexport-translator.md | sort -u`

Expected: list of slots referenced in this plan. Cross-check each Group A name appears in both headers with identical signature. If a slot we labeled Group A actually has a sig mismatch, move it to Group B before continuing.

- [ ] **Step 2: Confirm renderervk's GetRefAPI fills every BIG slot we depend on**

Read `code/renderervk/tr_init.c` from `refexport_t *GetRefAPI` (line 2050) through the `return &re;` at end of function. For every Group A+B slot in this plan, verify the corresponding `re.SLOTNAME = RE_XXX;` line exists. If a slot is unfilled, that's a separate triage — record it as a follow-up task and continue.

- [ ] **Step 3: Record pre-flight result**

If steps 1-2 pass clean, no commit; proceed to Task 1.
If a slot reclassification is needed, edit the §0 table in this plan and commit:

```bash
git add docs/superpowers/plans/2026-06-12-m5-refexport-translator.md
git commit -m "plan(m5): reclassify <slotname> after pre-flight"
```

---

### Task 1: Header + skeleton — minimal compile, no behavior change

**Files:**
- Modify: `code/client/cl_refvulkan.h`
- Modify: `code/client/cl_refvulkan.c` (~line 80, just below the existing static `vk_ri` declaration)
- Create: `code/client/cl_refvulkan_export.c`

**Goal of this task:** add the wire scaffolding so the engine, when built with `BUILD_RENDERER_VULKAN=1`, runs the translator function — but the translator returns a SMALL refexport_t with EVERY slot NULL. Expected smoke behavior: crash IMMEDIATELY on the first re.XXX call after CL_InitRef returns. The new crash site is the proof that the wire-in works.

- [ ] **Step 1: Extend `cl_refvulkan.h`**

Open `code/client/cl_refvulkan.h` and add at the end before the closing `#endif`:

```c
/* M5: refexport_t translator (renderer → engine direction).
 *
 * Takes the BIG refexport_t pointer returned by the vendored renderervk
 * GetRefAPI and returns a pointer to a SMALL refexport_t whose slots are
 * wired (directly or via wrappers) to the BIG slots.
 *
 * Opaque return type because cl_main.c (the only caller) sees SMALL but
 * this header is included from cl_refvulkan.c too which sees BIG. The
 * caller casts back to SMALL refexport_t * at the use site.
 *
 * Idempotent: subsequent calls return the same pointer; second BIG arg
 * is ignored. */
void *CL_BuildVulkanRefExport( void *big_export );

/* BIG-side thunk storage. Setter called from cl_refvulkan_export.c
 * during CL_BuildVulkanRefExport; thunks below read this pointer. */
void  CL_VulkanRefExport_StoreBig( void *big_export );
```

- [ ] **Step 2: Add BIG-side storage + setter in `cl_refvulkan.c`**

Find the existing `static refimport_t vk_ri;` declaration (around line 80) and add immediately after it:

```c
/* M5: BIG refexport_t cache. Set by cl_refvulkan_export.c during the
 * SMALL-side translator build. Thunks below invoke renderer entry
 * points through this pointer. */
static refexport_t *vk_re_big = NULL;

void CL_VulkanRefExport_StoreBig( void *big_export ) {
    vk_re_big = (refexport_t *)big_export;
}
```

- [ ] **Step 3: Create `cl_refvulkan_export.c` skeleton**

Create file with:

```c
/*
===========================================================================
Copyright (C) 2026 RealRTCW contributors

This file is part of RealRTCW source code.  See cl_refvulkan.c for the
sibling TU that handles the engine→renderer direction. This TU handles
the renderer→engine direction.

IMPORTANT: this TU MUST NOT include anything from renderercommon/. It
sees the SMALL refexport_t / refimport_t / tr_types.h via the engine's
own renderer/tr_public.h. cl_refvulkan.c sees the BIG view; the two
TUs communicate via void* and link-time symbols only.
===========================================================================
*/

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../renderer/tr_public.h"
#include "cl_refvulkan.h"

/* Engine-side SMALL refexport_t — what cl_main.c's `re` global expects. */
static refexport_t  vk_re_small;
static qboolean     vk_re_built = qfalse;

void *CL_BuildVulkanRefExport( void *big_export ) {
    if ( vk_re_built ) {
        return &vk_re_small;
    }

    Com_Memset( &vk_re_small, 0, sizeof( vk_re_small ) );

    /* Hand the BIG pointer to the sibling TU which holds the thunks. */
    CL_VulkanRefExport_StoreBig( big_export );

    /* M5 Task 1: skeleton only — every slot still NULL. First re.X call
     * from CL_InitRef caller will SIGSEGV; that's the wire-in proof.
     * Slot population follows in Tasks 2-7. */

    vk_re_built = qtrue;
    return &vk_re_small;
}
```

- [ ] **Step 4: Wire into the Makefile**

Open `Makefile`. Search for `cl_refvulkan` (one match):

```
$(B)/client/cl_refvulkan.o \
```

Add the export TU right after:

```
$(B)/client/cl_refvulkan.o \
$(B)/client/cl_refvulkan_export.o \
```

- [ ] **Step 5: Wire the translator into `cl_main.c:CL_InitRef`**

Open `code/client/cl_main.c`. Locate `CL_InitRef` (line 3388). Find the existing block:

```c
#ifdef BUILD_RENDERER_VULKAN
    {
        ...
        void *vk_ri_ptr = CL_BuildVulkanRefImport();
        ret = GetRefAPI( REF_API_VERSION, (refimport_t *)vk_ri_ptr );
    }
#else
    ret = GetRefAPI( REF_API_VERSION, &ri );
#endif

    if ( !ret ) {
        Com_Error( ERR_FATAL, "Couldn't initialize refresh" );
    }

    re = *ret;
```

Insert the SMALL-side translator BETWEEN the `if (!ret)` check and `re = *ret`:

```c
    if ( !ret ) {
        Com_Error( ERR_FATAL, "Couldn't initialize refresh" );
    }

#ifdef BUILD_RENDERER_VULKAN
    /* M5: renderer fills BIG refexport_t; engine `re` is SMALL.
     * Translate slot offsets + signature drift before copy.
     * Returns a SMALL refexport_t * for `re = *ret;` to consume. */
    ret = (refexport_t *)CL_BuildVulkanRefExport( ret );
    if ( !ret ) {
        Com_Error( ERR_FATAL, "CL_BuildVulkanRefExport returned NULL" );
    }
#endif

    re = *ret;
```

- [ ] **Step 6: Build and confirm clean link**

Run: `cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt && rm -rf build/release-darwin-arm64/client && make USE_INTERNAL_LIBS=0 BUILD_RENDERER_VULKAN=1 -j8 2>&1 | tail -40`

Expected: build completes with `cl_refvulkan_export.o` listed in the link line, no errors. Apply [[feedback_clean_rebuild_after_header_edit]] — the `rm -rf` is mandatory because we touched a shared header.

- [ ] **Step 7: Smoke run, capture log**

Run: `cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt && ./build/release-darwin-arm64/wolfsp.app/Contents/MacOS/wolfsp +set cl_renderer vulkan +quit 2>&1 | tee docs/vulkan-phase2/2026-06-12-m5-baseline-stub-refexport.log; echo "exit=$?"`

Expected: engine reaches `---- Renderer Initialization Complete ----` (line printed by CL_InitRef itself) then SIGSEGV on the first re.X call — likely re.BeginRegistration or re.Shutdown. The crash site MUST be different from the M4-end crash (which was inside RE_LoadWorldMap). If it's the same site, the wire-in didn't take and the translator wasn't actually invoked — debug before continuing.

- [ ] **Step 8: Commit**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt
git add code/client/cl_refvulkan.h code/client/cl_refvulkan.c code/client/cl_refvulkan_export.c code/client/cl_main.c Makefile docs/vulkan-phase2/2026-06-12-m5-baseline-stub-refexport.log
git commit -m "feat(m5): skeleton refexport translator wire-in (all slots NULL)"
```

---

### Task 2: Group A — wire 22 identity slots

**Files:**
- Modify: `code/client/cl_refvulkan_export.c`

**Goal:** populate SMALL refexport_t slots whose BIG counterpart has the same name + same signature. No wrapper needed — function pointer is identical shape on both sides, so the assignment reduces to reading a slot out of the BIG struct.

But there's an obstacle: `cl_refvulkan_export.c` sees SMALL, so it can't index `big->RegisterShader` directly (it doesn't know BIG's layout). We add per-slot **slot accessors** in `cl_refvulkan.c` (which sees BIG), expose them via `cl_refvulkan.h`, then `cl_refvulkan_export.c` calls them to retrieve the right function pointer.

- [ ] **Step 1: Add the slot-accessor block to `cl_refvulkan.h`**

Add to `cl_refvulkan.h` just below `CL_VulkanRefExport_StoreBig`:

```c
/* M5 Group A — direct slot accessors. Each returns the BIG slot's
 * function pointer as an opaque void *. SMALL-side translator casts
 * them to the SMALL slot's function-pointer type before assignment.
 * No signature translation happens here — only Group B wrappers do that. */
void *vk_re_get_BeginRegistration( void );
void *vk_re_get_RegisterModel( void );
void *vk_re_get_RegisterSkin( void );
void *vk_re_get_RegisterShader( void );
void *vk_re_get_RegisterShaderNoMip( void );
void *vk_re_get_LoadWorld( void );
void *vk_re_get_SetWorldVisData( void );
void *vk_re_get_EndRegistration( void );
void *vk_re_get_ClearScene( void );
void *vk_re_get_LightForPoint( void );
void *vk_re_get_RenderScene( void );
void *vk_re_get_SetColor( void );
void *vk_re_get_DrawStretchPic( void );
void *vk_re_get_BeginFrame( void );
void *vk_re_get_EndFrame( void );
void *vk_re_get_MarkFragments( void );
void *vk_re_get_ModelBounds( void );
void *vk_re_get_RegisterFont( void );
void *vk_re_get_RemapShader( void );
void *vk_re_get_GetEntityToken( void );
void *vk_re_get_TakeVideoFrame( void );
```

- [ ] **Step 2: Implement the 22 accessors in `cl_refvulkan.c`**

Append to `cl_refvulkan.c` (anywhere after the `vk_re_big` storage block):

```c
/* ====================================================================
 * M5 Group A — slot accessors for identity-signature slots.
 * Each returns (void *) so cl_refvulkan_export.c can assign without
 * pulling in BIG type declarations.
 * ==================================================================== */

#define VK_RE_GET(name)  void *vk_re_get_##name( void ) { return (void *)vk_re_big->name; }

VK_RE_GET(BeginRegistration)
VK_RE_GET(RegisterModel)
VK_RE_GET(RegisterSkin)
VK_RE_GET(RegisterShader)
VK_RE_GET(RegisterShaderNoMip)
VK_RE_GET(LoadWorld)
VK_RE_GET(SetWorldVisData)
VK_RE_GET(EndRegistration)
VK_RE_GET(ClearScene)
VK_RE_GET(LightForPoint)
VK_RE_GET(RenderScene)
VK_RE_GET(SetColor)
VK_RE_GET(DrawStretchPic)
VK_RE_GET(BeginFrame)
VK_RE_GET(EndFrame)
VK_RE_GET(MarkFragments)
VK_RE_GET(ModelBounds)
VK_RE_GET(RegisterFont)
VK_RE_GET(RemapShader)
VK_RE_GET(GetEntityToken)
VK_RE_GET(TakeVideoFrame)

#undef VK_RE_GET
```

- [ ] **Step 3: Assign in `cl_refvulkan_export.c::CL_BuildVulkanRefExport`**

Replace the `/* M5 Task 1: skeleton only ... */` placeholder with:

```c
    /* Group A — identity slots: name + signature match in SMALL and BIG.
     * Cast through void * to silence "incompatible pointer type" warnings
     * (legitimate — SMALL and BIG see different tr_types.h, so the
     * function pointer types differ in record-typed args even when the
     * slot semantics are identical). */
    vk_re_small.BeginRegistration   = (void (*)( glconfig_t * ))vk_re_get_BeginRegistration();
    vk_re_small.RegisterModel       = (qhandle_t (*)( const char * ))vk_re_get_RegisterModel();
    vk_re_small.RegisterSkin        = (qhandle_t (*)( const char * ))vk_re_get_RegisterSkin();
    vk_re_small.RegisterShader      = (qhandle_t (*)( const char * ))vk_re_get_RegisterShader();
    vk_re_small.RegisterShaderNoMip = (qhandle_t (*)( const char * ))vk_re_get_RegisterShaderNoMip();
    vk_re_small.LoadWorld           = (void (*)( const char * ))vk_re_get_LoadWorld();
    vk_re_small.SetWorldVisData     = (void (*)( const byte * ))vk_re_get_SetWorldVisData();
    vk_re_small.EndRegistration     = (void (*)( void ))vk_re_get_EndRegistration();
    vk_re_small.ClearScene          = (void (*)( void ))vk_re_get_ClearScene();
    vk_re_small.LightForPoint       = (int (*)( vec3_t, vec3_t, vec3_t, vec3_t ))vk_re_get_LightForPoint();
    vk_re_small.RenderScene         = (void (*)( const refdef_t * ))vk_re_get_RenderScene();
    vk_re_small.SetColor            = (void (*)( const float * ))vk_re_get_SetColor();
    vk_re_small.DrawStretchPic      = (void (*)( float, float, float, float, float, float, float, float, qhandle_t ))vk_re_get_DrawStretchPic();
    vk_re_small.BeginFrame          = (void (*)( stereoFrame_t ))vk_re_get_BeginFrame();
    vk_re_small.EndFrame            = (void (*)( int *, int * ))vk_re_get_EndFrame();
    vk_re_small.MarkFragments       = (int (*)( int, const vec3_t *, const vec3_t, int, vec3_t, int, markFragment_t * ))vk_re_get_MarkFragments();
    vk_re_small.ModelBounds         = (void (*)( qhandle_t, vec3_t, vec3_t ))vk_re_get_ModelBounds();
    vk_re_small.RegisterFont        = (void (*)( const char *, int, fontInfo_t * ))vk_re_get_RegisterFont();
    vk_re_small.RemapShader         = (void (*)( const char *, const char *, const char * ))vk_re_get_RemapShader();
    vk_re_small.GetEntityToken      = (qboolean (*)( char *, int ))vk_re_get_GetEntityToken();
    vk_re_small.TakeVideoFrame      = (void (*)( int, int, byte *, byte *, qboolean ))vk_re_get_TakeVideoFrame();
```

- [ ] **Step 4: Build clean**

Run: `cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt && rm -rf build/release-darwin-arm64/client && make USE_INTERNAL_LIBS=0 BUILD_RENDERER_VULKAN=1 -j8 2>&1 | tail -20`

Expected: clean build, possibly compiler warnings about function-pointer cast — verify each warning is one of the slots we're intentionally casting via `void *`. If warning appears for a slot we DIDN'T cast, the SMALL signature changed — investigate.

- [ ] **Step 5: Commit (smoke happens after Task 6 batch — no interim smokes for tasks that only fill more slots; the boot will keep failing on whatever Group B slot fires first until Tasks 3-6 land)**

```bash
git add code/client/cl_refvulkan.h code/client/cl_refvulkan.c code/client/cl_refvulkan_export.c
git commit -m "feat(m5): wire 22 Group A identity slots in refexport translator"
```

---

### Task 3: Group B — Shutdown signature wrapper

**Files:**
- Modify: `code/client/cl_refvulkan.h` (decl)
- Modify: `code/client/cl_refvulkan.c` (thunk)
- Modify: `code/client/cl_refvulkan_export.c` (wrapper + slot assign)

**Goal:** SMALL `Shutdown` takes `qboolean destroyWindow`; BIG takes `refShutdownCode_t code` (enum: `REF_KEEP_CONTEXT`, `REF_KEEP_WINDOW`, `REF_DESTROY_WINDOW`, `REF_UNLOAD_DLL`). Engine calls the slot when reconfiguring or shutting down. The wrapper maps `qtrue` → `REF_DESTROY_WINDOW`, `qfalse` → `REF_KEEP_CONTEXT` (matches OpenGL renderer's behavior — `qfalse` means "don't flash desktop", which is the keep-context case).

- [ ] **Step 1: Add thunk decl + call thunk in cl_refvulkan.c**

Add to `cl_refvulkan.h` (Group B block, new section):

```c
/* M5 Group B — signature-translating thunks. SMALL-side wrapper in
 * cl_refvulkan_export.c calls these with int / void * args; thunk
 * casts to BIG enums/types and invokes the slot. */
void vk_re_thunk_Shutdown( int code );
```

Add to `cl_refvulkan.c`:

```c
void vk_re_thunk_Shutdown( int code ) {
    vk_re_big->Shutdown( (refShutdownCode_t)code );
}
```

- [ ] **Step 2: Add SMALL-side wrapper + slot assign in cl_refvulkan_export.c**

Add to `cl_refvulkan_export.c` (above `CL_BuildVulkanRefExport`):

```c
/* Engine SMALL: Shutdown(qboolean destroyWindow).
 *   destroyWindow == qtrue  → renderer should drop the window.
 *   destroyWindow == qfalse → reconfig only; keep window+context to
 *                             avoid the desktop-flash UX glitch.
 * Maps to Q3e BIG: Shutdown(refShutdownCode_t code).
 *   REF_DESTROY_WINDOW  ≈ qtrue
 *   REF_KEEP_CONTEXT    ≈ qfalse (also keeps window, so safe choice) */
static void vk_re_wrap_Shutdown( qboolean destroyWindow ) {
    /* Magic constants match the BIG enum:
     *   REF_KEEP_CONTEXT  = 0
     *   REF_KEEP_WINDOW   = 1
     *   REF_DESTROY_WINDOW= 2
     *   REF_UNLOAD_DLL    = 3
     * Hardcoded to avoid pulling in the BIG enum definition. */
    vk_re_thunk_Shutdown( destroyWindow ? 2 /* REF_DESTROY_WINDOW */
                                        : 0 /* REF_KEEP_CONTEXT */ );
}
```

Inside `CL_BuildVulkanRefExport`, add:

```c
    /* Group B — Shutdown: qboolean → refShutdownCode_t */
    vk_re_small.Shutdown = vk_re_wrap_Shutdown;
```

- [ ] **Step 3: Build clean**

Run: `cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt && rm -rf build/release-darwin-arm64/client && make USE_INTERNAL_LIBS=0 BUILD_RENDERER_VULKAN=1 -j8 2>&1 | tail -20`

Expected: clean build, no new warnings.

- [ ] **Step 4: Commit**

```bash
git add code/client/cl_refvulkan.h code/client/cl_refvulkan.c code/client/cl_refvulkan_export.c
git commit -m "feat(m5): Shutdown signature wrapper (qboolean→refShutdownCode_t)"
```

---

### Task 4: Group B — Scene-build wrappers (AddRefEntityToScene, AddPolyToScene, AddLightToScene)

**Files:**
- Modify: `code/client/cl_refvulkan.h` (3 thunk decls)
- Modify: `code/client/cl_refvulkan.c` (3 thunks)
- Modify: `code/client/cl_refvulkan_export.c` (3 wrappers + 3 slot assigns)

**Goal:** add wrappers for the three scene-build slots whose signatures differ in arg count. These get called once gameplay starts. **All three pass struct pointers (`refEntity_t *`, `polyVert_t *`, `vec3_t`) whose underlying layout may differ between SMALL and BIG (`tr_types.h` drift).** M5 passes the pointer through as-is — if the renderer crashes on field-layout mismatch, that's M6 surface area; we log the call so the M6 plan has a starting list.

- [ ] **Step 1: Add thunk decls in `cl_refvulkan.h`**

```c
void vk_re_thunk_AddRefEntityToScene( const void *re_ptr, int intShaderTime );
void vk_re_thunk_AddPolyToScene( qhandle_t hShader, int numVerts, const void *verts, int num );
void vk_re_thunk_AddLightToScene( const float *org, float intensity, float r, float g, float b );
```

- [ ] **Step 2: Add thunks in `cl_refvulkan.c`**

```c
void vk_re_thunk_AddRefEntityToScene( const void *re_ptr, int intShaderTime ) {
    vk_re_big->AddRefEntityToScene( (const refEntity_t *)re_ptr,
                                    intShaderTime ? qtrue : qfalse );
}

void vk_re_thunk_AddPolyToScene( qhandle_t hShader, int numVerts, const void *verts, int num ) {
    vk_re_big->AddPolyToScene( hShader, numVerts, (const polyVert_t *)verts, num );
}

void vk_re_thunk_AddLightToScene( const float *org, float intensity, float r, float g, float b ) {
    /* SMALL passes `int overdraw`; BIG has no such arg. Drop it. The
     * overdraw flag was an RTCW-specific dynamic-light visibility hint;
     * the Q3e renderer just renders the light. */
    vk_re_big->AddLightToScene( (vec_t *)org, intensity, r, g, b );
}
```

- [ ] **Step 3: Add SMALL-side wrappers in `cl_refvulkan_export.c`**

```c
/* SMALL: AddRefEntityToScene(refEntity_t*). BIG adds qboolean intShaderTime.
 * Default to qfalse (Q3e's "use ent->shaderTime as-is"). */
static void vk_re_wrap_AddRefEntityToScene( const refEntity_t *re ) {
    vk_re_thunk_AddRefEntityToScene( (const void *)re, 0 /* intShaderTime = qfalse */ );
}

/* SMALL: AddPolyToScene(hShader, numVerts, verts). BIG adds int num.
 * Engine never split-loops over polys here — pass num=1. */
static void vk_re_wrap_AddPolyToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts ) {
    vk_re_thunk_AddPolyToScene( hShader, numVerts, (const void *)verts, 1 );
}

/* SMALL: AddLightToScene(org, intensity, r, g, b, overdraw). BIG drops overdraw. */
static void vk_re_wrap_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b, int overdraw ) {
    (void)overdraw;  /* RTCW-specific dynamic-light visibility hint; not modeled by Q3e renderer. */
    vk_re_thunk_AddLightToScene( org, intensity, r, g, b );
}
```

In `CL_BuildVulkanRefExport`:

```c
    /* Group B — scene build wrappers */
    vk_re_small.AddRefEntityToScene = vk_re_wrap_AddRefEntityToScene;
    vk_re_small.AddPolyToScene      = vk_re_wrap_AddPolyToScene;
    vk_re_small.AddLightToScene     = vk_re_wrap_AddLightToScene;
```

- [ ] **Step 4: Build clean**

Run: `cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt && rm -rf build/release-darwin-arm64/client && make USE_INTERNAL_LIBS=0 BUILD_RENDERER_VULKAN=1 -j8 2>&1 | tail -20`

Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add code/client/cl_refvulkan.h code/client/cl_refvulkan.c code/client/cl_refvulkan_export.c
git commit -m "feat(m5): scene-build wrappers (AddRefEntityToScene, AddPolyToScene, AddLightToScene)"
```

---

### Task 5: Group B — LerpTag wrapper

**Files:**
- Modify: `code/client/cl_refvulkan.h` (thunk decl)
- Modify: `code/client/cl_refvulkan.c` (thunk)
- Modify: `code/client/cl_refvulkan_export.c` (wrapper + slot assign)

**Goal:** LerpTag has the most signature divergence:

```
SMALL: int LerpTag( orientation_t *tag, const refEntity_t *refent,
                    const char *tagName, int startIndex );
BIG:   int LerpTag( orientation_t *tag, qhandle_t model,
                    int startFrame, int endFrame, float frac,
                    const char *tagName );
```

The SMALL→BIG translation needs to extract `hModel`, `oldframe`, `frame`, `backlerp` from `refEntity_t`. **This is the first place we directly read fields from a possibly-drifted struct.** Read-only access, simple fields (qhandle + 2 ints + 1 float) are stable across all known RTCW/Q3e revisions — likely safe, but worth flagging for adversarial review later.

The SMALL `startIndex` arg is the iortcw extension for multi-tag-with-same-name iteration; Q3e LerpTag's BIG signature doesn't expose this. **Drop `startIndex` and accept that multi-tag with duplicate names may pick wrong tag.** RTCW SP game data does not rely on duplicate tag names (verified in cgame asset audit during M3.5); MP might — out of scope.

- [ ] **Step 1: Thunk decl**

In `cl_refvulkan.h`:

```c
int vk_re_thunk_LerpTag( void *tag, int hModel, int startFrame, int endFrame,
                         float frac, const char *tagName );
```

- [ ] **Step 2: Thunk impl in `cl_refvulkan.c`**

```c
int vk_re_thunk_LerpTag( void *tag, int hModel, int startFrame, int endFrame,
                         float frac, const char *tagName ) {
    return vk_re_big->LerpTag( (orientation_t *)tag,
                               (qhandle_t)hModel,
                               startFrame, endFrame, frac, tagName );
}
```

- [ ] **Step 3: SMALL wrapper in `cl_refvulkan_export.c`**

Above `CL_BuildVulkanRefExport`:

```c
/* SMALL LerpTag: (tag, refent, tagName, startIndex).
 * BIG LerpTag:   (tag, hModel, startFrame, endFrame, frac, tagName).
 *
 * Translate by reading model/frame/oldframe/backlerp out of refent.
 *   BIG endFrame   = refent->frame      (current frame)
 *   BIG startFrame = refent->oldframe   (previous frame)
 *   BIG frac       = 1.0 - refent->backlerp
 *     (engine semantic: backlerp=1 means "fully on oldframe" → frac=0;
 *                       backlerp=0 means "fully on frame"    → frac=1.)
 *
 * startIndex dropped — RTCW-only multi-tag iteration extension. RTCW SP
 * cgame doesn't rely on it (verified via cgame asset audit, M3.5 era).
 * If a duplicate-name tag bug appears in gameplay, this is the place. */
static int vk_re_wrap_LerpTag( orientation_t *tag, const refEntity_t *refent,
                               const char *tagName, int startIndex ) {
    (void)startIndex;
    return vk_re_thunk_LerpTag( (void *)tag,
                                (int)refent->hModel,
                                refent->oldframe,
                                refent->frame,
                                1.0f - refent->backlerp,
                                tagName );
}
```

In `CL_BuildVulkanRefExport`:

```c
    vk_re_small.LerpTag = vk_re_wrap_LerpTag;
```

- [ ] **Step 4: Build clean**

Same command as previous tasks. Build expected to pass.

- [ ] **Step 5: Commit**

```bash
git add code/client/cl_refvulkan.h code/client/cl_refvulkan.c code/client/cl_refvulkan_export.c
git commit -m "feat(m5): LerpTag wrapper — drops startIndex, reads frames from refEntity_t"
```

---

### Task 6: Group B — DrawStretchRaw + UploadCinematic const-cast wrappers

**Files:**
- Modify: `code/client/cl_refvulkan.h`
- Modify: `code/client/cl_refvulkan.c`
- Modify: `code/client/cl_refvulkan_export.c`

**Goal:** Two slots with identical pattern — SMALL takes `const byte *data`, BIG takes `byte *data`. Renderer never writes through the pointer (verified for both: `code/renderervk/tr_cmds.c` reads from `data` to copy into textures, never writes back). Wrap each so the const-cast is local and grep-locatable rather than a silent function-pointer cast.

- [ ] **Step 1: Thunk decls**

In `cl_refvulkan.h`:

```c
void vk_re_thunk_DrawStretchRaw( int x, int y, int w, int h, int cols, int rows,
                                 const byte *data, int client, int dirty );
void vk_re_thunk_UploadCinematic( int w, int h, int cols, int rows,
                                  const byte *data, int client, int dirty );
```

- [ ] **Step 2: Thunk impls**

```c
void vk_re_thunk_DrawStretchRaw( int x, int y, int w, int h, int cols, int rows,
                                 const byte *data, int client, int dirty ) {
    /* BIG signature drops const on data ptr but renderer never writes
     * through it. Cast away const here, alone, so the unsafe cast is
     * grep-locatable. */
    vk_re_big->DrawStretchRaw( x, y, w, h, cols, rows,
                               (byte *)data, client, dirty ? qtrue : qfalse );
}

void vk_re_thunk_UploadCinematic( int w, int h, int cols, int rows,
                                  const byte *data, int client, int dirty ) {
    /* Same const-strip pattern as DrawStretchRaw — UploadCinematic copies
     * data into a renderer-owned texture, never writes through the input. */
    vk_re_big->UploadCinematic( w, h, cols, rows,
                                (byte *)data, client, dirty ? qtrue : qfalse );
}
```

- [ ] **Step 3: SMALL wrappers + slot assigns**

```c
static void vk_re_wrap_DrawStretchRaw( int x, int y, int w, int h, int cols, int rows,
                                       const byte *data, int client, qboolean dirty ) {
    vk_re_thunk_DrawStretchRaw( x, y, w, h, cols, rows, data, client, dirty );
}

static void vk_re_wrap_UploadCinematic( int w, int h, int cols, int rows,
                                        const byte *data, int client, qboolean dirty ) {
    vk_re_thunk_UploadCinematic( w, h, cols, rows, data, client, dirty );
}
```

Slot assigns:

```c
    vk_re_small.DrawStretchRaw  = vk_re_wrap_DrawStretchRaw;
    vk_re_small.UploadCinematic = vk_re_wrap_UploadCinematic;
```

- [ ] **Step 4: Build + smoke (this is the first batch-smoke after wiring is meaningfully populated)**

Build:
```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt && rm -rf build/release-darwin-arm64/client && make USE_INTERNAL_LIBS=0 BUILD_RENDERER_VULKAN=1 -j8 2>&1 | tail -20
```

Smoke:
```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt && timeout 10 ./build/release-darwin-arm64/wolfsp.app/Contents/MacOS/wolfsp +set cl_renderer vulkan 2>&1 | tee docs/vulkan-phase2/2026-06-12-m5-postwire-smoke.log; echo "exit=$?"
```

Expected: engine boots past `re.RegisterShader("white")`, past `re.RegisterFont`, into menu draw loop. Crash likely now lands in a Group C call (RTCW-only RegisterSmartSkin / SetFog / etc.) OR in a struct-shape mismatch on AddRefEntityToScene (which is M6 territory).

**Inspect log:** scroll for any `Com_Error` or signal — that's the first slot to triage. If the boot reaches main menu (renderer draws stretchpics for the background image), declare Task 6 done and proceed to Task 7. If a Group B slot fires and we missed something, fix inline before continuing.

- [ ] **Step 5: Commit**

```bash
git add code/client/cl_refvulkan.h code/client/cl_refvulkan.c code/client/cl_refvulkan_export.c docs/vulkan-phase2/2026-06-12-m5-postwire-smoke.log
git commit -m "feat(m5): DrawStretchRaw const-cast wrapper + first batch smoke"
```

---

### Task 7: Group C — RTCW-only NULL stubs with log-on-first-call probe

**Files:**
- Modify: `code/client/cl_refvulkan_export.c`

**Goal:** populate the 8 RTCW-only slots with **tripwire stubs** that print `S_COLOR_YELLOW "[M5/C] re.SLOT called from <unknown>\n"` exactly once per slot. This way we LEARN from the smoke log which RTCW-specifics actually fire during the boot/menu/level-load path. Slots that never fire don't need work; ones that fire frequently graduate to real translation in a follow-up iteration.

- [ ] **Step 1: Define the tripwire stubs in `cl_refvulkan_export.c`**

Above `CL_BuildVulkanRefExport`:

```c
/* ====================================================================
 * Group C — RTCW-only refexport slots (no BIG counterpart).
 *
 * Each is a tripwire stub: silent on subsequent calls, prints once on
 * first call. M5 use: drive a smoke run, scrape the log for which of
 * these actually fire during boot+menu+first-level-load, prioritize
 * follow-up routing accordingly.
 *
 * Signatures and behaviors are SMALL (engine view). When a renderer
 * counterpart is wired in a follow-up commit, replace the stub body
 * with the routing logic and remove the tripwire.
 * ==================================================================== */

#define VK_RE_TRIPWIRE(name) \
    static qboolean vk_re_C_fired_##name = qfalse; \
    static void vk_re_C_log_##name( void ) { \
        if ( !vk_re_C_fired_##name ) { \
            Com_Printf( S_COLOR_YELLOW "[M5/C] re." #name " called — RTCW-only slot, no-op stub\n" ); \
            vk_re_C_fired_##name = qtrue; \
        } \
    }

VK_RE_TRIPWIRE(RegisterSmartSkin)
VK_RE_TRIPWIRE(GetSkinModel)
VK_RE_TRIPWIRE(GetShaderFromModel)
VK_RE_TRIPWIRE(AddPolysToScene)
VK_RE_TRIPWIRE(AddCoronaToScene)
VK_RE_TRIPWIRE(SetFog)
VK_RE_TRIPWIRE(DrawStretchPicGradient)
VK_RE_TRIPWIRE(ZombieFXAddNewHit)

#undef VK_RE_TRIPWIRE

static qhandle_t vk_re_C_RegisterSmartSkin( const char *n, const char *m, qboolean u ) {
    (void)n; (void)m; (void)u;
    vk_re_C_log_RegisterSmartSkin();
    return 0;
}

static qboolean vk_re_C_GetSkinModel( qhandle_t skinid, const char *type, char *name ) {
    (void)skinid; (void)type; (void)name;
    vk_re_C_log_GetSkinModel();
    return qfalse;
}

static qhandle_t vk_re_C_GetShaderFromModel( qhandle_t modelid, int surfnum, int withlightmap ) {
    (void)modelid; (void)surfnum; (void)withlightmap;
    vk_re_C_log_GetShaderFromModel();
    return 0;
}

static void vk_re_C_AddPolysToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts, int numPolys ) {
    (void)hShader; (void)numVerts; (void)verts; (void)numPolys;
    vk_re_C_log_AddPolysToScene();
    /* TODO M5-followup: loop over numPolys, call vk_re_small.AddPolyToScene
     * for each. Trivial routing — deferred until smoke confirms it fires. */
}

static void vk_re_C_AddCoronaToScene( const vec3_t org, float r, float g, float b, float scale, int id, int flags ) {
    (void)org; (void)r; (void)g; (void)b; (void)scale; (void)id; (void)flags;
    vk_re_C_log_AddCoronaToScene();
    /* TODO M5-followup: approximate as small dynamic light via vk_re_small.AddLightToScene. */
}

static void vk_re_C_SetFog( int fogvar, int var1, int var2, float r, float g, float b, float density ) {
    (void)fogvar; (void)var1; (void)var2; (void)r; (void)g; (void)b; (void)density;
    vk_re_C_log_SetFog();
    /* TODO M5-followup: routing to renderervk fog system depends on
     * whether Q3e fog API is rich enough — investigate after smoke. */
}

static void vk_re_C_DrawStretchPicGradient( float x, float y, float w, float h,
                                            float s1, float t1, float s2, float t2,
                                            qhandle_t hShader, const float *gradientColor,
                                            int gradientType ) {
    vk_re_C_log_DrawStretchPicGradient();
    /* Fallback: gradientless DrawStretchPic — loses the gradient effect
     * but the pic still draws (HUD won't look wrong, just slightly off). */
    (void)gradientColor; (void)gradientType;
    if ( vk_re_small.DrawStretchPic ) {
        vk_re_small.DrawStretchPic( x, y, w, h, s1, t1, s2, t2, hShader );
    }
}

static void vk_re_C_ZombieFXAddNewHit( int entityNum, const vec3_t hitPos, const vec3_t hitDir ) {
    (void)entityNum; (void)hitPos; (void)hitDir;
    vk_re_C_log_ZombieFXAddNewHit();
    /* RealRTCW-specific zombie blood/spark FX. No Q3e equivalent. No-op
     * is observationally inert — the zombies will lack hit-spark VFX
     * but combat is functional. */
}
```

In `CL_BuildVulkanRefExport`:

```c
    /* Group C — RTCW-only stubs */
    vk_re_small.RegisterSmartSkin     = vk_re_C_RegisterSmartSkin;
    vk_re_small.GetSkinModel          = vk_re_C_GetSkinModel;
    vk_re_small.GetShaderFromModel    = vk_re_C_GetShaderFromModel;
    vk_re_small.AddPolysToScene       = vk_re_C_AddPolysToScene;
    vk_re_small.AddCoronaToScene      = vk_re_C_AddCoronaToScene;
    vk_re_small.SetFog                = vk_re_C_SetFog;
    vk_re_small.DrawStretchPicGradient = vk_re_C_DrawStretchPicGradient;
    vk_re_small.ZombieFXAddNewHit     = vk_re_C_ZombieFXAddNewHit;
```

- [ ] **Step 2: Build clean**

Same build command. Expect clean.

- [ ] **Step 3: Smoke + read tripwire log**

```bash
cd /Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt && timeout 20 ./build/release-darwin-arm64/wolfsp.app/Contents/MacOS/wolfsp +set cl_renderer vulkan 2>&1 | tee docs/vulkan-phase2/2026-06-12-m5-tripwire-smoke.log; echo "exit=$?"
grep "M5/C" docs/vulkan-phase2/2026-06-12-m5-tripwire-smoke.log
```

Expected: zero or more `[M5/C]` lines. **What we want to learn:** which RTCW-only slots fire during boot+main-menu path. Likely candidates: `RegisterSmartSkin` (cgame asset registration), `SetFog` (level fog setup, but only on level load — boot menu should NOT fire it).

- [ ] **Step 4: Decide follow-up**

If tripwire log shows ≤2 slots fire and they look benign → proceed to Task 8 (codify).
If 3+ fire or one fires in a hot loop (>100 times) → add inline follow-up tasks routing each hot slot to a real translation. The fallback bodies above are starting points.

- [ ] **Step 5: Commit**

```bash
git add code/client/cl_refvulkan_export.c docs/vulkan-phase2/2026-06-12-m5-tripwire-smoke.log
git commit -m "feat(m5): Group C tripwire stubs for 8 RTCW-only refexport slots"
```

---

### Task 8: Codify findings + update memory

**Files:**
- Create: `notes/decisions/2026-06-12-m5-refexport-translator-landed.md`
- Update: `/Users/ragnar/.claude/projects/-Users-ragnar-fedorov-tech-RealRTCW-macOS/memory/project_m5_refexport_divergence.md` (mark closed)
- Update: `/Users/ragnar/.claude/projects/-Users-ragnar-fedorov-tech-RealRTCW-macOS/memory/MEMORY.md` (link to new M6 memory if Task 6 smoke shows scene-build struct issues)
- Optionally create: `/Users/ragnar/.claude/projects/-Users-ragnar-fedorov-tech-RealRTCW-macOS/memory/project_m6_scene_struct_translation.md` if M6 surface area surfaced

- [ ] **Step 1: Use `codify-findings` skill**

The skill walks through notes/decisions + notes/reference + memory updates with proper cross-links. Invoke per its instructions.

- [ ] **Step 2: Commit codification**

```bash
git add notes/decisions/2026-06-12-m5-refexport-translator-landed.md
git commit -m "docs(m5): landed — refexport translator + tripwire findings"
```

- [ ] **Step 3: Push (after ragnar's explicit OK per [[feedback_push_gating]])**

Do NOT push without asking. Once OK'd:

```bash
git push origin macos-arm64-vulkan
```

---

## Self-Review (writing-plans skill checklist)

**Spec coverage:**
- §0 dep-map → Tasks 0-7 cover Groups A, B, C; Group D explicitly out of scope (engine never references those slots).
- Wire-in point identified → Task 1 Step 5 modifies `cl_main.c:3517`.
- Two-file split addressing tr_public.h coexistence problem → Task 1 creates `cl_refvulkan_export.c` (sees SMALL); existing `cl_refvulkan.c` retains BIG view; thunks bridge via void *.
- Tripwire approach for RTCW-only slots → Task 7.
- M6 deferral explicitly recorded → §scope boundary block + Task 5 inline comment.

**Placeholder scan:** No `TBD`, `TODO: fill in details`. Inline `TODO M5-followup` markers in Group C stub bodies are intentional — they're decisions deferred until tripwire data, not unwritten plan steps.

**Type consistency:**
- Thunks named `vk_re_thunk_X`, wrappers `vk_re_wrap_X`, tripwires `vk_re_C_X` — convention consistent.
- Slot-getter macro `VK_RE_GET(name)` consistent.
- `CL_BuildVulkanRefExport` return type is `void *` everywhere (header decl + call site cast in cl_main.c + implementation).

---

## Execution Handoff

Plan saved to `docs/superpowers/plans/2026-06-12-m5-refexport-translator.md`.

Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, you review between tasks, fast iteration. Good fit because tasks are independent and we've validated the dep-map together.

**2. Inline Execution** — execute tasks in this session using `superpowers:executing-plans`, batch with checkpoints at the end of Tasks 2, 6, 7.

Which approach?
