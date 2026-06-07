# color4ub_t — single source of truth в q_shared.h

**Date:** 2026-06-08
**Phase:** Vulkan Phase 2, Milestone 2 (Task 2.5)
**Commits:** 4e34ae5 (M2.5 sweep), surrounding M2.x history on `macos-arm64-vulkan`

## Context

При портировании Quake3e renderervk в RealRTCW обнаружилось три параллельных типа `color4ub_t`:

| Где | Что было | Кто хотел такой формат |
|---|---|---|
| `code/renderer/tr_local.h:628` | `typedef byte color4ub_t[4];;` (массив, двойная `;;`) | Engine OpenGL renderer |
| `code/renderercommon/tr_types.h:35` | `typedef byte color4ub_t[4];` | RealRTCW maintainer's mid-vendor shim (битый — vendored renderervk ожидает union) |
| `code/renderervk/realrtcw_shims.h` (added M2.2) | `typedef union { byte rgba[4]; uint32_t u32; } color4ub_t;` | Quake3e renderervk: читает `.rgba[]` и `.u32` |

Vendored код типа `code/renderervk/tr_shade_calc.c:608` делает `entity->shader.u32` — работает только на union, не на массив.

## Decision

Канонический `color4ub_t` живёт в **`code/qcommon/q_shared.h`** после `floatint_t` под guard'ом `COLOR4UB_T_DEFINED`. Все остальные определения свёрнуты в `#ifndef COLOR4UB_T_DEFINED` обёртки:

```c
#ifndef COLOR4UB_T_DEFINED
#define COLOR4UB_T_DEFINED
typedef union {
    byte     rgba[4];
    uint32_t u32;
} color4ub_t;
#endif
```

q_shared.h всегда подтягивается транзитивно (через `realrtcw_shims.h:18` для vendored, через `q_shared.h` для engine), поэтому канонический union landed first. Legacy `typedef byte color4ub_t[4]` сайты — no-op.

Аналог: `code/qcommon/qfiles.h` поле `drawVert_t.color` поменяно с `byte color[4]` на `color4ub_t color`. Каскадно фикснули 4 строки в `code/renderer/tr_curve.c:64-67` (engine OpenGL renderer) с `out->color[i]` → `out->color.rgba[i]`.

## Why

- **Single source of truth** (code-craft принцип 1). Три копии одного типа с разной семантикой — рецепт скрытого разрыва ABI. Один union покрывает все use-case'ы.
- **Union форма** обязательна потому что Quake3e renderervk **уже опирается на `.rgba`/`.u32` accessors**. Массивная форма (`byte[4]`) физически несовместима с этим vendored кодом.
- **q_shared.h как место** — он включается раньше всего в любом TU (через -include shim для vendored, через прямой include для engine), значит его определение всегда landed-first.
- **Guard вместо удаления legacy сайтов** — минимизирует diff, легче понять что поменялось, проще откатить.

## Trade-off

- **Расширение публичного интерфейса q_shared.h.** Раньше color4ub_t был «renderer-local». Теперь он в общем заголовке. Все TU видят union — некоторые могут не ожидать. Mitigation: union бинарно совместим с `byte[4]` (4 байта, тот же layout), не ломает stack init вроде `{0,0,0,0}`.
- **Engine OpenGL renderer тоже теперь видит union форму.** Пришлось патчить `tr_curve.c:64-67` на `.rgba` accessor. Минорное изменение в 4 строках, но это **shipped breaking change** для engine renderer'a. Регрессии в OpenGL будут видны на playtest'е (см. M5 review).

## Revisit if

- При попытке поднять OpenGL renderer (без BUILD_RENDERER_VULKAN) обнаружится что `.color.rgba` ломает старые drawVert paths — придётся рассматривать обратное переименование или две отдельные struct.
- Появится третий renderer (Metal native? rend2?) с третьим набором ожиданий — может оказаться что и `.rgba` недостаточно (хочется `.r/.g/.b/.a` field names).
- Quake3e upstream поменяет color4ub_t (маловероятно — структура в их коде стабильна с 2015).

## Связано

- `notes/reference/engine-map.md` секция «Renderer-ABI shim layer» → bullet «color4ub_t single source of truth»
- `notes/decisions/2026-06-08-refentity-shader-anon-union.md` — параллельная задача для `refEntity_t.shaderRGBA`, но тут оба варианта доступа имеют **разные имена полей**, поэтому решается анонимным union. Контраст с этим документом где имя одно — пришлось патчить engine.
- `docs/vulkan-phase2-abi-diff.md` Section 2 — каталог дрейфа, исходный пункт для `color4ub_t`
