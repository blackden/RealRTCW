# Vulkan M4 — First Run + Validation Triage Loop

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Boot RealRTCW under the Vulkan renderer to the main menu with zero severity-ERROR validation messages in the KHRONOS validation layer log.

**Architecture:** Reuse the `vk-validation-triage` skill's capture + grouping scripts (`vk-capture.sh`, `vk-validation-group.sh`, `vk-triage.cfg`) by copying them into `scripts/mac/` on the `macos-arm64-vulkan` branch. Cherry-pick `playtest.sh` from the `macos-arm64` branch and extend it with a `--vulkan` mode that sets `cl_renderer=vulkan`. Capture log, group by VUID, classify against the triage config, dispatch the `vulkan-validation-analyst` agent for clusters → apply one fix at a time (engine-glue first, shim second, vendor-edit last) → re-capture until clean.

**Tech Stack:** macOS arm64, MoltenVK 1.4.350.0, Vulkan Validation Layers from VulkanSDK 1.4.350.0 (`~/VulkanSDK/1.4.350.0/macOS`), MoltenVK ICD from Homebrew (`/opt/homebrew/share/vulkan/icd.d/MoltenVK_icd.json`), RealRTCW engine with `BUILD_RENDERER_VULKAN=1 USE_RENDERER_DLOPEN=1` per `project_build_flags_macos`.

---

## File Structure

| File | Responsibility |
|---|---|
| `scripts/mac/vk-capture.sh` | Sets `VK_ICD_FILENAMES` + `VK_LAYER_PATH` + `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` + `MVK_DEBUG=1`, then delegates to `playtest.sh` with `--vulkan` and tees stderr to `/tmp/vk-validation-<ts>.log`. |
| `scripts/mac/vk-validation-group.sh` | Reads a validation log, groups by `VUID-*` / `UNASSIGNED-*` token, looks up severity in `vk-triage.cfg`, emits `<log>.grouped.md` markdown report ordered by severity. |
| `scripts/mac/vk-triage.cfg` | Severity rules (fatal/high/medium/low/ignore) for VUID globs; first-match-wins. |
| `scripts/mac/playtest.sh` | Existing Phase-1 ASAN launcher; extended with `--vulkan` flag (exports `+set cl_renderer vulkan`) and `--auto` flag (runs autotest mission used in Phase 1 UBSAN triage). |
| `docs/vulkan-phase2/vk-triage-<date>-iterN.md` | One report per triage iteration (capture + grouped output + analyst findings). |
| `docs/vulkan-phase2/m4-close.md` | Final M4 summary with commits, before/after counts, residual deferred issues. |

---

## Task 1: Cherry-pick playtest.sh + extend with --vulkan / --auto

**Files:**
- Create: `scripts/mac/playtest.sh` (from `macos-arm64` branch, modified)
- Verify: `/Users/ragnar/fedorov_tech/RealRTCW-vulkan-wt/build/release-darwin-arm64-nosteam/RealRTCW.arm64` exists

- [ ] **Step 1: Bring playtest.sh from macos-arm64 branch**

```bash
cd ~/fedorov_tech/RealRTCW-vulkan-wt
mkdir -p scripts/mac
git show macos-arm64:scripts/mac/playtest.sh > scripts/mac/playtest.sh
chmod +x scripts/mac/playtest.sh
git diff --stat scripts/mac/playtest.sh
```

Expected: file appears in working tree.

- [ ] **Step 2: Add `--vulkan` and `--auto` flag parsing to playtest.sh**

Modify the `set -uo pipefail` line block. Replace the section between `set -uo pipefail` (line 21) and the `REPO_ROOT=...` line with the following:

```bash
set -uo pipefail

VULKAN_MODE=0
AUTO_MODE=0
EXTRA_ARGS=()

while [ $# -gt 0 ]; do
  case "$1" in
    --vulkan) VULKAN_MODE=1; shift;;
    --auto)   AUTO_MODE=1;   shift;;
    --)       shift; EXTRA_ARGS+=("$@"); break;;
    *)        EXTRA_ARGS+=("$1"); shift;;
  esac
done

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
```

