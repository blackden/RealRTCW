# RealRTCW Vulkan — Program Roadmap

Long-arc program for moving RealRTCW (macOS arm64 port) from the legacy
RTCW OpenGL renderer onto a Vulkan renderer (vendored from Quake3e).

Time is not a constraint; quality and feature parity are. OpenGL fallback
stays alive throughout — Vulkan is added alongside, not as a replacement,
until visual parity is reached. After parity, modernization adds features
that the original 2003-era renderer could not deliver.

Status legend: ✅ done · ▶ in progress · ⧗ pending · ⊘ future / optional

---

## Phase 2 — Vulkan visual parity (with bonus enhancements)

Goal: RealRTCW campaign plays on Vulkan with all original RTCW visual
features intact, and a baseline set of "free" modern improvements that
the new renderer's infrastructure gives for nothing.

```
M1   ABI inventory + sanity                                   ✅
M2   Shim layer + vtable adapter + engine glue                ✅
M3   Link clean, both renderer DLLs build                     ✅ (link only)
M3.5 ABI carry-over: refImport_t vocabulary reconciliation    ▶
M4   First run + Vulkan validation triage (boot to menu)      ⧗
M5.0 Base in-game rendering: level loads, geometry visible    ⧗
M5.1 Corona (light flares around bright sources)              ⧗
M5.2 Volumetric fog (RTCW-style room fog)                     ⧗
M5.3 Smart skins / LOD model swap                             ⧗
M5.4 ZombieFX (blood/hit effects)                             ⧗
M5.5 Remaining RTCW-specific render features                  ⧗
M6   Visual parity check: OpenGL ↔ Vulkan frame-by-frame      ⧗
M6+  BONUS — Wave 1 enhancements (see below)                  ⧗
done Vulkan plays campaign with parity + Wave 1 polish        ⧗
```

### M6+ Bonus — Wave 1 enhancements (free or near-free)

These exist already in the Q3e Vulkan renderer infrastructure (or come
naturally from modern Vulkan defaults). Wiring them up adds cvars and
defaults, not new feature code:

| Enhancement | What it does | Source |
|---|---|---|
| MSAA 4×/8× | Smooths jaggies on geometry edges | Vulkan native, cheap on MoltenVK |
| Anisotropic 16× | Floor/wall textures stay sharp at oblique angles | Vulkan native |
| Trilinear filtering | Less texture shimmer in motion | Vulkan default |
| Bloom | Soft glow around bright pixels (lamps, sun) | Q3e renderer's `FinishBloom` path already exists |
| sRGB / proper gamma | Colors more accurate, deeper contrast | Vulkan-native color space handling |
| HD textures at native resolution | Existing `*_hd.pk3` paks load without GL2.1 limits | Vulkan handles larger textures natively |
| Uncapped FPS / VRR | Smoother on 120Hz monitors | Vulkan present modes |

These are part of Phase 2's "done" definition. Vulkan must look at least
as good as OpenGL **and** add these freebies.

---

## Phase 3 — Modernization

Features that take real implementation work but stay within reasonable
scope. Each is a separate milestone with its own design + plan.

```
P3.A  SSAO — screen-space ambient occlusion              ⊘
P3.B  Volumetric lighting / god rays                     ⊘
P3.C  Better lightmaps (higher-resolution re-bake)       ⊘
P3.D  Dynamic shadows (replace blob shadows)             ⊘
P3.E  Particle system upgrade                            ⊘
```

Entry condition: Phase 2 fully closed (parity + Wave 1 stable in actual
play sessions, not just smoke tests).

Each Phase 3 item is independent — can pick any order, skip any item.

---

## Phase 4 — Premium

Ambitious features that change the visual character meaningfully or
require asset-side work.

```
P4.A  HDR pipeline + tone mapping                        ⊘
P4.B  FSR upscaling (1080p → 4K)                         ⊘
P4.C  PBR materials (requires arts pipeline)             ⊘
P4.D  Real-time GI (Lumen-style)                         ⊘
```

Entry condition: Phase 3 closed, and active decision to invest in this
direction. Phase 4 may never happen, and that's fine — Phase 2 alone is
already a worthwhile result.

---

## Constraints

- **OpenGL fallback never dies.** All work preserves it as the dev
  fallback / reference renderer through the entire program.
- **No RTCW feature gets dropped.** If Vulkan can't reproduce a visual
  effect, the answer is "port the effect", not "drop the effect".
- **Vendor convention.** `code/renderervk/` and `code/renderercommon/`
  are vendored Quake3e; new code lives in `realrtcw_*` files (see
  `notes/decisions/2026-06-08-vendor-prefix-convention.md`).
- **Push gating.** Nothing pushed to GitHub without explicit OK.

## Why this shape

- **Parity before polish.** SSAO on a black screen is just black. Wave 1
  enhancements piggyback on the Q3e renderer's existing infrastructure
  so they cost nothing extra — but Phase 3+ features need a working
  Vulkan base under them.
- **Phase boundaries are honest stopping points.** Phase 2 done = a real
  shipping result. Stopping there is a complete project. Phase 3+ is
  optional improvement, not unfinished work.

## Related

- `notes/decisions/2026-06-09-m3-renderer-side-cvar-bridge.md` — M3
  closure decision (cvar bridge between engine and Vulkan renderer)
- `notes/reference/engine-map.md` — file:line pointers for navigation
- `docs/superpowers/plans/2026-06-07-vulkan-phase2-playable-campaign.md`
  — original Phase 2 plan with task-level breakdown
- `docs/superpowers/plans/2026-06-09-vulkan-m4-first-run-triage.md` —
  M4 (validation triage) plan, currently blocked on M3.5
