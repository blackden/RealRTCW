# M5.10 closed — vendored renderervk image-upload temp-overflow workaround (2026-06-12)

Follow-up to [[2026-06-12-m5-refexport-translator-landed]]. With M5's refexport translator in place, Vulkan boot reached UI vm init and immediately crashed at `Hunk_FreeTempMemory: bad magic`. M5.10 is the surgical fix: a 4-line `vk_Hunk_AllocateTempMemory` clamp in `code/client/cl_refvulkan.c`. Engine and renderer code unchanged.

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

**Decision.** Workaround in `vk_Hunk_AllocateTempMemory` (the engine-side translator wrapper for the BIG refImport_t slot, `code/client/cl_refvulkan.c`): if the renderer requests size==0, allocate 4096 bytes instead.

```c
static void *vk_Hunk_AllocateTempMemory( size_t size ) {
    if ( size == 0 ) {
        size = 4096;
    }
    return Hunk_AllocateTempMemory( (int)size );
}
```

**Why.** The renderer's intent at line 633 is "buffer for one image's mip chain". When dimensions degenerate to 0, the intent doesn't change but the formula produces 0. Giving 4096 bytes — enough for `4096 / 4 = 1024` pixels of mipmap data — covers any reasonable UI placeholder image. The boot smoke (`+set cl_renderer vulkan +quit`) completes cleanly to `--- Common Initialization Complete ---` and `Client Shutdown (Client quit)` with the workaround in place.

**Trade-off.** Workaround is in the translator shim, not where the bug actually lives. Future readers debugging UI image upload will see the symptom (0-byte alloc returning a 4KB block) and have to trace back to find the comment. Mitigated by the inline comment block citing the exact source line and crash backtrace.

The 4096-byte minimum also wastes 4KB per affected alloc — observed in M5.10 smokes: ~4-5 such allocs per UI init → ~20KB extra hunk_temp. Negligible against the 1 GiB hunk.

**Revisit if.**
- Renderervk gets patched upstream (in `wolfetplayer/RealRTCW` or `ec-/Quake3e`) to clamp scaled values before the line 633 alloc. Then the workaround can be removed.
- We bring `code/renderervk/tr_image.c` under `REALRTCW_ALLOW_VENDOR_EDIT=1` and apply the proper 4-line patch (add `if (scaled_width < 1) scaled_width = 1; if (scaled_height < 1) scaled_height = 1;` before line 633). Cleaner because the comment lives where the bug lives. Deferred so we keep the vendored tree minimally-touched until M5 → M6 transition.
- UI loads a larger placeholder image with one zero dimension that exceeds 4KB of mipmap data. Unlikely but signal: bad-magic returns at a larger temp offset. Bump the 4096 to 16384 or higher.

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

- `code/client/cl_refvulkan.c` — 4-line workaround in `vk_Hunk_AllocateTempMemory` + comment block explaining the bug.

## Smoke verification

- Pre-workaround: `Hunk_FreeTempMemory: bad magic` at first UI shader registration.
- Post-workaround: boot completes `--- Common Initialization Complete ---` → `Opening IP6 socket: [::]:27960` → `Opening IP socket: 0.0.0.0:27960` → clean `Client Shutdown (Client quit)` via `+quit`.

Logs archived: `docs/vulkan-phase2/2026-06-12-m5-10-hunk-{instrumented,banks,trace,workaround,readback,bigger}.log`.

## See also

- [[2026-06-12-m5-refexport-translator-landed]] — M5 closure that unblocked boot to reach this crash site.
- Memory: [[project-m5-10-hunk-free-temp-memory-landmine]] (now CLOSED).
- `code/renderervk/tr_image.c:633` — alloc with pre-clamp scaled (renderer bug)
- `code/renderervk/tr_image.c:720` — memcpy with post-clamp scaled (renderer bug, other half)
- `code/client/cl_refvulkan.c::vk_Hunk_AllocateTempMemory` — workaround landing place
