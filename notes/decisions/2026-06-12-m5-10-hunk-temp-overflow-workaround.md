# M5.10 closed — vendored renderervk image-upload temp-overflow fix (2026-06-12)

Follow-up to [[2026-06-12-m5-refexport-translator-landed]]. With M5's refexport translator in place, Vulkan boot reached UI vm init and immediately crashed at `Hunk_FreeTempMemory: bad magic`. M5.10 fixes the bug at its source in the vendored renderervk.

## Final state (after 2026-06-12 evening update)

Two-step landing. The first commit (`294ba69`) shipped a defensive **shim workaround** in `code/client/cl_refvulkan.c::vk_Hunk_AllocateTempMemory` (round size==0 to 4096) — it unblocked boot but the bug was masked, not fixed. The second commit (this work) lands the **proper renderer-side fix** in `code/renderervk/tr_image.c:633` (clamp scaled values in the alloc formula so it matches what the later memcpy uses) and **removes the shim** — single source of truth, no more `4 KiB` waste per affected alloc.

The renderer-side fix requires `REALRTCW_ALLOW_VENDOR_EDIT=1`. Vendored-prefix convention is satisfied via the `RealRTCW M5.10 fix:` comment block at the patched call site, which cross-references this decision doc so anyone diffing against ec-/Quake3e upstream understands the divergence.

## Symptom

`Client fatal crashed: Hunk_FreeTempMemory: bad magic` during UI vm init, specifically during the `AssetCache → trap_R_RegisterShaderNoMip` path. Bad-magic crash signature was deterministic — same byte pattern (`0x896a8ea5 0x0000003c`) at the same hunk offset every run.

## Root cause

`generate_image_upload_data()` in `code/renderervk/tr_image.c` has a pre-clamp / post-clamp mismatch:

- **Line 633** (alloc): `upload_data->buffer = ri.Hunk_AllocateTempMemory( 2 * 4 * scaled_width * scaled_height )` uses **PRE-clamp** scaled values. For a NOSCALE image with one dimension == 0 (e.g., `0×8` placeholder asset in the dossier-portrait UI cache, several of those are loaded — see `R_FindImageFile could not find 'ui/assets/*_dossier.jpg'` warnings in any boot log), `scaled_w * scaled_h = 0` and the alloc returns a 0-byte block.
- **Lines 672-677** clamp scaled to a minimum of 1 in each dimension.
- **Line 720** (memcpy): `Com_Memcpy(upload_data->buffer, scaled_buffer, mip_level_size)` with `mip_level_size = scaled_width * scaled_height * 4` using **POST-clamp** values. At least `1 * other_dim * 4` bytes — 32 bytes for the failing 0×8 case.

So the renderer asks for 0 bytes but later writes 32. The overflow lands on the next temp allocation's `hunkHeader_t.magic` field. The corruption surfaces later when that next block is freed — engine reads the now-overwritten magic and aborts.

**Reproduction trace** (from M5.10 instrumented engine, paraphrased):

```
[alloc] size=0   → hdr=...360 (workaround → 16-byte block; later 4096)
[alloc] size=0   → hdr=...378 ← will be the bad-magic victim
[alloc] size=32  → hdr=...390
[memcpy line 720] 32 bytes from ...398 into ...368  ← OVERFLOWS past the 16-byte slot
                                                       into hdr=...378's magic field
[free  ...390] OK
[free  ...378] BAD MAGIC — corruption from line 720 memcpy
```

## Decision

**Decision.** Patch the alloc formula at `code/renderervk/tr_image.c:633` in-place. The alloc clamps each scaled dimension to ≥ 1 so it matches what the later memcpy at line ~720 uses (post-clamp, always ≥ 1):

```c
upload_data->buffer = (byte*) ri.Hunk_AllocateTempMemory(
    2 * 4 * (scaled_width  > 0 ? scaled_width  : 1)
          * (scaled_height > 0 ? scaled_height : 1) );
if ( data == NULL ) {
    Com_Memset( upload_data->buffer, 0,
        2 * 4 * (scaled_width  > 0 ? scaled_width  : 1)
              * (scaled_height > 0 ? scaled_height : 1) );
}
```

Subsequent code keeps using the original `scaled_width` and `scaled_height` variables. Specifically, the line 638 check `(scaled_width != width || scaled_height != height)` stays accurate — for NOSCALE 0-dim images, scaled_width / scaled_height remain 0, the condition stays false, and `ResampleTexture` is never called with degenerate (0, h) source dimensions.

**Why.** The renderer's intent at line 633 is "buffer for one image's mip chain". When dimensions degenerate to 0, the intent doesn't change but the formula produces 0. Clamping inline in the formula — without mutating the `scaled_*` variables — fixes the alloc size without disturbing the rest of the function's logic.

