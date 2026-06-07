# RealRTCW engine map — hot file:line pointers

«Если я навигирую код в этой области, вот указатель который понадобился в прошлый раз.» Только non-obvious — то что не вылавливается обычным grep'ом.

Каждая запись: `path/to/file:line` + что не очевидно + ссылка на decision-doc / PR / commit.

---

## Q3-family fork landmines (renderer ABI)

- **Два параллельных `tr_public.h` в дереве.**
  `code/renderer/tr_public.h` (engine OpenGL ABI, ~75 полей `refImport_t`) vs `code/renderercommon/tr_public.h` (vendored Quake3e ABI, ~100 полей, `Shutdown` принимает `refShutdownCode_t` enum а не `qboolean`). Который видишь — зависит от include-chain. Engine через `client.h:33`; vendored renderervk через `tr_local.h:52 → ../renderercommon/tr_public.h`. Если не сверишь оба — runtime garbage reads. История: M2.4 поиск в session 2026-06-08. Decision: [[m2.4-vtable-adapter-shortcut]].

- **Два параллельных `tr_types.h`.** Та же ловушка: `code/renderer/tr_types.h` (engine) vs `code/renderercommon/tr_types.h` (vendored). Guard `__TR_TYPES_H` означает что **первый загруженный побеждает** — обычно engine версия через `q_shared.h` транзитивно, тогда vendored просто пропускается. Это объясняет почему вендоренный код видит engine-side определения структур.

- **`Sys_StripAppBundle` strip = `/Applications` как fs_basepath.**
  `code/sys/sys_osx.m:102` — когда binary внутри `.app/Contents/MacOS/`, стрипает 3 уровня вверх. Результат: `fs_basepath = /Applications` (родитель .app), не bundle-internal. Сейвы/паки **всё равно находятся** через `fs_apppath` и `fs_homepath`, но при попытке загрузить game module из `fs_basepath/main/` ловишь тихий fail. Decision: see session 2026-06-07 transcripts. Симптом: `.app` запускается, меню работает, Load Game молча возвращает в menu.

- **`fs_apppath` спасает поиск game modules в .app.**
  `code/qcommon/files.c:4287` `fs_apppath = Sys_DefaultAppPath() = Sys_BinaryPath()` — путь к каталогу бинарника. Используется как дополнительный search-path в `FS_AddGameDirectory`. Поэтому даже когда `fs_basepath = /Applications` (после Sys_StripAppBundle), engine находит dylib'ы в `/Applications/RealRTCW.app/Contents/MacOS/main/`. **Не очевидно** — не искать «как находится renderer dlib в .app» через basepath, искать через apppath.

- **`developer` cvar блокирует Load Game в UI.**
  `code/ui/ui_main.c:4822` — anti-feature наследие RTCW. Комментарий «in developer, don't actually load the game». Симптом: список сейвов виден, клик «Load» → молча в главное меню, музыка перезапустилась. Console команда `loadgame %s` всё равно работает — gate только UI. Если включал `seta developer 1` через `ubsan-triage.cfg` или вручную, оно сохраняется в `~/Library/Application Support/RealRTCW/main/realrtcw_cvars.cfg` и каждый запуск ломает Load Game. Fix: `set` вместо `seta` в скрипт-cfg, либо `\developer 0; \writeconfig`.

## Renderer-ABI shim layer (M2 Vulkan Phase 2)

- **`color4ub_t` single source of truth: `code/qcommon/q_shared.h` (after `floatint_t`).**
  Guard `COLOR4UB_T_DEFINED`. Legacy `typedef byte color4ub_t[4]` в `code/renderer/tr_local.h:628` и `code/renderercommon/tr_types.h:35` обёрнуты в `#ifndef COLOR4UB_T_DEFINED` — q_shared.h всегда побеждает потому что подтягивается через `realrtcw_shims.h:18` (-include первым). Decision: [[color4ub-single-source]].

