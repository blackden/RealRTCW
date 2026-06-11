# Iter 10 triage — refexport_t struct divergence (next M5 territory)

**Date:** 2026-06-12 (early hours)
**Branch:** macos-arm64-vulkan, HEAD `aa6ac11`

## Symptom

After iter 10 fix (vk_CL_LoadJPG stub), Vulkan boot reaches **`RE_LoadWorldMap: couldn't load white`** ERR_DROP, then engine recovery cycle triggers `VK_ERROR_NATIVE_WINDOW_IN_USE_KHR` on swapchain recreate, clean exit code 3.

R_Init completes fully (line 110 `----- finished R_Init -----`), Sound init OK, UI dylib loaded, then crash during UI's startup sequence.

## Root cause (suspected)

The engine's `cl_main.c:3326` calls `cls.whiteShader = re.RegisterShader("white")`. Error message says `RE_LoadWorldMap: couldn't load white` — implying the function pointer at `re.RegisterShader` slot offset is actually dispatching to `LoadWorld`.

This is the **refexport_t mirror of γ'-class issue**: SMALL refexport_t (`code/renderer/tr_public.h`) used by engine and BIG refexport_t (`code/renderercommon/tr_public.h`) populated by renderer DLL have **DIFFERENT struct layouts**. The Vulkan renderer fills BIG; engine reads as SMALL; slot offsets diverge from position 5 onward.

## Layout divergence (key delta points)

SMALL refexport_t (engine view, RTCW lineage):
- Includes RTCW-specific slots: `RegisterSmartSkin`, `GetSkinModel`, `GetShaderFromModel`, `AddPolysToScene`, `AddCoronaToScene`, `SetFog`, `DrawStretchPicGradient`, `ZombieFXAddNewHit`
- `AddRefEntityToScene` signature: 1 arg
- `AddPolyToScene` signature: 3 args
- `AddLightToScene` signature: has extra `int overdraw`
- `LerpTag` signature: `(tag, refent, tagName, startIndex)`

BIG refexport_t (renderervk view, Q3e lineage):
- Includes Q3e-specific slots: `AddAdditiveLightToScene`, `AddLinearLightToScene`, `inPVS`, `ThrottleBackend`, `FinishBloom`, `SetColorMappings`, `CanMinimize`, `GetConfig`, `VertexLighting`, `SyncRender`
- `AddRefEntityToScene` signature: 2 args (`refEntity_t *re, qboolean intShaderTime`)
- `AddPolyToScene` signature: 4 args (extra `int num`)
- `AddLightToScene` signature: no `overdraw` arg
- `LerpTag` signature: `(tag, model, startFrame, endFrame, frac, tagName)`

## Why this manifests as `re.RegisterShader → RE_LoadWorldMap`

Engine slot offset for RegisterShader = SMALL position 6 ≈ 5*sizeof(ptr) = 40 bytes from struct start.

Renderer's BIG slot at offset 40 bytes = depends on actual layout. Without counting exact alignments, it's possible the BIG slot at offset 40 is `LoadWorld` (or another slot calling `RE_LoadWorldMap`).

Confirmation would require:
1. Dump struct offsets via test program with both headers
2. Or set a breakpoint in `re.RegisterShader` callsite and step through to confirm it lands in `RE_LoadWorldMap`

## Solutions (M5 design space)

Same family of options as γ' explored, adapted to refexport direction:

**α5'** — Add `vk_BuildRefExport` translator in `cl_refvulkan.c` post-`GetRefAPI`:
- Receive BIG `refexport_t *` from renderer DLL
- Allocate engine-side SMALL `refexport_t`
- Wire each slot to a `vk_re_XXX` wrapper that translates signature + calls into BIG
- Mirrors `vk_BuildRefImport` symmetrically
- Wrappers handle signature differences (AddRefEntityToScene 1→2 args, etc.)
- RTCW-only slots: implement at RealRTCW engine level (RegisterSmartSkin, GetSkinModel etc.) — engine already has implementations, just route to them
- Q3e-only slots that engine ignores: silently dropped
- Cost: ~400-600 LoC similar to existing translator

**β5'** — Vendor-edit renderervk to layout-match SMALL refexport (UNLIKELY — diverges from upstream)

**γ5'** — Move shared rendering API extraction into `code/renderercommon/` for both (large Phase 3 work)

## Recommended path

**α5'** — full translator. Same engineering pattern as θ'/γ' work proved. Has known cost envelope.

## Next steps

1. Memory entry capture this finding so next session doesn't re-discover.
2. Update decision note with §6c: refexport_t divergence noted.
3. Plan M5 properly via `superpowers:writing-plans` skill — should NOT be ad-hoc inline.
4. Investigate alternative: maybe engine SMALL refexport can be vendor-edited at RealRTCW side (not vendored to upstream) to match BIG layout exactly. Cheaper than full translator.