Then locate the `"$BIN" "$@" < /dev/null` line and replace it with:

```bash
RENDERER_ARGS=()
if [ "$VULKAN_MODE" = "1" ]; then
  RENDERER_ARGS+=("+set" "cl_renderer" "vulkan")
  echo "Renderer:      Vulkan (cl_renderer=vulkan)"
else
  echo "Renderer:      OpenGL (cl_renderer=opengl1, default)"
fi

AUTO_ARGS=()
if [ "$AUTO_MODE" = "1" ]; then
  # Boot straight into main menu then quit after 10 s (so we have stable
  # capture window for validation triage). +wait spins frames.
  AUTO_ARGS+=("+wait" "600" "+quit")
  echo "Auto mode:     boot, 10s, quit"
fi

"$BIN" "${RENDERER_ARGS[@]+"${RENDERER_ARGS[@]}"}" "${AUTO_ARGS[@]+"${AUTO_ARGS[@]}"}" "${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}" < /dev/null
GAME_EXIT=$?
```

Also remove the trailing «ЧЕКЛИСТ» heredoc block (the «A1 — low-ammo warning…» section through end of script) — it's Phase-1-specific and no longer relevant.

- [ ] **Step 3: Smoke-test OpenGL path still works**

```bash
cd ~/fedorov_tech/RealRTCW-vulkan-wt
timeout 15 ./scripts/mac/playtest.sh --auto 2>&1 | tail -15
```

Expected: engine starts under OpenGL renderer (current default), reaches main menu, exits after `+quit`. If you see "Failed to load renderer" — abort and check the binary exists.

- [ ] **Step 4: Commit**

```bash
git add scripts/mac/playtest.sh
git commit -m "build(vulkan/m4): port playtest.sh from macos-arm64 with --vulkan/--auto

Phase-1 ASAN launcher reused as the M4 capture target. Adds --vulkan
(sets cl_renderer=vulkan) and --auto (boot + 10s + quit) modes so the
validation-triage wrapper can drive it headlessly. Phase-1 checklist
heredoc dropped — that workflow closed in #218."
```

---

## Task 2: Drop in vk-capture.sh + vk-validation-group.sh + vk-triage.cfg

**Files:**
- Create: `scripts/mac/vk-capture.sh` (copy from skill)
- Create: `scripts/mac/vk-validation-group.sh` (copy from skill, adjust CFG path)
- Create: `scripts/mac/vk-triage.cfg` (copy from skill verbatim)

- [ ] **Step 1: Copy capture wrapper from skill**

```bash
cd ~/fedorov_tech/RealRTCW-vulkan-wt
cp ~/.claude/skills/vk-validation-triage/scripts/vk-capture.sh scripts/mac/vk-capture.sh
chmod +x scripts/mac/vk-capture.sh
```

The skill's wrapper already discovers VULKAN_SDK/explicit_layer.d, falls back to /opt/homebrew, and delegates to `scripts/mac/playtest.sh`. After Task 1 the delegate is in place.

- [ ] **Step 2: Copy grouper and adjust triage-config path**

```bash
cp ~/.claude/skills/vk-validation-triage/scripts/vk-validation-group.sh scripts/mac/vk-validation-group.sh
chmod +x scripts/mac/vk-validation-group.sh
```

The skill's grouper computes `CFG_DIR="$(cd "$(dirname "$0")/.." && pwd)"` — that resolves to `scripts/` when invoked as `scripts/mac/vk-validation-group.sh`, so the config must live one dir up from the script. Move it:

Open `scripts/mac/vk-validation-group.sh` and change the line

```bash
TRIAGE_CFG="${CFG_DIR}/vk-triage.cfg"
```

to

```bash
TRIAGE_CFG="$(dirname "$0")/vk-triage.cfg"
```

so the cfg is sibling to the script (no fragile relative paths).

- [ ] **Step 3: Copy triage config**