- **`refEntity_t.shaderRGBA` — анонимный union с `.shader` вариантом.**
  `code/renderer/tr_types.h:151` — engine кладёт через `.shaderRGBA[i]` (67 точек в cgame/ui/qagame), vendored renderervk читает через `.shader.rgba/.u32`. Анонимный union делит 4 байта. **Только потому что имена полей разные** — для `drawVert_t.color` тот же трюк не сработал (имя одинаковое с обеих сторон), пришлось патчить `code/renderer/tr_curve.c:64-67`. Decision: [[refentity-shader-anon-union]].

- **Vendor-guard hook + `realrtcw_*` prefix convention.**
  `.claude/hooks/block-vendored-renderervk.sh` блокирует Edit/Write на `code/renderervk/*` и `code/renderercommon/*`. Файлы с префиксом `realrtcw_` (например `realrtcw_shims.h`) пропускаются — они RealRTCW-authored support code, не upstream Quake3e. Escape hatch `REALRTCW_ALLOW_VENDOR_EDIT=1` для случаев когда нужно патчить уже-RealRTCW-модифицированный vendored файл (типа `code/renderercommon/tr_types.h` где RealRTCW maintainer оставил битые shim'ы при checkpoint 95177e7). Decision: [[vendor-prefix-convention]].

- **`refimport_t` adapter — conditional include в `code/client/client.h:33`.**
  При `BUILD_RENDERER_VULKAN=1` engine подтягивает renderercommon/tr_public.h (большая struct), при OpenGL — renderer/tr_public.h (маленькая). `cl_main.c:CL_InitRef` зерофицирует `ri` через `Com_Memset` чтобы непопулированные слоты были NULL не stack-garbage. Pragmatic shortcut вместо full vtable adapter (~12 строк vs ~400). Decision: [[m2.4-vtable-adapter-shortcut]].

## Build / packaging

- **`USE_INTERNAL_LIBS=0` обязателен на macOS.**
  Без него Makefile тянет `code/freetype-2.9/src/gzip/` (bundled FreeType internal gzip) который ожидает `Byte`/`Bytef` типы из bundled zlib — несовместимо с современным clang. На `macos-arm64` ветке это уже зашито в `playtest.sh`; на `macos-arm64-vulkan` ветке (checkpoint 95177e7) — забыли, надо добавлять явно. Команда: `make ARCH=arm64 USE_RENDERER_DLOPEN=1 BUILD_RENDERER_VULKAN=1 USE_OPENAL=1 USE_INTERNAL_LIBS=0 -j8 -k`.

- **Makefile флаг — `BUILD_RENDERER_VULKAN=1`, не `USE_VULKAN_API=1`.**
  Plan и первоначальный abi-diff doc ссылались на `USE_VULKAN_API` — такого флага в Makefile нет. Реальный: `BUILD_RENDERER_VULKAN`. Triple-check команды сборки если получаешь странные «renderer is not vulkan» симптомы. Исправлено plan-revision commit 73dccf2.

- **`.app` bundle layout: paks в homepath, game modules в bundle.**
  Engine OpenGL renderer DLL загружается из `/Applications/RealRTCW.app/Contents/MacOS/renderer_sp_opengl1_arm64.dylib`, game modules (qagame/cgame/ui) из `/Applications/RealRTCW.app/Contents/MacOS/main/*.dylib`. Pak файлы (`.pk3`) — в `$HOME/Library/Application Support/RealRTCW/main/`. Engine ищет через `fs_apppath/main/` (для dylib) и `fs_homepath/main/` (для paks). См. [[Sys_StripAppBundle quirk]] выше.

---

## История кросс-линков

Записи выше ссылаются на decision-docs в `notes/decisions/`:
- [[m2.4-vtable-adapter-shortcut]] → `notes/decisions/2026-06-08-m2.4-vtable-adapter-shortcut.md`
- [[color4ub-single-source]] → `notes/decisions/2026-06-08-color4ub-single-source.md`
- [[refentity-shader-anon-union]] → `notes/decisions/2026-06-08-refentity-shader-anon-union.md`
- [[vendor-prefix-convention]] → `notes/decisions/2026-06-08-vendor-prefix-convention.md`
