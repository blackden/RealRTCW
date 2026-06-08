# External reference repos — locally cloned

Локальная инфра для работы над RealRTCW Vulkan port. Все клоны shallow `--depth=50`. Обновлять `git fetch --depth=50 && git reset --hard origin/main` периодически.

## Где что лежит

| Repo | Путь | Что внутри | Когда использовать |
|---|---|---|---|
| **Quake3e** | `~/fedorov_tech/refs/Quake3e` | Source вендоренного renderervk + renderercommon. Upstream `ec-/Quake3e` | Сверить какой именно код мы скопировали; найти оригинальный typedef / inline function |
| **iortcw** | `~/fedorov_tech/refs/iortcw` | Прямой предок RealRTCW (SP + MP, ~2014 vintage) | Найти RealRTCW-style функцию которой нет в Quake3e (типа `R_GetModeInfo`) — почти всегда уже есть в iortcw/SP |
| **ioq3** | `~/fedorov_tech/refs/ioq3` | ioquake3 — родитель iortcw и Quake3e | Найти «чистый» Q3 baseline без RTCW специфики |
| **vkQuake3** | `~/fedorov_tech/refs/vkQuake3` | Другой Vulkan-Q3 порт (suijingfeng/vkQuake3) | Альтернативное решение тех же проблем что мы ловим — особенно для shader translation, swapchain, MoltenVK quirks |
| **MoltenVK** | `~/fedorov_tech/refs/MoltenVK` | Сам MoltenVK + докладов | Понять что **именно** не работает на Metal-бэкенде и почему |
| **Vulkan-Docs** | `~/fedorov_tech/refs/Vulkan-Docs` | Спецификация Vulkan + VUID database | Расшифровать VUID-* в validation log; разобраться какая фича сейчас обязательна, а какая опциональна |

## Полезные паттерны grep'а

### «Где определена функция X в Q3-форках»

```bash
grep -rn "^[a-zA-Z _*]\+ R_GetModeInfo\s*(" ~/fedorov_tech/refs/{Quake3e,ioq3,iortcw,vkQuake3}/
```

Возвращает только **определения** (не вызовы). Filter `^[a-zA-Z _*]\+ funcname\s*(` — типичный C-style сигнатуры.

### «Сравнить как форк X решает проблему Y»

```bash
for repo in Quake3e ioq3 iortcw vkQuake3; do
  echo "=== $repo ==="
  grep -rn "swap_chain\|VkSwapchainKHR" ~/fedorov_tech/refs/$repo/code/renderervk/ 2>/dev/null | head -3
done
```

### «MoltenVK ограничения для конкретной фичи»

```bash
grep -n "geometry shader\|sparse residency\|compute pipeline" \
  ~/fedorov_tech/refs/MoltenVK/Docs/MoltenVK_Runtime_UserGuide.md
```

MoltenVK Docs:
- `MoltenVK_Runtime_UserGuide.md` — capability matrix, known limitations
- `MoltenVK_Configuration_Parameters.md` — env vars и runtime tuning (`MVK_CONFIG_*`)
- `Whats_New.md` — недавние изменения

### «Расшифровать VUID-*»

VUID коды в Vulkan-Docs живут под `chapters/`:

```bash
grep -rn "VUID-vkCmdBindPipeline-pipeline-02785" ~/fedorov_tech/refs/Vulkan-Docs/chapters/ | head -3
```

VUID получится не во всех случаях — часть генерится из `.adoc` parsing'а. Если grep'ом не найдено, fallback на web:
```
https://registry.khronos.org/vulkan/specs/latest/man/html/vkspec.html#<VUID>
```

## Sourcegraph (cross-fork search в облаке)

CLI `src` установлен в `/opt/homebrew/bin/src` (v7.4.0-rc.0). Endpoint default — `sourcegraph.com` (public free tier).

### Базовые запросы

```bash
# Найти все Q3-derived проекты определяющие R_GetModeInfo
SRC_ENDPOINT=https://sourcegraph.com src search \
  'context:global file:tr_init\.(c|cpp)$ qboolean R_GetModeInfo'

# Найти как RealRTCW делает Cvar_Get в game/cgame/ui модулях
src search 'repo:wolfetplayer/RealRTCW file:cg_.*\.c trap_Cvar_Get'

# JSON для machine-parsing
src search -json 'context:global ...'
```

### Когда Sourcegraph vs локальный grep

- **Sourcegraph**: «как ЭТА проблема решена в 10+ форках сразу», «есть ли хоть один публичный fork с фиксом ABC». Один запрос вместо 10 git clone'ов.
- **Локальный grep**: уже знаешь репо, нужна скорость, или работаешь оффлайн. Quake3e+iortcw+ioq3+vkQuake3 покрывают 95% «соседних форков».

### Полезные context: фильтры

- `context:global` — все публичные репы (~5M+ репозиториев на sourcegraph.com)
- `repo:^github\.com/(Quake3e|ec-)/...$` — конкретные org/users
- `lang:C` — фильтр по языку
- `file:tr_(init|local)\.(c|h)$` — regex по пути файла

## Обновление локальных клонов

Скрипт-helper (запускать раз в неделю-месяц):

```bash
for r in ~/fedorov_tech/refs/*/; do
  echo "=== $(basename "$r") ==="
  git -C "$r" fetch --depth=50 --no-tags 2>&1 | tail -1
  git -C "$r" reset --hard origin/HEAD 2>/dev/null || git -C "$r" reset --hard origin/master 2>/dev/null || git -C "$r" reset --hard origin/main 2>/dev/null
done
```

(Каждый репо имеет свою default-ветку — main/master/develop, поэтому каскад reset.)

## Memory + Skills которые знают про эти клоны

- Memory: `project-local-refs` в `~/.claude/projects/.../memory/` (TODO: создать)
- Skill: `q3-renderer-abi-diff` обновлён — prefer local clones для diff'а headers (см. skill body)
- Skill: `q3-fork-detective` agent — обновлён для local-first lookup
- Lumen: не индексирует `~/fedorov_tech/refs/` автоматически. Если нужен semantic search по чужому форку — запустить `lumen reindex` указав конкретный repo path.

## Связано

- `notes/reference/engine-map.md` — RealRTCW-side hot pointers
- `notes/decisions/2026-06-08-*` — архитектурные решения Phase 2
- `docs/superpowers/plans/2026-06-07-vulkan-phase2-playable-campaign.md` — план эпика
