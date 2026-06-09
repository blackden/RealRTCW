# M3 — renderer-side cvar bridge для sdl_glimp.c

**Date:** 2026-06-09
**Phase:** Vulkan Phase 2, Milestone 3 (Link Clean → Vulkan DLL Produced)
**Commits:** `f493440` (bridge), `bae52a5`+`dd3afb3` (license), `739fe25` (Q3VKOBJ wire), `368551c` (tr_subs.o drop), `d96cc87` (per-target CFLAGS), `9e2ca8b` (sdl_glimp hook)

## Context

При попытке слинковать `renderer_sp_vulkan_arm64.dylib` после M2 закрытия — 11 unresolved символов, все из `sdl_glimp.o`:

```
_R_GetModeInfo, _displayAspect, _haveClampToEdge,
_r_mode, _r_fullscreen, _r_noborder, _r_colorbits, _r_depthbits,
_r_stencilbits, _r_stereoEnabled, _r_swapInterval
```

Vendored Quake3e `renderervk/tr_init.c` НЕ определяет эти символы как renderer-side globals — Quake3e выкинул их в пользу `ri.Cvar_VariableString("r_mode")` lookup'ов через vtable (см. `renderervk/tr_init.c:1365-1374`). А RealRTCW делит **shared** `code/sdl/sdl_glimp.c` между обоими renderer DLL'ами (`renderer/sdl_glimp.o` для OpenGL, `rendv/sdl_glimp.o` для Vulkan) — и она написана под старый iortcw/RTCW ABI где эти cvars **есть** как globals и через них же передаётся `R_GetModeInfo`.

Симметричная ловушка к M2.4 (`refimport_t` divergence engine ↔ renderercommon), но теперь на другой стороне vtable — renderer ↔ sdl_glimp.

## Decision

**Bridge файл `code/renderervk/realrtcw_vk_window_bridge.{h,c}`** — RealRTCW-authored, hook-allowed prefix. Определяет 11 ожидаемых символов как tentative globals, латчит указатели через `ri.Cvar_Get` в `RealRTCW_VkBridgeInit(void)`, портирует `R_GetModeInfo` + `r_vidModes[]` verbatim из iortcw SP `tr_init.c:346-416`. Линкуется ТОЛЬКО в Q3VKOBJ. Init вызывается из `code/sdl/sdl_glimp.c:GLimp_Init` под guard'ом `#ifdef BUILD_RENDERER_VULKAN`.

Per-target Makefile append `$(B)/rendv/sdl_glimp.o: CFLAGS += -DBUILD_RENDERER_VULKAN` обеспечивает что guard работает ТОЛЬКО на rendv/ build path — `renderer/sdl_glimp.o` (OpenGL DLL) этого define не получает.

## Альтернативы рассмотренные

**A) Tentative-globals bridge (выбрано).** Минимальный engine-side touch (одна строка include + одна строка call в sdl_glimp.c). Bridge — самодостаточный TU в `code/renderervk/`. Симметричная парадигма с M2.4 (per-DLL conditional, не runtime adapter).

**B) Модифицировать `renderervk/tr_init.c` напрямую — добавить туда glob'ы + `R_GetModeInfo`.** Отвергнуто:
- Нарушает vendor-prefix convention (см. [[vendor-prefix-convention]]) — vendored файл редактируется только под `REALRTCW_ALLOW_VENDOR_EDIT=1` escape hatch
- Делает следующий re-vendor из Quake3e болезненным — diff будет шуметь
- Quake3e умышленно убрал эти globals из tr_init.c; навязывать их обратно — идти против upstream выбора

**C) Заменить shared `sdl_glimp.c` на Quake3e-style вариант, использующий `ri.Cvar_*` lookups.** Отвергнуто:
- Engine файл, не renderer-side — поломает OpenGL build path
- Делает поддержку двух renderer'ов из одного `sdl_glimp.c` ещё более рискованной
- Слишком большой scope для M3 (link clean), а не runtime correctness

## Why

