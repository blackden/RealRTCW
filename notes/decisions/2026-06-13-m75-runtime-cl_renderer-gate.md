# M7.5 step 0: runtime cl_renderer gate for refImport_t shape

**Date:** 2026-06-13
**Branch:** macos-arm64-vulkan
**Touch:** `code/client/cl_main.c` `CL_InitRef`

## Symptom

With engine built `BUILD_RENDERER_VULKAN=1` and default `cl_renderer="opengl1"`,
boot segfaulted at `R_Init` with `Hunk_Alloc(~3 GB)` for `backEndData_t`. Reported
in [[project-m7-jpg-loader-closed]] as the M7.5 blocker. The symptom was a
non-power-of-two garbage number, not a clean multiple — that ruled out
const-regression and pointed at corrupted cvar reads.

## Root cause

`CL_InitRef` chose between the BIG (Quake3e-shaped) `refImport_t` translator
(`CL_BuildVulkanRefImport()`) and the SMALL (RealRTCW-native) `&ri` at
**compile time**:

```c
#ifdef BUILD_RENDERER_VULKAN
    ret = GetRefAPI(REF_API_VERSION, (refimport_t *)CL_BuildVulkanRefImport());
#else
    ret = GetRefAPI(REF_API_VERSION, &ri);
#endif
```

Once the engine binary was compiled with the flag, every `dlopen`'d renderer
DLL got the BIG vtable — regardless of which DLL was actually loaded. The
legacy OpenGL renderer at the SMALL `ri.Cvar_Get` slot offset hit a different
function pointer in BIG, returned garbage as `cvar_t*`, and
`r_maxpolys->integer` read from arbitrary memory fed the `Hunk_Alloc` size.

## Fix

Replace the compile-time gate with a runtime check on `cl_renderer->string`:

```c
#ifdef BUILD_RENDERER_VULKAN
    const qboolean useVulkanRefImport =
        (Q_stricmp(cl_renderer->string, "vulkan") == 0);
#else
    const qboolean useVulkanRefImport = qfalse;
#endif

    if (useVulkanRefImport) { /* BIG path */ } else { /* SMALL path */ }
```

`cl_renderer` is initialized earlier at the top of `CL_InitRef` (line 3407),
so the cvar is always available when the gate runs. The
`BUILD_RENDERER_VULKAN`-conditional translator calls stay wrapped in `#ifdef`
so engines built without the flag don't reference Vulkan-only symbols.

Both renderer DLLs are now reachable at runtime from a single binary — the
core design principle the prior compile-time gate accidentally violated.

## Alternatives considered

- **Status quo + force `cl_renderer vulkan` everywhere**: leaves the legacy
  OpenGL path permanently broken with `BUILD_RENDERER_VULKAN` builds. Doesn't
  fit RealRTCW's user-facing requirement that both renderers work.
- **Refactor `ri` to BIG shape and have OpenGL renderer adapt**: too invasive
  for an unblock fix, and inverts the M3.5 translator ownership (BIG belongs
  to vendored renderervk, SMALL is the RealRTCW-native legacy boundary).
- **Build-time toggle (two binaries)**: doubles ship surface, breaks runtime
  switching, regressive vs current single-binary design.

## Cross-links

- [[project-m7-jpg-loader-closed]] — earlier M7 close.
- `notes/plans/2026-06-12-m7-jpg-loader-and-m8-shader-keywords.md` — plan body
  with M7.5 fog-investigation step (now unblocked).
- [[project-m6-types-unification]] — M6 baseline this unblocks.
