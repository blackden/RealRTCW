# M7 JPG loader engine-side (2026-06-12 evening)

Closes M7 milestone. Vulkan UI backgrounds (`realrtcw_background.jpg`) now decode via libjpeg instead of returning the M6 placeholder texture.

## Where we ended

1 commit on `macos-arm64-vulkan` after `b5f6b85` (M6 HEAD). 4 files touched:

| File | Change |
|---|---|
| `code/client/cl_jpeg.c` | **Created** (~140 lines). Port of `code/renderer/tr_image_jpg.c` R_LoadJPG half — engine-side libjpeg consumer. `ri.*` → `Com_/FS_/Z_Malloc` substitution. |
| `code/qcommon/qcommon.h` | Added `CL_LoadJPG` prototype + 5-line comment block. |
| `Makefile` | Added `cl_jpeg.o` to Q3OBJ, `JPEG_LIBS` to CLIENT_LIBS (`USE_INTERNAL_JPEG=0` branch), `JPGOBJ` to the USE_RENDERER_DLOPEN!=0 CLIENTBIN link command. |
| `code/client/cl_refvulkan.c` | Rewrote `vk_CL_LoadJPG` from NULL-stub to delegate `CL_LoadJPG(filename, pic, width, height)`. Updated the comment block at the slot-fill site (line 268-273) from "NO-OP STUB" to "DIRECT WIRE (M7)". |

Smoke verification: `scripts/mac/playtest.sh --vulkan` reaches main menu, background renders with cover-art colors (not the M6 placeholder), `--- Common Initialization Complete ---`, clean exit 0, no ASAN/UBSAN reports. No "couldn't find image realrtcw_background.tga" warnings.

## Decision — engine-side CL_LoadJPG, Quake3e canonical

**Decision.** Put libjpeg consumer in `code/client/cl_jpeg.c` (engine binary), expose via `CL_LoadJPG`, route through the existing `vk_CL_LoadJPG` slot in the M3.5 refImport translator. Zero vendor edits in `code/renderervk/` or `code/renderercommon/`.

**Why.** Three alternatives were live during research:

1. **Engine-side `CL_LoadJPG`** (chosen). Mirrors Quake3e's design: `renderercommon/tr_image_jpg.c` is a thin shim that calls `ri.CL_LoadJPG`, and the engine binary provides that implementation. RealRTCW already vendors that thin shim from Quake3e; only the engine-side consumer was missing.
2. **Renderer-side direct call.** Could have made `vk_CL_LoadJPG` call legacy renderer's `R_LoadJPG` directly. But that requires the engine binary to call into a function in the renderer DLL, breaking the dlopen boundary, AND would couple Vulkan path to OpenGL .dylib presence.
3. **Stub permanently.** "Game content might never ship JPG textures." Falsified at M6 smoke — `realrtcw_background.jpg` and others DO ship.

Picked (1) because it's the architecturally honest design — libjpeg lives where the engine reads it, with one function pointer crossing the DLL boundary the same way as all other `CL_*` slots in refImport.

**Trade-off.** The engine binary now links libjpeg, adding ~few hundred KB. The legacy OpenGL renderer .dylib continues to link its own libjpeg copy (separate symbol space) — there's no shared state between them. If the renderer ever needs to be refactored to share libjpeg, that's a separate phase.

**Revisit if.** If RealRTCW ever switches to dlopen-the-engine-binary architecture or distributes the engine without libjpeg (e.g., a slim CLI variant), the CLIENT_LIBS link line will need adjustment. Currently both opt-in via `USE_INTERNAL_JPEG=1` and opt-out (`USE_INTERNAL_JPEG=0`, system libjpeg) paths are wired.

## Plan-time miss

The plan body in `notes/plans/2026-06-12-m7-jpg-loader-and-m8-shader-keywords.md` initially only added `cl_jpeg.o` to Q3OBJ. The first build failed with `Undefined symbols for architecture arm64: _jpeg_CreateDecompress, ...` — libjpeg symbols not linked into engine binary because:

- `JPGOBJ` (the libjpeg static sources) was only added to renderer .dylib link in the `USE_RENDERER_DLOPEN!=0` branch.
- `JPEG_LIBS` (system libjpeg `-ljpeg`) was only added to `RENDERER_LIBS`, not `CLIENT_LIBS`.

Both were corrected mid-flight. The fix is preserved in the committed Makefile diff.

**Lesson.** When relocating a consumer between binaries in this codebase, always grep both `*OBJ +=` and `*_LIBS +=` and `*_LIBS = ...` for the relevant library across the Makefile. The conditional `USE_RENDERER_DLOPEN` × `USE_INTERNAL_JPEG` matrix has four cells; only two were originally wired for engine binary.

## Epistemic correction

M6 handoff phrased "blurred/grayscale + asset path" as joint JPG-stub fallout. M7 smoke shows they're independent:

- **Asset path** (`realrtcw_background.tga not found`) — was JPG-related. Closed with M7. ✓
- **Blur** — persists after M7. Background decodes (color visible) but renders at low effective resolution. Either (a) low-res upstream asset stretched to 1920×1200, or (b) renderervk sampler/mipmap defect. M7.5 investigation classifies which.

## Bonus findings (from M7 smoke log)

Not blockers, recorded for future M-candidate planning:

- **Two shader files entirely ignored by parser** due to upstream syntax errors: `scripts/common.shader` line 842, `scripts/models_mapobjects_et.shader` line 1347, both "missing closing brace". Legacy renderer is more lenient; renderervk's Quake3e parser whole-file-rejects. These shaders contain hundreds of surface definitions — visual fallout is wider than the shader-keyword issue.
- **Shader keyword warnings** (4 sites in log) — confirms M8 candidate: `nofog` × 2 (`flareShader`, `sun`), `nocompress` × 2 (`console`, `console2`). These are NOT whole-file rejections — only "unknown general shader parameter" warnings that abort just THAT shader.
- **Audio format warning** `music file sound/music/l_theme.wav is not 22k stereo` — orthogonal, pre-existing.

## Cross-links

- [[project-m6-types-unification]] — M6 baseline this builds on.
- `notes/plans/2026-06-12-m7-jpg-loader-and-m8-shader-keywords.md` — execution plan (M7 LANDED, M7.5+M8 deferred).
- [[project-q3fork-dual-headers]] — Quake3e's engine-side CL_LoadJPG pattern is what RealRTCW now mirrors.