- **Локализация ABI shim'а.** Bridge — один TU, легко аудитировать. Engine не видит Quake3e-side нюансов; vendored код не модифицирован.
- **Init вызывается ровно один раз, в правильное место.** `GLimp_Init` — первая renderer-side функция, вызываемая engine'ом перед любой работой sdl_glimp.c с этими cvars. До этого момента никакого dangling deref'а быть не может (проверено final code review'ером — нет static инициализаторов / callback'ов трогающих эти globals).
- **OpenGL build path strictly untouched.** Per-target CFLAGS guard + Q3ROBJ не включает bridge `.o`. `nm renderer_sp_opengl1_arm64.dylib | grep RealRTCW_VkBridgeInit` = 0 (проверено).
- **Cvar defaults 1:1 с engine renderer.** Verified против `code/renderer/tr_init.c:1241-1306`. `seta r_mode N` ведёт себя одинаково между OpenGL и Vulkan DLL.

## Trade-off

- **Bridge — это два источника правды на cvar.** При смене default'а в `code/renderer/tr_init.c` нужно одновременно править bridge'е. Не очень принципиально потому что defaults стабильны (defaults RTCW не менялись с 2010), но при крупном сweep'е engine cvars (e.g. M4/M5) надо помнить пройти грепом.
- **R_GetModeInfo portированная копия.** Если iortcw добавит новый custom-mode (mode == -3 для super-custom) — bridge надо обновлять вручную. Привязки к upstream нет.
- **Per-target CFLAGS — Makefile-specific.** Если кто-то будет конвертировать билд в CMake/Bazel, это правило надо переписать.

## Revisit if

- **Quake3e в каком-то будущем re-vendor добавит обратно renderer-side cvar globals.** Тогда bridge можно удалить и пойти прямым линком. Маловероятно (vtable lookup — их осознанный architecture choice).
- **Появится третий renderer DLL** (e.g. Metal native). Та же bridge-стратегия подходит — нужно лишь добавить третий per-target CFLAGS append + ещё одну версию bridge'а с правильным набором cvars/defaults.
- **Понадобится runtime switch OpenGL ↔ Vulkan без rebuild'а** — bridge придётся переписать, потому что guard'ы и conditional include'ы фиксируются на build time.

## Связано

- `notes/decisions/2026-06-08-m2.4-vtable-adapter-shortcut.md` — симметричное решение на engine↔renderer стороне vtable. M3 — его close, но с другой стороны.
- `notes/decisions/2026-06-08-vendor-prefix-convention.md` — bridge файл `realrtcw_vk_window_bridge.*` соблюдает префикс.
- `notes/reference/engine-map.md` — обновлён с двумя bullets:
  - tr_subs.o duplicate-symbol landmine (pre-M3 vendoring oversight, surfaced clean rebuild only)
  - target-specific CFLAGS pattern для Vulkan-only guard'ов
- code-craft принципы 1 (single source of truth — partial violation accepted, см. trade-off выше), 5 (layering — bridge как явно отделённый слой), 8 (structural fix vs workaround — выбран structural)

## Pre-M3 vendoring landmine (surfaced during M3)

Не часть M3 scope изначально, но всплыло на clean rebuild:

**Duplicate symbol `Com_Printf` / `Com_Error`** между:
- `code/renderervk/tr_init.c:254-275` (под `#ifdef USE_RENDERER_DLOPEN`)
- `code/renderer/tr_subs.c:26-48` (безусловно)

Оба `.o` были в `Q3VKOBJ` до M3. M2 закрытие НЕ ловило этот landmine потому что между попытками сборки .o файлы оставались stale — на clean rebuild оба перекомпиливаются и ld падает с duplicate. OpenGL DLL link path в порядке: engine `renderer/tr_init.c` НЕ определяет эти функции, только `tr_subs.c`.

**Fix:** убрать `tr_subs.o` из `Q3VKOBJ` (`Makefile:2174-2185`). Vendored renderervk сам обеспечивает `Com_Printf`/`Com_Error` функционально идентично (оба ri-wrap'ают через PRINT_ALL/error level).