```bash
cp ~/.claude/skills/vk-validation-triage/vk-triage.cfg scripts/mac/vk-triage.cfg
```

- [ ] **Step 4: Dry-run the grouper on a synthetic log to verify it loads cfg cleanly**

```bash
echo 'foo VUID-vkQueueSubmit-pSubmits-00075 bar' > /tmp/vk-synth.log
echo 'baz UNASSIGNED-CoreValidation-Shader-OutputNotConsumed qux' >> /tmp/vk-synth.log
scripts/mac/vk-validation-group.sh /tmp/vk-synth.log
```

Expected stdout: a markdown report with `## FATAL` containing `VUID-vkQueueSubmit-pSubmits-00075 × 1` and `## IGNORE` containing the UNASSIGNED line. If you get `ERROR: triage config not found` — the path adjustment in Step 2 is wrong.

- [ ] **Step 5: Set `VULKAN_SDK` so the capture wrapper picks the SDK layers**

This step is a `.envrc`-style nudge — record it in the M4 close doc, do NOT bake the value into a tracked file. For interactive use:

```bash
export VULKAN_SDK="$HOME/VulkanSDK/1.4.350.0/macOS"
```

Verify the discovery:

```bash
ls "$VULKAN_SDK/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json"
ls /opt/homebrew/share/vulkan/icd.d/MoltenVK_icd.json
```

Both must exist. If `VkLayer_khronos_validation.json` is missing, reinstall the SDK (you said it's at 1.4.350.0 already).

- [ ] **Step 6: Commit**

```bash
git add scripts/mac/vk-capture.sh scripts/mac/vk-validation-group.sh scripts/mac/vk-triage.cfg
git commit -m "build(vulkan/m4): vendor vk-capture + grouper + triage cfg from skill

Mirrors scripts/mac/ubsan-group.sh workflow but for KHRONOS validation
layer output. Wraps playtest.sh, sets VK_INSTANCE_LAYERS + VK_LAYER_PATH
(SDK or brew), MVK_DEBUG=1; produces /tmp/vk-validation-<ts>.log; group
script classifies by VUID-* using vk-triage.cfg first-match-wins rules.

Source: ~/.claude/skills/vk-validation-triage/"
```

---

## Task 3: First capture — baseline triage iteration 1

**Files:**
- Create: `docs/vulkan-phase2/vk-triage-2026-06-09-iter1.md`

- [ ] **Step 1: Make sure homepath has the paks**

```bash
ls -lh ~/Library/Application\ Support/RealRTCW/main/*.pk3 | head -5
```

Expected: at least `pak0.pk3`, `mp_pak*.pk3`, `realrtcw.pk3`. If the dir is empty — see `project_realrtcw_macos.md` § «Game data layout»; this is a Phase-1 prerequisite, not an M4 task.

- [ ] **Step 2: Take the capture (auto mode — boots, waits 10s, quits)**

```bash
cd ~/fedorov_tech/RealRTCW-vulkan-wt
export VULKAN_SDK="$HOME/VulkanSDK/1.4.350.0/macOS"
scripts/mac/vk-capture.sh --auto 2>&1 | tail -30
```

Expected one of these outcomes (record which):

a) Engine boots to main menu, exits cleanly at +quit → best case, validation log captures full menu init.
b) Engine fails to load Vulkan renderer DLL → `cl_renderer` cvar not picking up `renderer_sp_vulkan_arm64.dylib`. Inspect with `nm build/release-darwin-arm64-nosteam/renderer_sp_vulkan_arm64.dylib | grep GetRefAPI`.
c) Engine loads renderer but crashes before menu → expected; log still captures the early VUIDs which are the M4 work product.

Either way: the script prints `log: /tmp/vk-validation-<ts>.log`. Save that path.

- [ ] **Step 3: Group the log**

```bash
scripts/mac/vk-validation-group.sh /tmp/vk-validation-<ts>.log
```

(Replace `<ts>` with the actual timestamp from Step 2.) Expected: stdout shows markdown report; sibling file `/tmp/vk-validation-<ts>.log.grouped.md` written.

