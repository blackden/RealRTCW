# refEntity_t.shaderRGBA — анонимный union с .shader вариантом

**Date:** 2026-06-08
**Phase:** Vulkan Phase 2, Milestone 2 (Task 2.5)
**Commit:** часть 4e34ae5

## Context

`refEntity_t` (структура на которую передаются 3D-объекты из game-modules в renderer) имеет цветовое поле для tint'а. RealRTCW historically использовала:

```c
// code/renderer/tr_types.h:151
byte shaderRGBA[4];  // colors used by rgbgen entity shaders
```

Game modules (cgame.dll, ui.dll, qagame.dll) обращаются как `ent->shaderRGBA[0] = red`, `ent->shaderRGBA[3] = alpha` — **67 точек** по `code/cgame/`, `code/ui/`, `code/game/`.

Vendored Quake3e renderervk ожидает другую форму того же поля:

```c
color4ub_t shader;  // union { byte rgba[4]; uint32_t u32; }
```

И обращается как `entity->shader.u32` (быстрый whole-color compare) или `entity->shader.rgba[i]`.

**Имена полей разные** (`shaderRGBA` vs `shader`), что даёт нам lever'у для anonymous union.

## Decision

Заменили поле в `code/renderer/tr_types.h:151` на анонимный union, делящий 4 байта между двумя именами:

```c
// misc
/* Quake3e renderervk reads entity tint as a color4ub_t union with
 * .rgba[] and .u32 accessors; RealRTCW's engine code (cgame, ui)
 * historically writes byte shaderRGBA[4]. Anonymous union below lets
 * both access styles share the same 4 bytes — no engine-side rewrite
 * needed, vendored renderervk compiles against the same struct. */
union {
    byte shaderRGBA[4];
    union {
        byte rgba[4];
        uint32_t u32;
    } shader;
};
```

Внешний union содержит два альтернативных имени для одних и тех же 4 байт. Внутренний union (`shader`) даёт Quake3e-style `.rgba[]` + `.u32` accessors через outer field `.shader`.

Access patterns:
- Engine cgame/ui legacy: `ent->shaderRGBA[i]` ✓ (внешний union member 1)
- Vendored renderervk: `ent->shader.rgba[i]` ✓ (внешний → внутренний union)
- Vendored renderervk: `ent->shader.u32` ✓ (внешний → внутренний union)

Все три варианта делят те же 4 байта. Никакого изменения engine-side кода не нужно.

## Why

- **Zero diff в 67 точках engine codebase.** Если бы переименовали `shaderRGBA` → `shader.rgba`, пришлось бы патчить cgame.dll / ui.dll / qagame.dll кодом — это **API breakage** для пользовательских мод-плагинов (game modules могут собираться отдельно).
- **Имена полей разные** (`shaderRGBA` vs `shader`) — это даёт нам лазейку. Anonymous union работает когда нужно дать одной памяти два разных name accessor'а. Если бы имена совпадали (как в случае `drawVert_t.color`), такая лазейка не работала бы.
- **Binary layout идентичен.** 4 байта, и legacy и shader-style accessor'ы читают одни и те же байты в том же порядке. Никакой ABI break'а.

## Contrast — `drawVert_t.color` (НЕ работает таким же образом)

Параллельный кейс в `code/qcommon/qfiles.h:692`:

```c
typedef struct {
    ...
    byte color[4];  // engine OpenGL renderer accesses as out->color[i]
} drawVert_t;
```

Vendored renderervk обращается **через то же имя**: `verts[i].color.rgba[0]`. То есть **outer field тоже называется `color`** — нет возможности развести имена.

Anonymous union trick тут не работает: `verts[i].color[0]` и `verts[i].color.rgba[0]` требуют чтобы `color` БЫЛО ОДНОВРЕМЕННО массивом И union'ом. C type system такого не даёт.

Решение для drawVert_t.color было иное:
1. Поменять `byte color[4]` → `color4ub_t color` (теперь union)
2. Патчить engine renderer `code/renderer/tr_curve.c:64-67` на `.rgba` accessor — 4 строки.

См. [[color4ub-single-source]] (`notes/decisions/2026-06-08-color4ub-single-source.md`).

## Trade-off

- **`code/renderer/tr_types.h` теперь содержит «странный» nested anonymous union.** Кто-то reading этот код first time будет undisplay. Mitigation: длинный comment перед union поясняющий why.
- **Если future-RealRTCW кто-то напишет `ent->shaderRGBA = SOMETHING`** (whole-array assignment) — то это работало раньше когда поле было прямой массив, теперь анонимный union отвергает потому что `shaderRGBA` — array member of union, нельзя assignement'ом. Нужен `memcpy` или per-element. Mitigation: компилер сразу даст error, не silent breakage.

## Revisit if

- **API stability контракт меняется** — RealRTCW решит что game modules могут rebuild'иться, и тогда переименование `shaderRGBA` → `shader.rgba` становится приемлемым (cleaner code, минус anonymous union).
- **Quake3e upstream меняет** layout color4ub_t (маловероятно) — тогда внутренний union shape придётся обновить.

## Связано

- `notes/reference/engine-map.md` секция «Renderer-ABI shim layer» → bullet «refEntity_t.shaderRGBA анонимный union»
- `notes/decisions/2026-06-08-color4ub-single-source.md` — параллельная задача для типа color4ub_t как такового. Этот документ — про конкретный struct field; [[color4ub-single-source]] про тип целиком.
- code-craft принцип 1: dual representation accepted-with-rationale (engine codebase не модифицируется, vendored код не модифицируется, только union magic в shared header).
