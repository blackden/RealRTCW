# RealRTCW Vulkan M3 — Link Clean → Vulkan DLL Produced

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce `build/release-darwin-arm64-nosteam/renderer_sp_vulkan_arm64.dylib` linked clean (zero unresolved symbols) as a loadable Q3-style renderer DLL. Engine `RealRTCW.arm64` already links — only the Vulkan DLL link step fails.

**Architecture:** Vendored Quake3e `renderervk` dropped renderer-side cvar globals (`r_mode`, `r_fullscreen`, …) in favor of `ri.Cvar_VariableString` lookups via the vtable. RealRTCW's shared `code/sdl/sdl_glimp.c` still expects the engine-OpenGL renderer ABI — 11 symbols (`r_mode`, `r_fullscreen`, `r_noborder`, `r_colorbits`, `r_depthbits`, `r_stencilbits`, `r_stereoEnabled`, `r_swapInterval`, `displayAspect`, `haveClampToEdge`, `R_GetModeInfo`) unresolved when linking the Vulkan DLL. Solution: a RealRTCW-authored bridge file `code/renderervk/realrtcw_vk_window_bridge.c` that **defines** these 11 symbols as tentative globals, latches the cvar pointers via `ri.Cvar_Get` at init, and ports `R_GetModeInfo` verbatim from iortcw SP (`~/fedorov_tech/refs/iortcw/SP/code/renderer/tr_init.c:389`). The bridge links into the Vulkan DLL only; the OpenGL DLL keeps using the engine-side `code/renderer/tr_init.c` definitions unchanged.

**Tech stack:** C99, GNU Make, clang/ld for arm64 Mach-O dylib, Vulkan SDK 1.4.350.0 (`~/VulkanSDK/1.4.350.0`).

**Branch:** `macos-arm64-vulkan` (worktree `~/fedorov_tech/RealRTCW-vulkan-wt`), tip `4d7ae26`. All commits on this branch. **Push only after M3 review checkpoint with ragnar's explicit OK** (see memory `feedback_push_gating`).

**Reference materials:**
- Baseline link log: `docs/vulkan-phase2/m3-link-baseline.log` (already committed, 11 unresolved symbols all referenced from `sdl_glimp.o`).
- `R_GetModeInfo` reference port: `~/fedorov_tech/refs/iortcw/SP/code/renderer/tr_init.c:389`. Signature `qboolean R_GetModeInfo(int *width, int *height, float *windowAspect, int mode)`. Reads `r_customwidth`/`r_customheight` cvars when `mode == -1`; otherwise looks up `r_vidModes[mode]` table.
- Q3-fork reference (read-only): `~/fedorov_tech/refs/Quake3e/code/renderervk/`, `~/fedorov_tech/refs/iortcw/SP/code/renderer/`. **Use local refs first, GitHub MCP only if missing** (memory: `feedback_subagent_research_paralysis`).
- Vendor-prefix convention: any RealRTCW-authored file under `code/renderervk/` MUST start with `realrtcw_` — the hook auto-passes those (memory: `feedback_vendor_prefix_convention`).
- M2.4 conditional-include decision: `notes/decisions/2026-06-08-m2.4-vtable-adapter-shortcut.md` — explains the engine↔renderercommon side; M3 is the symmetric renderer-side gap.

**Out of scope:**
- Runtime correctness of the Vulkan DLL (M4 — first run + validation triage).
- Rendering correctness (M5).
- Engine-side `refImport_t` changes (closed at M2.4 with zero-init shortcut).
- The 10 ld warnings about libraries built for macOS 26.0 — pre-existing, not introduced here.

---

## File Structure

**Created:**
- `code/renderervk/realrtcw_vk_window_bridge.h` — one declaration: `void RealRTCW_VkBridgeInit(void);`. No exposed globals — the bridge owns the globals internally and consumers see them via the existing `extern` declarations in `code/renderer/tr_local.h` (which sdl_glimp.c already includes).
- `code/renderervk/realrtcw_vk_window_bridge.c` — defines the 11 globals, populates the cvar pointers via `ri.Cvar_Get`, ports `R_GetModeInfo` + `r_vidModes` table from iortcw SP.
- `docs/vulkan-phase2/m3-link-clean.log` — proof artifact: the final `make` log showing zero `Undefined symbols`.