- [ ] **Step 4: Stage the grouped report into the repo**

```bash
mkdir -p docs/vulkan-phase2
cp /tmp/vk-validation-<ts>.log.grouped.md docs/vulkan-phase2/vk-triage-2026-06-09-iter1.md
# Prepend a one-line capture-context header so future-me knows what was running
{ echo "# Vulkan validation triage — iter 1 (2026-06-09)"
  echo ""
  echo "**Capture**: \`scripts/mac/vk-capture.sh --auto\` against \`build/release-darwin-arm64-nosteam/RealRTCW.arm64\`"
  echo "**Renderer DLL**: \`renderer_sp_vulkan_arm64.dylib\` (763 KB, M3 close tip \`1971fbf\`)"
  echo "**Outcome**: $(echo 'a/b/c — fill in')"
  echo ""
  cat docs/vulkan-phase2/vk-triage-2026-06-09-iter1.md
} > docs/vulkan-phase2/vk-triage-2026-06-09-iter1.md.tmp
mv docs/vulkan-phase2/vk-triage-2026-06-09-iter1.md.tmp docs/vulkan-phase2/vk-triage-2026-06-09-iter1.md
```

Edit the «Outcome» line by hand to record which of {a, b, c} actually happened.

- [ ] **Step 5: Commit baseline**

```bash
git add docs/vulkan-phase2/vk-triage-2026-06-09-iter1.md
git commit -m "docs(vulkan/m4): baseline validation triage iter 1

First Vulkan-DLL run under KHRONOS validation. Records VUID counts by
severity for triage loop entry point. No code changes — observation
only."
```

---

## Task 4: Triage analyst dispatch — get fix-plan for top clusters

**Files:**
- Read: `docs/vulkan-phase2/vk-triage-2026-06-09-iter1.md` (input to analyst)
- Create: `docs/vulkan-phase2/vk-analyst-iter1.md` (analyst output)

- [ ] **Step 1: Dispatch `vulkan-validation-analyst` agent**

Use the Agent tool:

```text
subagent_type: vulkan-validation-analyst
description: Triage Vulkan M4 iter 1 log
prompt: |
  Triage the validation log at /tmp/vk-validation-<ts>.log (grouped:
  docs/vulkan-phase2/vk-triage-2026-06-09-iter1.md). Project context:
  RealRTCW (Q3-family fork) running Quake3e's vendored renderervk
  unmodified, with realrtcw_vk_window_bridge.c supplying the renderer-
  side cvars (see notes/decisions/2026-06-09-m3-renderer-side-cvar-
  bridge.md). Goal: boot to main menu with zero severity-ERROR
  messages.

  For each FATAL and HIGH cluster:
  1. Fetch the Khronos VUID description (validusage.json at
     $VULKAN_SDK/share/vulkan/registry/validusage.json is offline-
     friendly; WebFetch the docs.vulkan.org URL only if SDK isn't
     present).
  2. Hypothesize root cause given the project context. Three priors:
     (i) shim layer divergence engine↔renderer (see notes/reference/
     engine-map.md «Renderer-ABI shim layer»), (ii) bridge defaults
     not seeded yet at the point the VUID fires, (iii) genuine
     upstream Quake3e bug — check ec-/Quake3e issues+commits for
     known fix.
  3. Recommend a fix and where to apply it. Order of preference:
     engine glue (code/client/*, code/sdl/*) → realrtcw_*.c shim →
     vendored renderervk (only with REALRTCW_ALLOW_VENDOR_EDIT=1,
     last resort).

  Output: docs/vulkan-phase2/vk-analyst-iter1.md, structured as
  severity-grouped sections, top 5 FATAL clusters first, then HIGH.
  Each entry: VUID, count, Khronos description, hypothesis, fix.
```

Expected: the analyst writes the markdown report. Read it.

- [ ] **Step 2: Sanity-check analyst output**

