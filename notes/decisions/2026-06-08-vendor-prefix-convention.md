# `realrtcw_*` prefix — convention для RealRTCW-authored файлов в vendored dirs

**Date:** 2026-06-08
**Phase:** Vulkan Phase 2, Milestone 2 (Task 2.1+)
**Hook:** `.claude/hooks/block-vendored-renderervk.sh`

## Context

В RealRTCW есть две `vendored` директории — `code/renderervk/` и `code/renderercommon/` — которые скопированы из upstream Quake3e и должны оставаться byte-equivalent чтобы будущая ре-синхронизация была чистым diff'ом. Проектный hook (`.claude/hooks/block-vendored-renderervk.sh`) блокирует Edit/Write в этих путях с подсказкой «используй shim layer или engine-side fix».

При работе над M2 потребовалось создавать **RealRTCW-authored** support файлы (типа `realrtcw_shims.h`, `realrtcw_engine_glue.c`) **внутри** vendored dirs — потому что Makefile `-include` macro работает только на файлы в той же compile unit'ной директории, а shim header физически должен быть рядом с vendored .c файлами которые он шими́т.

Сразу видна mismatch: hook говорит «не трогай vendored dir», но мы туда кладём наши support files. Они НЕ vendored — они **RealRTCW-authored support layer**. Hook'у нужно их отличать.

## Decision

**Convention:** любой RealRTCW-authored файл который физически живёт в `code/renderervk/` или `code/renderercommon/` **обязан** иметь имя с префиксом `realrtcw_`.

Hook соответственно расширен: после проверки на vendored dir, перед стандартным BLOCKED-баннером, проверяется basename файла. Если начинается с `realrtcw_` — pass без escape hatch.

```bash
case "$(basename "$file_path")" in
  realrtcw_*) exit 0 ;;
esac
```

Этот change в hook'е требовал явной user-authorization (auto-mode classifier поймал self-modification permission widening). Получили ✓ от ragnar 2026-06-08.

Escape hatch `REALRTCW_ALLOW_VENDOR_EDIT=1` остаётся для случаев когда **нужно patch'ить actually-vendored файл** (например `code/renderercommon/tr_types.h` — это файл который RealRTCW maintainer уже модифицировал shim'ами при checkpoint 95177e7, но не доделал, и нам пришлось добавлять `#ifndef COLOR4UB_T_DEFINED` guard'ы для интеграции с canonical color4ub_t).

## Why

- **Семантически правильно.** Префикс — это явный маркер «это наш файл, не upstream». Удобно для grep'ов типа `grep -rL '^realrtcw_' code/renderervk/` — все upstream файлы.
- **Hook decision based on filename без нужды читать содержимое.** Простая проверка `case basename`.
- **Reduces friction для M2.x dispatches.** Без convention пришлось бы каждый раз ставить `REALRTCW_ALLOW_VENDOR_EDIT=1` в каждом subagent-prompt'е. С convention — нативно работает.
- **Reversible.** Если convention окажется проблемной — `git revert` hook patch'а возвращает строгий guard.

## Trade-off

- **Hook теперь имеет ДВА exit pathways: prefix-based pass + env-based pass.** Чуть больше surface для багов в логике hook'а. Mitigation: тесты `block-vendored-renderervk.sh.test.sh` (если есть) или smoke-проверка при следующем coding session.
- **Если кто-то создаст `realrtcw_typo_renamed_qag.c` в vendored dir с реальной upstream patch'ной правкой внутри — hook это пропустит.** То есть convention опирается на дисциплину именования. Mitigation: code review при merge'е (hook не заменяет review, он guards-against-accident).
- **`REALRTCW_ALLOW_VENDOR_EDIT=1` всё ещё нужна** для edit'а файлов типа `code/renderercommon/tr_types.h` которые RealRTCW maintainer уже частично перепатчил. То есть convention НЕ покрывает все RealRTCW-authored изменения в vendored dirs — только новые файлы.

## Revisit if

- **Hook начинает пропускать что-то очевидно вредное** (типа файлов с realrtcw_ префиксом которые реально содержат upstream Quake3e правку с переименованным заголовком) — нужно tighten check (например на содержимое первой строки или header guard).
- **Появляется third-party submodule** в одной из vendored dirs — convention не покрывает submodule'ные .c файлы, может потребоваться более complex pattern match.

## Связано

- `notes/reference/engine-map.md` секция «Renderer-ABI shim layer» → bullet «Vendor-guard hook + realrtcw_ prefix convention»
- `.claude/hooks/block-vendored-renderervk.sh` — собственно hook (in `.claude/` dir, не version-controlled by default но present локально)
- Памятная запись: [[feedback-vendor-prefix-convention]] в `~/.claude/projects/.../memory/`
- `docs/superpowers/plans/2026-06-07-vulkan-phase2-playable-campaign.md` Task 2.1 — где впервые применили convention
