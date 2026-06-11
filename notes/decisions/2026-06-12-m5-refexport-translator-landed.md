# M5 refexport_t translator landed (2026-06-12)

Closes M5 milestone. Mirror of M3.5's [[m3.5-vtable-adapter-proper]] — same engineering pattern (vtable translator + static struct + per-slot wrappers), opposite direction. Engine→renderer was M3.5; renderer→engine is M5.

## Where we ended

8 commits on `macos-arm64-vulkan` (`2d27b17` → `2872b0d`). 36/36 SMALL `refexport_t` slots populated; engine `re` global now sees the correct function in every slot regardless of BIG/SMALL layout drift.

Smoke verification: engine boots through R_Init → Vulkan device enum on Apple M1 → shader parsing → finished R_Init → Sound init → start of UI vm load. Then crashes at `Hunk_FreeTempMemory: bad magic` — a separate landmine, not refexport related. See [[project-m5-10-hunk-free-temp-memory-landmine]].

## Decision — two-TU split

**Decision.** Implement the translator across two translation units:
- `code/client/cl_refvulkan.c` — sees BIG (`renderercommon/tr_public.h`), houses BIG-side thunks + Group A slot accessors. Already existed from M3.5 (held the engine→renderer translator); extended.
- `code/client/cl_refvulkan_export.c` — NEW. Sees SMALL (`renderer/tr_public.h`), houses SMALL-shaped wrappers + Group C tripwire stubs + the `CL_BuildVulkanRefExport` builder.

The two TUs communicate via `void *` and link-time symbols (`vk_re_get_*`, `vk_re_thunk_*`).

**Why.** SMALL and BIG `tr_public.h` both define `typedef struct { ... } refexport_t` and both transitively include `tr_types.h` which is itself differently-defined in the two trees. Both headers also share the `__TR_TYPES_H` include guard. **They cannot coexist in a single translation unit** — first include wins, second is silently elided, struct layouts/sizes silently disagree at the call site.

The single-TU alternative (use BIG everywhere, do struct-shape translation at slot boundaries) would have required either (a) duplicating SMALL's `refexport_t` layout under a renamed typedef inside the same TU, or (b) raw byte-offset memory access to assemble a SMALL refexport without naming it. Both are uglier than the link-time bridge.

**Trade-off.** Two files instead of one; every Group B slot needs a thunk decl in `cl_refvulkan.h` so both TUs can see it. The visible surface area is ~12 thunk decls — small enough that the discoverability cost is dominated by the existing M3.5 setup. The `void *` returns from Group A accessors lose compiler typechecking — SMALL-side casts are explicit and clustered in `CL_BuildVulkanRefExport`, which makes them grep-locatable.

**Revisit if.** If SMALL `tr_public.h` ever gets demoted to a thin shim that just forwards to BIG (M6+ direction), the two-TU split becomes redundant and the export TU collapses into the engine-side translator.

## Slot taxonomy

| Group | Count | Definition | Mechanism |
|---|---|---|---|
| **A.** Identity | 21 | name + signature match SMALL↔BIG | `VK_RE_GET(name)` macro in cl_refvulkan.c emits accessor returning `(void *)vk_re_big->name`; export TU casts to SMALL slot type |
| **B.** Sig mismatch | 7 | name matches, signature differs | thunk in cl_refvulkan.c (takes scalar args, calls BIG); wrapper in cl_refvulkan_export.c (matches SMALL slot sig, calls thunk) |
| **C.** RTCW-only | 8 | in SMALL, NOT in BIG | tripwire stub in cl_refvulkan_export.c — prints once on first call (`[M5/C] re.SLOTNAME called`), no-ops thereafter |
| **D.** Q3e-only | 10 | in BIG, NOT in SMALL | dropped — engine has no slot to assign to |

## Group B details — the seven signature wrappers

Worth keeping in case any of them needs revisiting:

| Slot | SMALL → BIG | Wrapper strategy |
|---|---|---|
| `Shutdown` | `(qboolean)` → `(refShutdownCode_t)` | `destroyWindow ? 2 /*REF_DESTROY_WINDOW*/ : 0 /*REF_KEEP_CONTEXT*/`. Hardcoded enum values; SMALL TU never names the BIG enum |
| `AddRefEntityToScene` | 1 arg → 2 args (`+intShaderTime`) | pass `qfalse` — Q3e's "use ent->shaderTime as-is" default |
| `AddPolyToScene` | 3 args → 4 args (`+num`) | pass `1` — engine never split-loops here |
| `AddLightToScene` | 6 args → 5 args (drop `int overdraw`) | drop arg; RTCW-only dynamic-light visibility hint not modeled by Q3e |
| `LerpTag` | `(tag, refent, tagName, startIndex)` → `(tag, hModel, startFrame, endFrame, frac, tagName)` | read `hModel/oldframe/frame/backlerp` from `refEntity_t`; `frac = 1.0f - backlerp`; drop `startIndex` (RTCW multi-tag-name iter extension, RTCW SP cgame doesn't rely on it) |
| `DrawStretchRaw` | `(const byte *data, ...)` → `(byte *data, ...)` | cast away const in thunk — renderer reads only |
| `UploadCinematic` | same const-qualifier diff as DrawStretchRaw | same cast-away pattern. **Initially misclassified as Group A; caught by Task 0 pre-flight verification.** Future ABI work: const-qualifier diffs on pointer args are sig mismatches, not identity, even if logically the renderer doesn't write through |

## LerpTag struct-field read pattern

The most intricate Group B slot. Reads four fields out of `refEntity_t` to translate one signature to the other. Field offsets need stability between SMALL and BIG `tr_types.h`. Verified in this session that `hModel` (qhandle_t), `frame`/`oldframe` (int), `backlerp` (float) all live in the early-fields region of `refEntity_t` in both headers — they're load-bearing engine fields the struct has carried since iortcw 2014 era.

`backlerp` semantic, from SMALL `tr_types.h:151` comment: `0.0 = current, 1.0 = old`. So BIG's `frac` (engine semantic: how far toward `endFrame`) = `1.0f - backlerp`. **If gameplay shows tag-anim glitches, this formula is the suspect.**

## Group C strategy — tripwire over implementation

RTCW-only slots have no BIG renderer counterpart. Options:
- (a) Implement each from scratch on top of BIG primitives (expensive, may not have equivalents)
- (b) Empty no-op stubs (silent, can't tell which fire)
- (c) Tripwire stubs — print-once-per-slot, no-op after (this milestone's choice)

Three slots got opportunistic fallback bodies even at tripwire stage:
- `DrawStretchPicGradient` → routes to `DrawStretchPic` (loses gradient effect, pic still draws — HUD slightly off but not broken)
- `AddPolysToScene` → marked TODO to loop over `numPolys` calling `AddPolyToScene` (deferred until smoke confirms it fires)
- `AddCoronaToScene` → marked TODO to approximate as small dynamic light (deferred)

The tripwire-first approach was deliberate: M5 budget says we want to LEARN which RTCW-specifics matter for boot/menu before spending time routing them, not assume all eight need implementations. The smoke log after the Hunk_FreeTempMemory crash is resolved will tell us which of the 8 actually fire.

## Pre-flight Task 0 — a worthwhile checkpoint

The plan led with a pre-flight task: cross-check Group A slot signatures across SMALL and BIG headers before implementation. It found one misclassification (`UploadCinematic` const-qualifier mismatch belonged in Group B, not A). 5 minutes of verification, one plan edit, zero downstream rework. The plan was right to budget for it.

**Pattern for similar ABI-translation work:** schedule a pre-flight verification task even when the dep-map looks settled. Cost is negligible, catch rate is positive.

## Files touched

```
code/client/cl_refvulkan.h       +99 (Group A accessors, Group B thunk decls)
code/client/cl_refvulkan.c       +88 (BIG-pointer cache, VK_RE_GET macro x21, Group B thunks)
code/client/cl_refvulkan_export.c NEW, ~280 lines (SMALL refexport builder, Group B+C wrappers, Group C tripwires)
code/client/cl_main.c             +10 (Vulkan-path wire-in between GetRefAPI return and re=*ret)
Makefile                          +1 (cl_refvulkan_export.o in client object list)
docs/superpowers/plans/2026-06-12-m5-refexport-translator.md  NEW (~950 lines)
docs/vulkan-phase2/2026-06-12-iter10-refexport-divergence.md  pre-existing (M4 closing triage)
docs/vulkan-phase2/2026-06-12-m5-baseline-stub-refexport.log  smoke proof after Task 1
docs/vulkan-phase2/2026-06-12-m5-postwire-smoke.log           smoke proof after Task 6 — Hunk crash captured
```

## What this unblocks

- All boot-time `re.X` calls now dispatch to the correct renderer function. No more slot-offset misalignment between SMALL caller and BIG callee.
- UI vm load can begin (next phase that fires after Sound init). It hits Hunk_FreeTempMemory bad-magic immediately — see [[project-m5-10-hunk-free-temp-memory-landmine]].
- Group C tripwire log (when smoke advances past the Hunk landmine) tells us which RTCW-only refexport slots gameplay actually requires. Drives prioritization of follow-up routing work.

## See also

- `notes/decisions/2026-06-09-m3.5-vtable-adapter-proper.md` — sibling translator, engine→renderer direction. Same pattern.
- `notes/decisions/2026-06-10-m4-window-ownership-model.md` — γ' migration that closed M4, made window init engine-side, unblocked M5.
- `docs/vulkan-phase2/2026-06-12-iter10-refexport-divergence.md` — pre-M5 triage that established the design space.
- `docs/superpowers/plans/2026-06-12-m5-refexport-translator.md` — the executed plan, includes the slot taxonomy + per-task bite-sized steps.
- Memory: [[project-m5-refexport-divergence]] (now CLOSED), [[project-m5-10-hunk-free-temp-memory-landmine]] (new frontier), [[feedback-const-qualifier-not-identity]] (pre-flight pattern).