**Trade-off.** Vendored-tree divergence from ec-/Quake3e upstream. Mitigated by:
- The inline `RealRTCW M5.10 fix:` comment block at the patched site, cross-referencing this decision doc.
- The `REALRTCW_ALLOW_VENDOR_EDIT=1` escape hatch in the vendor-block hook (see `[[feedback-vendor-prefix-convention]]`).
- Compact patch shape (2 expression edits + 1 comment block) — easy to identify in a future re-vendor diff.

The earlier shim workaround in `cl_refvulkan.c` (size==0 → 4096) is removed in this same commit. It served as the M_now solution before we entered the vendored tree; with the renderer-side fix in place, the shim adds no defense and obscures the simpler `vk_Hunk_AllocateTempMemory` wrapper.

**Revisit if.**
- ec-/Quake3e upstream lands a similar fix. Then our patch becomes redundant and gets dropped at the next re-vendor — the inline comment will guide the reviewer.
- A different code path hits the same bug pattern. Search renderervk for other `ri.Hunk_AllocateTempMemory(...scaled...)` call sites that might also have pre/post-clamp asymmetry; the obvious candidate is `tr_image.c:639` (resampled_buffer), though that path doesn't fire for the M5.10 NOSCALE 0-dim case.

## How we got here

M5.10 triage took ~3 hours across two sessions. Important pivots:

1. **First hypothesis** — renderer freeing a Hunk_Alloc'd permanent pointer as if it were temp. Disproved by hunk bank-state dump: the bad hdr was inside `hunk_low.temp` range, not permanent.
2. **Second hypothesis** — 0-byte alloc letting renderer write OOB into next header. Half-right but wrong about the mechanism. The workaround at this stage (`size==0 → 16`) didn't fix the crash; bad-magic just moved one slot down in the LIFO stack. Same deterministic `0x896a8ea5 0x0000003c` bytes appeared at the same relative offset.
3. **Decisive instrumentation** — readback of `hdr->magic` immediately after `Hunk_AllocateTempMemory` return AND immediately before passing to `Hunk_FreeTempMemory`. Showed magic was correctly written by the alloc and corrupted before the free — narrowing the corruption window to the renderer code between alloc and free.
4. **Source archaeology** — reading `generate_image_upload_data` carefully, the line 633 / line 720 mismatch surfaced. Math agreed with observed bytes (mip_level_size = 32 with post-clamp scaled product = 8, while line 633 alloc was 0).
5. **Workaround = 4096 bytes** — confirmed by smoke completing through `--- Common Initialization Complete ---`.

## Key M5.10 finding for future debugging

**Lesson.** When a `Hunk_FreeTempMemory: bad magic` fires with the same byte pattern across runs, the pattern is renderer-side image data being memcpy'd into a too-small temp buffer. The bytes are pixel values; the size field that decodes as `60` (0x3c) is a coincidence of two adjacent pixel R/G components looking like a Hunk header size. Not corruption from another allocator family.

## Files touched

- `code/renderervk/tr_image.c:633-636` — clamp `scaled_width`/`scaled_height` to ≥ 1 in the alloc formula and the matching `Com_Memset` (renderer-side fix).
- `code/client/cl_refvulkan.c::vk_Hunk_AllocateTempMemory` — earlier shim workaround REMOVED in the same commit; reverted to the original `Hunk_AllocateTempMemory((int)size)` passthrough.

## Smoke verification

- Pre-fix: `Hunk_FreeTempMemory: bad magic` at first UI shader registration.
- Post-renderer-fix (no shim): boot completes `--- Common Initialization Complete ---` → `Opening IP6 socket: [::]:27960` → `Opening IP socket: 0.0.0.0:27960` → clean `Client Shutdown (Client quit)` via `+quit`. Exit code 0.

Logs archived: `docs/vulkan-phase2/2026-06-12-m5-10-hunk-{instrumented,banks,trace,workaround,readback,bigger,renderer-fix}.log`.

## See also

- [[2026-06-12-m5-refexport-translator-landed]] — M5 closure that unblocked boot to reach this crash site.
- Memory: [[project-m5-10-hunk-free-temp-memory-landmine]] (now CLOSED).
- `code/renderervk/tr_image.c:633` — patched alloc site (RealRTCW M5.10 inline comment block lives here)
- `code/renderervk/tr_image.c:720` — memcpy that originally overflowed (now safe because alloc at 633 is sized correctly)
- `code/client/cl_refvulkan.c::vk_Hunk_AllocateTempMemory` — back to a passthrough wrapper, no special-case logic