Open `docs/vulkan-phase2/vk-analyst-iter1.md`. For each proposed fix verify:
- The file:line cited actually exists in the repo (or in vendored code).
- The fix touches the layer the analyst claimed (engine vs shim vs vendor).
- The Khronos description quoted is consistent with the URL/section reference.

If anything is hand-waved («add appropriate handling»), re-dispatch the analyst with a sharper brief on that specific VUID. Do NOT apply fixes from speculative entries.

- [ ] **Step 3: Commit analyst report**

```bash
git add docs/vulkan-phase2/vk-analyst-iter1.md
git commit -m "docs(vulkan/m4): vulkan-validation-analyst report iter 1

Fix-plan for top FATAL+HIGH VUID clusters with Khronos spec refs,
root-cause hypotheses, and layer recommendations. Input to the iter 1
fix loop."
```

---

## Task 5: Fix loop — one cluster at a time, re-capture between

**Files** (vary per fix; common candidates):
- Modify: `code/client/cl_main.c` (refImport_t / GetRefAPI vtable)
- Modify: `code/sdl/sdl_glimp.c` (window/swapchain init)
- Modify: `code/renderervk/realrtcw_vk_window_bridge.c` (bridge globals timing)
- Modify (last resort, only with `REALRTCW_ALLOW_VENDOR_EDIT=1`): `code/renderervk/vk*.c`

- [ ] **Step 1: Pick the top FATAL cluster from analyst report**

«Top» = highest in `vk-analyst-iter1.md` order. Don't shop around; the analyst already ordered by impact.

- [ ] **Step 2: Apply the fix in the recommended layer**

Engine-glue or shim — straight Edit. For vendored code only:

```bash
REALRTCW_ALLOW_VENDOR_EDIT=1 <edit command>
```

Keep the change minimal — just what the analyst's fix recipe says.

