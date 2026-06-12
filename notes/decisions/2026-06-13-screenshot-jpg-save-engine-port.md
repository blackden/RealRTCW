# M-screenshot JPG save engine-side (2026-06-13)

Closes the `+screenshotJPEG` SIGSEGV on the Vulkan renderer. Mirror to M7
(which closed the JPG **load** side); same Quake3e-canonical pattern,
zero vendor edits in `code/renderervk/`.

## Where we ended

3 commits' worth of diff on `macos-arm64-vulkan` after `dce929d` (M7.5
HEAD). 3 files touched:

| File | Change |
|---|---|
| `code/client/cl_jpeg.c` | **Extended** (~140 → ~280 lines). Ported `RE_SaveJPGToBuffer`, `RE_SaveJPG`, plus the libjpeg destination manager (`my_destination_mgr`, `cl_jpeg_init_destination`, `cl_jpeg_empty_output_buffer`, `cl_jpeg_term_destination`, `cl_jpeg_dest`) from `code/renderer/tr_image_jpg.c`. `ri.*` → engine-direct `Com_Error` / `Hunk_AllocateTempMemory` / `Hunk_FreeTempMemory` / `FS_WriteFile`. |
| `code/qcommon/qcommon.h` | Added `CL_SaveJPG` + `CL_SaveJPGToBuffer` prototypes next to `CL_LoadJPG`. Extended the comment block to cover both. |
| `code/client/cl_refvulkan.c` | Added forward decls + wrappers `vk_CL_SaveJPG` and `vk_CL_SaveJPGToBuffer`. Wired both into `vk_ri` adjacent to `vk_ri.CL_LoadJPG`. Removed both names from the «intentionally NULL» comment block. |

Smoke verification: `scripts/mac/playtest.sh --vulkan --auto +screenshotJPEG smoketest` reaches main menu, schedules screenshot at next frame end, writes `screenshots/smoketest.jpg` (196 KB, valid JFIF 1.01 baseline JPEG, 1680×1050, 3 components). Clean exit 0, no ASAN/UBSAN reports. Independently verified on a separate `+map escape1 +screenshotJPEG escape1_*` run where the screenshot still wrote correctly even after a downstream server-crash recovery to main menu — proving `RB_TakeScreenshotJPEG`'s `ri.CL_SaveJPG` call no longer NULL-derefs.

## Decision — engine-side CL_SaveJPG{,ToBuffer}, mirror to M7

**Decision.** Put the libjpeg compressor in `code/client/cl_jpeg.c`, expose via `CL_SaveJPG` and `CL_SaveJPGToBuffer`, route through new `vk_CL_SaveJPG{,ToBuffer}` slots in the M3.5 refImport translator. Zero vendor edits in `code/renderervk/` or `code/renderercommon/`. Same Quake3e-canonical shape as M7's load side.

**Why.** The M3.5 translator at `cl_refvulkan.c:286-298` deliberately left both `vk_ri.CL_SaveJPG` and `vk_ri.CL_SaveJPGToBuffer` NULL with an explicit comment («not present in RealRTCW engine»). That comment was true at M3.5 — engine had no JPG-write code. But `RB_TakeScreenshotJPEG` (`code/renderervk/tr_init.c:833`) unconditionally calls `ri.CL_SaveJPG(...)` and `RB_TakeVideoFrameCmd` (`tr_init.c:1178`) calls `ri.CL_SaveJPGToBuffer(...)` — so the NULL-slot was a latent SIGSEGV waiting for any user to type `\screenshotJPEG`.

Three alternatives were considered briefly:

1. **Engine-side `CL_SaveJPG`** (chosen). Mirrors what M7 did for the load side. Both halves of the libjpeg API now live engine-side, refImport stays clean.
2. **Renderer-side direct call.** Renderervk has no libjpeg link of its own; would require adding `JPGOBJ` to the renderervk dylib. Diverges from Quake3e structure for no gain.
3. **Stub permanently.** «Screenshots are an edge-case.» Falsified by today's test plan — M7.5 step 2 (fog comparison) needs Vulkan screenshots, and AVI capture (`RB_TakeVideoFrameCmd`) hits the same code path.

Picked (1) because it's the architecturally honest design AND the same shape as M7's load side AND the engine binary already links libjpeg (M7 work) — so this is a zero-cost extension.

**Trade-off.** None substantive. The engine binary already links libjpeg from M7, so no link-line changes. `Hunk_AllocateTempMemory` takes `int`, so width × height × 3 is cast to int — fine for any realistic screenshot (16K × 16K × 3 = 768 MB, still in int31). The `empty_output_buffer` fatal-on-overflow matches legacy behavior exactly.

**Revisit if.** Per-pixel-format alpha-preserving JPEG (or a PNG screenshot path) — would extend `CL_SavePNG` with separate alloc / sig. Out of scope today.

## Plan-time miss

None this time. The fix path was clear from the M7 pattern. Total wall-clock from root-cause-found to smoke-green: ~25 minutes.

## Epistemic note

The handoff state phrased `+screenshotJPEG` segfault as an «M-candidate to isolate the readback path» — implying the bug might be in `vk_read_pixels` MoltenVK-specific. That was the wrong epistemic prior. The actual bug was one layer earlier (NULL function-pointer call before readback even ran). Took ~5 minutes of code reading at `RB_TakeScreenshotJPEG` → `ri.CL_SaveJPG` → grep the translator → comment block calling out NULL slot — to flip the prior.

**Lesson.** When a Q3-family renderer call segfaults early, **check refImport translator NULL-slots before suspecting the renderer's deeper machinery.** The translator carries an inventory of «known-NULL» slots in its comment block — those are landmines waiting for a caller.

## Cross-links

- `notes/decisions/2026-06-12-m7-jpg-loader-engine-side.md` — M7 load-side mirror. This is the bookend.
- `code/client/cl_refvulkan.c:286-298` — the historical «intentionally NULL» block. Two entries removed today.
- [[project-m5-refexport-divergence]] — M5 translator that this extends.