**Modified:**
- `Makefile` — add `realrtcw_vk_window_bridge.c` to the Vulkan DLL object list. Find the rule that produces `renderer_sp_vulkan_arm64.dylib`; add bridge to the OBJ var feeding that link.
- `code/sdl/sdl_glimp.c` — one-line conditional call to `RealRTCW_VkBridgeInit()` near the top of `GLimp_Init`, guarded by `#ifdef BUILD_RENDERER_VULKAN`. This is the only engine-side change.

**Untouched (hook-protected):**
- `code/renderervk/tr_init.c`, `vk.c`, etc. — vendored Quake3e sources.
- `code/renderercommon/*` — same.
- `code/renderer/tr_local.h` — engine-OpenGL renderer header. The existing `extern` declarations there already match the symbol names the bridge defines; we don't need to touch the header.

---

## Task 1: Locate the Vulkan DLL link rule in Makefile

End state: precise line numbers and variable names recorded for the renderer_sp_vulkan_arm64.dylib link rule, so subsequent tasks can patch it without grep-and-pray.

**Files:**
- Read-only: `Makefile`
- Record: `docs/vulkan-phase2/m3-link-baseline.log` (already committed)

- [ ] **Step 1: Find the Vulkan DLL target rule**

```bash
cd ~/fedorov_tech/RealRTCW-vulkan-wt
awk '/renderer_sp_vulkan/ {print NR": "$0}' Makefile | head -20
```

Expected: lines naming the dylib target, its prereq object list variable (likely `Q3R2VKOBJ`, `RVKOBJ`, or similar), and the recipe line (`$(LD) ... -o $@`).

- [ ] **Step 2: Find the object-list variable for the Vulkan DLL**

```bash
awk '/^Q3R2VKOBJ|^RVKOBJ|^RENDERERV?VK.*OBJ|^VKOBJ/ {print NR": "$0}' Makefile | head -20
```

Expected: the variable name and its assignment line. Record both. Also note whether the assignment lists individual `.o` files or uses `$(addprefix ...)`/wildcard — drives how the bridge object is added in Task 4.

- [ ] **Step 3: Find sdl_glimp.o build rule for the Vulkan DLL**

```bash
awk '/sdl_glimp/ {print NR": "$0}' Makefile | head -20
```

Expected: confirm sdl_glimp.c is compiled (possibly twice) and which CFLAGS set is used for the Vulkan-DLL build. Record whether `BUILD_RENDERER_VULKAN` is defined when compiling sdl_glimp.c for the Vulkan side.

- [ ] **Step 4: Record findings as inline notes — no commit yet**

Write a 4-line note in your task tracker:
- DLL target: `Makefile:<line>` → `$(B)/renderer_sp_vulkan_arm64.dylib`
- OBJ variable: `<NAME>` defined at `Makefile:<line>`
- sdl_glimp.o is compiled with `BUILD_RENDERER_VULKAN` defined: yes / no
- Recipe line: `Makefile:<line>`

No commit. This is reconnaissance for Task 4.

---

## Task 2: Port `R_GetModeInfo` and `r_vidModes` from iortcw SP

End state: a standalone, compilable port of `R_GetModeInfo` ready to drop into the bridge. Done in isolation here so it can be reviewed against the iortcw original before plugging in.

**Files:**
- Read: `~/fedorov_tech/refs/iortcw/SP/code/renderer/tr_init.c`
- Write (draft, no commit): `/tmp/m3-bridge-draft.c`

- [ ] **Step 1: Read the iortcw `R_GetModeInfo` and the `r_vidModes` table it depends on**

```bash
awk 'NR>=350 && NR<=460 {print NR": "$0}' ~/fedorov_tech/refs/iortcw/SP/code/renderer/tr_init.c
```

Expected: the function at line ~389 plus the `r_vidModes[]` table above it (~340-380). Record:
- `r_vidModes` element struct shape (likely `{ const char *description; int width, height; float pixelAspect; }`)
- How `mode == -1` is handled (custom width/height path)
- Which cvars it reads (likely `r_customwidth`, `r_customheight`, `r_customaspect`)

- [ ] **Step 2: Verify the same cvars exist on RealRTCW engine side**

```bash
awk '/r_customwidth|r_customheight|r_customaspect/ {print FILENAME":"NR": "$0}' code/renderer/tr_init.c code/renderervk/tr_init.c
```