- [ ] **Step 3: Rebuild Vulkan DLL only (don't touch OpenGL DLL)**

```bash
make ARCH=arm64 USE_RENDERER_DLOPEN=1 BUILD_RENDERER_VULKAN=1 \
     USE_OPENAL=1 USE_INTERNAL_LIBS=0 -j8 \
     build/release-darwin-arm64-nosteam/renderer_sp_vulkan_arm64.dylib
```

Expected: builds clean. If link fails — undo the fix, re-think.

- [ ] **Step 4: Re-capture**

```bash
scripts/mac/vk-capture.sh --auto 2>&1 | tail -5
scripts/mac/vk-validation-group.sh /tmp/vk-validation-<new-ts>.log
```

- [ ] **Step 5: Diff cluster counts (cleared vs survived vs new)**

```bash
# Quick before/after diff of FATAL section:
diff <(awk '/^## FATAL/,/^## HIGH/' docs/vulkan-phase2/vk-triage-2026-06-09-iter1.md) \
     <(awk '/^## FATAL/,/^## HIGH/' /tmp/vk-validation-<new-ts>.log.grouped.md)
```

Expected: the cluster you fixed is GONE from the new report. If it survived → fix didn't take; revert and re-think before piling on more changes.

- [ ] **Step 6: Commit the single fix with the cluster name**

```bash
git add <files>
git commit -m "fix(vulkan/m4): <one-line> — clears VUID-<id> ×N

<2-3 lines: what was wrong, what the analyst said, layer choice why>"
```

- [ ] **Step 7: Loop**

Go to Step 1 of this task using the NEW report. Each iteration N produces a new `docs/vulkan-phase2/vk-triage-2026-06-09-iterN.md` snapshot via the pattern in Task 3 Step 4. Exit when: `vk-capture.sh --auto` boots to main menu AND grouped report shows **zero entries under FATAL and HIGH** (MEDIUM/LOW/IGNORE are acceptable carry-over).

If a single iteration adds more clusters than it removes (the «whack-a-mole» case) — stop, re-dispatch the analyst with the new log as input and a brief that says «previous fixes introduced regressions; reconcile».

---

## Task 6: M4 close — write summary, push, demo

**Files:**
- Create: `docs/vulkan-phase2/m4-close.md`

- [ ] **Step 1: Write the M4 summary doc**

```markdown
# Vulkan M4 close — first run + validation triage

**Date:** 2026-06-09 (or close date)
**Branch tip:** <git rev-parse HEAD>
**Iterations:** N
**Final state:** main menu boots; FATAL=0, HIGH=0, MEDIUM=K (deferred), LOW=L, IGNORE=M.

## Iter-by-iter

| Iter | FATAL | HIGH | Top cluster cleared | Fix commit |
|------|-------|------|---------------------|------------|
| 1    | F1    | H1   | VUID-...            | sha        |
| 2    | F2    | H2   | VUID-...            | sha        |
| ...  |       |      |                     |            |
| N    | 0     | 0    | —                   | —          |

## Deferred (MEDIUM/LOW carry-over)

- VUID-... × N — reason: <brief>; revisit at M5/M6.

## Layer-choice retrospective

How many fixes landed in engine-glue vs shim vs vendor-edit? Did the
order-of-preference rule hold? Were there cases where shim was a
workaround for a real engine bug?

## Next: M5

Per `docs/superpowers/plans/2026-06-07-vulkan-phase2-playable-campaign.md`
Milestone 5 — load `escape1.bsp` and compare to OpenGL reference frame.
```

Fill in the table from the iter snapshots; pull commit shas from `git log`.

- [ ] **Step 2: Codify findings**

Invoke `codify-findings` skill (per `feedback_use_planning_skill` no-wait pattern). Specifically:

- Any new file:line landmines surfaced during triage → `notes/reference/engine-map.md` bullets.
- Any non-trivial layer-choice decisions (e.g. «we patched vendored code for VUID-X because Quake3e upstream is stale») → new `notes/decisions/2026-06-09-m4-<topic>.md`.
- Project state update → `~/.claude/projects/-Users-ragnar-fedorov-tech-RealRTCW-macOS/memory/project_realrtcw_macos.md` § «Phase 2 — Milestone 4 CLOSED».

- [ ] **Step 3: Commit summary + codified findings**

```bash
git add docs/vulkan-phase2/m4-close.md notes/
git commit -m "docs(vulkan/m4): close milestone — main menu boots clean

<bullet list: final FATAL=0/HIGH=0 counts, N iters, decisions codified>"
```

- [ ] **Step 4: Push (after explicit ragnar OK per `feedback_push_gating`)**

```bash
git log --oneline 1971fbf..HEAD  # show what's about to push
# Wait for explicit "push" from ragnar before:
# git push origin macos-arm64-vulkan
```

- [ ] **Step 5: Demo to ragnar**

Show: `scripts/mac/vk-capture.sh --auto` launches → main menu under Vulkan → exits → grouped report has FATAL=0/HIGH=0. Attach `docs/vulkan-phase2/m4-close.md`.

---

## Self-Review

**Spec coverage** vs Phase 2 plan Milestone 4 (Tasks 4.1–4.3):
- 4.1 (capture+group scripts) → covered by Tasks 1+2 of this plan.
- 4.2 (triage loop) → covered by Tasks 3+4+5.
- 4.3 (demo + push) → covered by Task 6.

**Placeholders:** none — every step has concrete commands; Task 5 is the iteration loop which is by-nature data-driven, but the exit criterion («FATAL=0 AND HIGH=0 AND boots to menu») is concrete.

**Type / path consistency:** all script paths agree across tasks (`scripts/mac/vk-*.sh` + `scripts/mac/vk-triage.cfg`). Iteration doc paths agree (`docs/vulkan-phase2/vk-triage-2026-06-09-iterN.md`). The capture wrapper's delegate path (`scripts/mac/playtest.sh`) matches what Task 1 creates.

**Known unknowns flagged in plan:**
- Task 3 Step 2 outcome a/b/c is recorded in the iter1 doc by the engineer — not pre-decided.
- Task 5 cluster count is unknowable until iter1 lands; loop exit is by stable criterion.
- Task 6 layer-choice retrospective ratio is fill-after-the-fact — that's appropriate for a retrospective.
