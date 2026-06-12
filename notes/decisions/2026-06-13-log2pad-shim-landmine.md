# M7.5 step 1: log2pad shim landmine — Vulkan UI blur root cause

**Date:** 2026-06-13
**Branch:** macos-arm64-vulkan
**Touch:** `code/renderervk/realrtcw_shims.h`

## Symptom

After M6/M7 landed, Vulkan main menu rendered with severe blur — UI
buttons/text were extreme low-mip blobs, JPG cover art visible but dim.
OpenGL baseline (after M7.5 step 0 unblocked it) rendered crisply. Log line
in renderervk init reported `VK_MAX_TEXTURE_SIZE: 11` (vs OpenGL's
`GL_MAX_TEXTURE_SIZE: 16384`) — the 11 was the smoking gun.

## Root cause

`realrtcw_log2pad()` in `code/renderervk/realrtcw_shims.h` is a RealRTCW shim
for Quake3e's `log2pad()` (which exists upstream in `qcommon.h` but not in
RealRTCW's). The shim was written during M2 vendoring with **wrong
semantics**: it returned the log2 (exponent) instead of the rounded value
(power of two).

```c
// WRONG — earlier shim, returned exponent
static inline unsigned int realrtcw_log2pad(unsigned int v, int roundup) {
    unsigned int r = 0;
    if (roundup && v && (v & (v - 1))) v <<= 1;
    while ((v >>= 1) != 0) r++;
    return r;
}
```

Quake3e upstream (`Quake3e/code/qcommon/qcommon.h:1003`) returns the value:

```c
static ID_INLINE unsigned int log2pad(unsigned int v, int roundup) {
    unsigned int x = 1;
    while (x < v) x <<= 1;
    if (roundup == 0 && x > v) x >>= 1;
    return x;
}
```

So `log2pad(2048, 0)` upstream returns `2048`; our shim returned `11`.

Affected call sites (all silently miscompiled, none crashed):

| site | usage | shim returned | should have returned |
|---|---|---|---|
| `vk.c:4051` | `glConfig.maxTextureSize` from `sqrt(IMAGE_CHUNK_SIZE/4)` | `11` | `2048` |
| `vk.c:3985` | `vkSamples` (multisample) | `log2(N)` | `N` |
| `vk.c:6851`, `6868` | `vk.geometry_buffer_size_new` | tiny | size in bytes |

The `glConfig.maxTextureSize = 11` then propagated to `tr_image.c:627-631`
where it acted as the "clamp to current upper GL limit" — every texture got
downsampled to ≤11 pixels per side. Hence the blur.

The bug was **latent since M2** because M3/M3.5/M4/M5/M6 work hadn't yet
reached a state where the menu actually rendered to screen. M6 first-frame
and M7 JPG decode together made the menu visible, exposing the rendering
quality regression.

## Fix

Replace the shim body verbatim with Quake3e's `log2pad()` implementation.
No vendor edits — `realrtcw_shims.h` is RealRTCW-authored translator-layer
code per [[feedback-vendor-prefix-convention]].

## Verification

After fix + rebuild:
- Log line: `VK_MAX_TEXTURE_SIZE: 2048` (was `11`).
- Menu screenshot at `screenshots/m75-vulkan-menu-fixed.png` shows full
  texture sharpness — visual parity with OpenGL baseline.
- The `2048` cap is `MIN(maxImageDimension2D, log2pad(sqrt(32 MB / 4), 0))
  = MIN(16384, 2048) = 2048`. Coincidentally equal to the
  `MAX_TEXTURE_SIZE` hard cap in `code/renderervk/tr_local.h:38` — the
  second cap doesn't bite. RealRTCW PAK textures don't exceed 2048×2048,
  so raising the cap further is unnecessary at this milestone.

## Why this lived under the radar

- Other M2-era shims (`Q_stradd`, `Q_atof`, etc.) all returned values, so
  no pattern raised suspicion.
- The shim's `roundup` parameter was honored, just the return shape was
  wrong — code at call sites compiled clean (both versions return
  `unsigned int`).
- `log2pad` name itself is ambiguous: "log2 padded to power of 2" could
  read either way. Lesson: shim docstrings must give a worked example,
  not just describe semantics.

## Cross-links

- `code/renderervk/realrtcw_shims.h` line 182 — the comment now includes
  a worked example to prevent the same misread.
- `~/fedorov_tech/refs/Quake3e/code/qcommon/qcommon.h:1003` — upstream
  reference body.
- [[project-m6-types-unification]] — M6 close that surfaced the bug.
- [[project-m7-jpg-loader-closed]] — M7 close that made the menu visible.
- [[feedback-vendor-prefix-convention]] — shim ownership rule.