Expected: at least one match per cvar in renderervk/tr_init.c (Quake3e keeps them; they're registered via `ri.Cvar_Get`). If any are missing, note it — the bridge will register them itself in that case.

- [ ] **Step 3: Draft the ported code in /tmp**

```bash
cat > /tmp/m3-bridge-draft.c <<'EOF'
/* Draft port — not yet wired. Verify against iortcw original before committing. */

typedef struct vidmode_s {
    const char *description;
    int         width, height;
    float       pixelAspect;
} vidmode_t;

/* Table verbatim from iortcw SP tr_init.c. Update bottom comment with the
 * exact source-line range you copied from. */
static const vidmode_t r_vidModes[] = {
    /* (filled per Step 1 reading) */
};
static const int s_numVidModes = sizeof(r_vidModes) / sizeof(r_vidModes[0]);

qboolean R_GetModeInfo(int *width, int *height, float *windowAspect, int mode)
{
    const vidmode_t *vm;
    float           pixelAspect;

    if (mode < -2) return qfalse;
    if (mode >= s_numVidModes) return qfalse;

    if (mode == -2) {
        /* desktop resolution; sdl_glimp.c handles this case before calling us */
        return qfalse;
    }

    if (mode == -1) {
        *width  = ri.Cvar_VariableIntegerValue("r_customwidth");
        *height = ri.Cvar_VariableIntegerValue("r_customheight");
        pixelAspect = ri.Cvar_VariableValue("r_customaspect");
    } else {
        vm = &r_vidModes[mode];
        *width  = vm->width;
        *height = vm->height;
        pixelAspect = vm->pixelAspect;
    }

    *windowAspect = (float)*width / ((float)*height * pixelAspect);
    return qtrue;
}
EOF
```

Fill in the `r_vidModes` table from the actual iortcw source range read in Step 1. **Do not invent entries** — copy exactly.

- [ ] **Step 4: Cross-check signatures against sdl_glimp.c callsite**

```bash
awk '/R_GetModeInfo/ {print NR": "$0}' code/sdl/sdl_glimp.c
```

Expected: call at `GLimp_SetMode` like `R_GetModeInfo( &glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect, mode )`. Confirm types match the draft signature `qboolean R_GetModeInfo(int *, int *, float *, int)`. If anything differs, fix the draft now, not later.

No commit. Draft stays in /tmp until Task 3.

---

## Task 3: Create the bridge files

End state: `realrtcw_vk_window_bridge.h` + `.c` exist, compile cleanly against `renderervk` CFLAGS, define all 11 symbols, expose one init function.

**Files:**
- Create: `code/renderervk/realrtcw_vk_window_bridge.h`
- Create: `code/renderervk/realrtcw_vk_window_bridge.c`

- [ ] **Step 1: Write the header**

```c
/*
 * RealRTCW Vulkan window-bridge — defines renderer-side cvar globals and
 * R_GetModeInfo that Quake3e renderervk dropped but RealRTCW's shared
 * sdl_glimp.c still expects. Linked only into renderer_sp_vulkan_arm64.dylib.
 *
 * Call RealRTCW_VkBridgeInit() once, before sdl_glimp.c first touches any of
 * the cvar globals. The bridge owns the cvar_t* pointers; consumers see them
 * via the existing extern declarations in code/renderer/tr_local.h.
 */
#ifndef REALRTCW_VK_WINDOW_BRIDGE_H
#define REALRTCW_VK_WINDOW_BRIDGE_H

void RealRTCW_VkBridgeInit(void);

#endif /* REALRTCW_VK_WINDOW_BRIDGE_H */
```

Write to `code/renderervk/realrtcw_vk_window_bridge.h`.

- [ ] **Step 2: Write the implementation skeleton — includes + tentative globals**

```c
/*
 * RealRTCW Vulkan window-bridge implementation.
 * See realrtcw_vk_window_bridge.h for purpose.
 *
 * The 11 symbols below match the extern declarations in
 * code/renderer/tr_local.h that sdl_glimp.c reads. We define them here so
 * the Vulkan DLL link resolves; the OpenGL DLL keeps using the engine-side
 * definitions in code/renderer/tr_init.c unchanged.
 */
#include "tr_local.h"                       /* renderervk-side tr_local.h, gives us ri */
#include "realrtcw_vk_window_bridge.h"

/* --- 11 globals matching code/renderer/tr_local.h externs ----------------- */

cvar_t  *r_mode;
cvar_t  *r_fullscreen;
cvar_t  *r_noborder;
cvar_t  *r_colorbits;
cvar_t  *r_depthbits;
cvar_t  *r_stencilbits;
cvar_t  *r_stereoEnabled;
cvar_t  *r_swapInterval;

float    displayAspect;
qboolean haveClampToEdge;

/* --- R_GetModeInfo + r_vidModes ------------------------------------------ */

/* (pasted from Task 2 draft after cross-check) */
```

Write to `code/renderervk/realrtcw_vk_window_bridge.c`. Paste the `r_vidModes` table + `R_GetModeInfo` function body from `/tmp/m3-bridge-draft.c` after the comment marker.

- [ ] **Step 3: Add `RealRTCW_VkBridgeInit` at the bottom of the .c file**

```c
void RealRTCW_VkBridgeInit(void)
{
    static qboolean initialized = qfalse;
    if (initialized) return;
    initialized = qtrue;

    /* Latch cvar pointers via the vtable. Flags chosen to match what
     * renderervk/tr_init.c registers them with — if you change flag here,
     * grep ri.Cvar_Get("r_mode" in renderervk/tr_init.c first.
     *
     * For each cvar, sdl_glimp.c reads ->integer, ->value, or ->modified.
     * As long as the cvar_t* is non-NULL after Cvar_Get, sdl_glimp.c is happy.
     */
    r_mode          = ri.Cvar_Get("r_mode",          "3",  CVAR_ARCHIVE | CVAR_LATCH);
    r_fullscreen    = ri.Cvar_Get("r_fullscreen",    "0",  CVAR_ARCHIVE | CVAR_LATCH);
    r_noborder      = ri.Cvar_Get("r_noborder",      "0",  CVAR_ARCHIVE | CVAR_LATCH);
    r_colorbits     = ri.Cvar_Get("r_colorbits",     "0",  CVAR_ARCHIVE | CVAR_LATCH);
    r_depthbits     = ri.Cvar_Get("r_depthbits",     "0",  CVAR_ARCHIVE | CVAR_LATCH);
    r_stencilbits   = ri.Cvar_Get("r_stencilbits",   "8",  CVAR_ARCHIVE | CVAR_LATCH);
    r_stereoEnabled = ri.Cvar_Get("r_stereoEnabled", "0",  CVAR_ARCHIVE | CVAR_LATCH);
    r_swapInterval  = ri.Cvar_Get("r_swapInterval",  "0",  CVAR_ARCHIVE);

    displayAspect   = 0.0f;          /* sdl_glimp.c writes this during GLimp_SetMode */
    haveClampToEdge = qtrue;         /* Vulkan always supports clamp-to-edge */
}
```

Append to `realrtcw_vk_window_bridge.c`. Note: defaults are chosen to match RealRTCW's OpenGL renderer `R_Register` defaults — Task 5.5 verifies via diff.

- [ ] **Step 4: Standalone compile check (no Makefile change yet)**

```bash
clang -c -DUSE_VULKAN_API -DBUILD_RENDERER_VULKAN \
    -I code/renderervk -I code/renderercommon -I code/qcommon \
    -include code/renderervk/realrtcw_shims.h \
    -mmacosx-version-min=11.0 -arch arm64 \
    code/renderervk/realrtcw_vk_window_bridge.c \
    -o /tmp/m3-bridge.o 2>&1 | head -30
```

Expected: zero errors. If `tr_local.h` cannot be found, double-check include paths against renderervk's existing CFLAGS (see Task 1.3 record). If `cvar_t` or `ri` is undefined, check that `tr_local.h` from `code/renderervk/` is the one being picked up (not the engine-side `code/renderer/tr_local.h`).

- [ ] **Step 5: Commit**

```bash
git add code/renderervk/realrtcw_vk_window_bridge.h code/renderervk/realrtcw_vk_window_bridge.c
git commit -m "build(vulkan/M3): renderer-side bridge for sdl_glimp.c globals

Quake3e renderervk dropped renderer-side cvar globals (r_mode etc.) in
favor of ri.Cvar_VariableString lookups. RealRTCW's shared sdl_glimp.c
still expects the old ABI. Bridge defines the 11 expected symbols
(8 cvar pointers + displayAspect + haveClampToEdge + R_GetModeInfo) and
latches the cvar pointers via ri.Cvar_Get at init time.

R_GetModeInfo ported verbatim from iortcw SP tr_init.c:389.

Linked only into renderer_sp_vulkan_arm64.dylib by the next commit.
No effect on the OpenGL renderer DLL."
```

---

## Task 4: Wire bridge into Vulkan DLL Makefile

End state: `realrtcw_vk_window_bridge.o` is built and linked into `renderer_sp_vulkan_arm64.dylib`. The OpenGL DLL is unaffected.

**Files:**
- Modify: `Makefile`

- [ ] **Step 1: Add bridge to the Vulkan-DLL OBJ list**

Use the variable name + line number recorded in Task 1.2. The pattern usually looks like (concrete identifier from Task 1):

```makefile
Q3R2VKOBJ = \
    $(B)/renderervk/tr_animation.o \
    ...
    $(B)/renderervk/vk_vbo.o \
    $(B)/renderervk/realrtcw_vk_window_bridge.o   ← add this line
```

Append the bridge to the list immediately after the last `renderervk/*.o` entry.

- [ ] **Step 2: Confirm the existing `renderervk/%.o` pattern rule covers the bridge**

```bash
awk '/renderervk\/.*\.o:/ {print NR": "$0; getline; print NR": "$0}' Makefile | head -20
```

Expected: a generic pattern rule like `$(B)/renderervk/%.o: $(MOUNT_DIR)/renderervk/%.c` already exists. The bridge is just another `.c` file in that directory, so it picks up the rule automatically. If the Makefile lists files explicitly per-file (no pattern), add an explicit rule mirroring the others.

- [ ] **Step 3: Dry-run build, check the bridge is included**

```bash
make -n ARCH=arm64 USE_RENDERER_DLOPEN=1 BUILD_RENDERER_VULKAN=1 USE_OPENAL=1 USE_INTERNAL_LIBS=0 2>&1 | grep -E "realrtcw_vk_window_bridge|renderer_sp_vulkan" | head -20
```

Expected: at least one line compiling `realrtcw_vk_window_bridge.c` to `.o`, and the link line for `renderer_sp_vulkan_arm64.dylib` includes the bridge `.o` in its inputs.

- [ ] **Step 4: Commit (Makefile-only)**

```bash
git add Makefile
git commit -m "build(vulkan/M3): link realrtcw_vk_window_bridge.o into Vulkan DLL"
```

---

## Task 5: Wire the init call from sdl_glimp.c

End state: `sdl_glimp.c` calls `RealRTCW_VkBridgeInit()` at the top of `GLimp_Init`, guarded by `#ifdef BUILD_RENDERER_VULKAN`. The cvar pointers are non-NULL before sdl_glimp.c touches them.

**Files:**
- Modify: `code/sdl/sdl_glimp.c`

- [ ] **Step 1: Find `GLimp_Init`**

```bash
awk '/^void GLimp_Init/ {print NR": "$0}' code/sdl/sdl_glimp.c
```

Expected: the function definition line. Record the line number.

- [ ] **Step 2: Add the include + call**

In `code/sdl/sdl_glimp.c`:

a) Near the existing renderer-side include block (the one with `#include "../renderer/tr_local.h"`), add a Vulkan-guarded include:

```c
#ifdef BUILD_RENDERER_VULKAN
#include "../renderervk/realrtcw_vk_window_bridge.h"
#endif
```

b) Inside `GLimp_Init`, **as the very first statement** (before any cvar access, before `ri.Printf`, before SDL init):

```c
#ifdef BUILD_RENDERER_VULKAN
    RealRTCW_VkBridgeInit();
#endif
```

- [ ] **Step 3: Rebuild**

```bash
make ARCH=arm64 USE_RENDERER_DLOPEN=1 BUILD_RENDERER_VULKAN=1 USE_OPENAL=1 USE_INTERNAL_LIBS=0 -j8 2>&1 | tee /tmp/vk-m3-after-bridge.log | tail -40
```

Expected: zero `Undefined symbols`. The Vulkan DLL is produced. The OpenGL DLL build is unchanged (sdl_glimp.c still builds against the OpenGL renderer side via the existing tr_local.h include).

If unresolved symbols remain, grep for them in `realrtcw_vk_window_bridge.c` — most likely a typo in a global name vs. the sdl_glimp.c reference. The 11 expected globals are listed verbatim in the baseline log (`docs/vulkan-phase2/m3-link-baseline.log`).

- [ ] **Step 4: Verify the OpenGL DLL still links (regression guard)**

Same `make` command also rebuilds `renderer_sp_opengl1_arm64.dylib`. Check it succeeded:

```bash
ls -la build/release-darwin-arm64-nosteam/renderer_sp_opengl1_arm64.dylib build/release-darwin-arm64-nosteam/renderer_sp_vulkan_arm64.dylib
file build/release-darwin-arm64-nosteam/renderer_sp_vulkan_arm64.dylib
```

Expected: both dylibs exist, Vulkan one reported as `Mach-O 64-bit dynamically linked shared library arm64`.

- [ ] **Step 5: Sanity-check cvar default values vs OpenGL renderer**

Verify the defaults in `RealRTCW_VkBridgeInit` match RealRTCW's OpenGL `R_Register`:

```bash
awk '/r_mode\b|r_fullscreen\b|r_noborder\b|r_colorbits\b|r_depthbits\b|r_stencilbits\b|r_stereoEnabled\b|r_swapInterval\b/ && /Cvar_Get/ {print NR": "$0}' code/renderer/tr_init.c
```

Expected: 8 lines, each showing the default value the engine-OpenGL renderer registers. If any differ from the bridge's defaults, update the bridge to match. **Identical defaults across DLLs** = `seta r_mode 5` in cfg behaves the same regardless of selected renderer.

- [ ] **Step 6: Capture proof artifact and commit**

```bash
cp /tmp/vk-m3-after-bridge.log docs/vulkan-phase2/m3-link-clean.log
git add code/sdl/sdl_glimp.c docs/vulkan-phase2/m3-link-clean.log
git commit -m "build(vulkan/M3): wire bridge init from sdl_glimp.c — link clean

Calls RealRTCW_VkBridgeInit() as the first statement of GLimp_Init when
BUILD_RENDERER_VULKAN is defined, populating the 11 globals before any
cvar dereference. OpenGL DLL build path is unchanged (guard prevents the
include + call from leaking into the OpenGL CFLAGS set).

renderer_sp_vulkan_arm64.dylib is now produced. Link log saved as
docs/vulkan-phase2/m3-link-clean.log for bisection reference."
```

---

## Task 6: M3 review checkpoint

End state: ragnar has seen the dylib + link log and either OK's the M3 push or flags issues.

- [ ] **Step 1: Summary for ragnar**

Post (1) before/after symbol counts (`11 → 0`), (2) `ls -la` of both renderer dylibs with sizes, (3) `file` output for the Vulkan one, (4) confirmation that no engine-side header was changed, (5) commit list. **Do not push.**

- [ ] **Step 2: On OK — push branch**

```bash
git push -u origin macos-arm64-vulkan
```

This is the first push of Phase 2.

- [ ] **Step 3: On OK — codify findings**

Use the `codify-findings` skill. Likely additions:
- `notes/decisions/2026-06-09-m3-renderer-side-cvar-bridge.md` — why bridge file vs. modifying vendored tr_init.c; why init from sdl_glimp.c vs. from a constructor.
- `notes/reference/engine-map.md` → new entry under "Renderer-ABI shim layer": "11 renderer-side globals expected by sdl_glimp.c, provided by `code/renderervk/realrtcw_vk_window_bridge.c` for Vulkan DLL".
- `memory/` → project entry update closing M3.

---

## Self-Review Notes

- **Spec coverage:** Baseline log has 11 unresolved symbols (`R_GetModeInfo`, `displayAspect`, `haveClampToEdge`, 8 cvars). Bridge `.c` defines all 11. Init function latches the 8 cvars + leaves the 2 scalars at sentinel values (sdl_glimp.c writes `displayAspect` itself in `GLimp_SetMode`; `haveClampToEdge` is true for Vulkan).
- **Placeholder scan:** No "TBD"/"TODO"/"similar to". Task 2 leaves the `r_vidModes` body as a copy-from-source action with the line range identified by reading at runtime — this is concrete enough.
- **Type consistency:** `RealRTCW_VkBridgeInit` named consistently across Tasks 3, 5. Header path `code/renderervk/realrtcw_vk_window_bridge.h` consistent across Tasks 3, 4, 5. `R_GetModeInfo` signature `qboolean R_GetModeInfo(int *, int *, float *, int)` matches sdl_glimp.c callsite verified in Task 2.4.
- **Risk callouts:** Push gating explicit at Task 6 (per `feedback_push_gating`). Vendor-prefix convention obeyed — bridge file starts with `realrtcw_`. No `REALRTCW_ALLOW_VENDOR_EDIT` needed.
- **One open uncertainty:** sdl_glimp.c is engine-shared. The Makefile may compile it once per DLL OR once total. Task 1.3 confirms which — if it's compiled once and shared between both renderer DLLs, the `#ifdef BUILD_RENDERER_VULKAN` guard means the same `.o` file would be missing the init call when linked into the OpenGL DLL. Acceptable — OpenGL DLL doesn't need the init call. If linkage is shared object file though, the bridge `.h` include path needs to resolve from both builds; safest is sdl_glimp.o compiled twice (once per DLL with that DLL's CFLAGS). Task 1.3 verifies.
