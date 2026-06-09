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

- **`refimport_t` adapter — `code/client/cl_refvulkan.c` (M3.5 closure 2026-06-09).**
  Engine populates the small RTCW `refimport_t` as it always has (`cl_main.c:3430-3482`, unchanged). Under `BUILD_RENDERER_VULKAN`, `CL_InitRef` (`cl_main.c:3484`) calls `CL_BuildVulkanRefImport()` which returns a statically-allocated big Quake3e-shaped struct filled slot-by-slot — direct aliases for shared names (Cmd_*, FS_*, Cvar_*), wrappers for signature drift (Microseconds, Malloc+FreeAll local tracker, Hunk_* size_t→int narrowing, FS_ReadFile long→int), real Vulkan window code (VK_CreateSurface via SDL3 + engine's `SDL_window` global from sdl_input.c with `static` dropped), intentional NULLs for ~23 Q3e-only slots RTCW gameplay never asks for. This pointer is what `GetRefAPI` receives. Disjoint-vocabulary finding: ~13 fields exist only in small (`AddCoronaToScene`, `Z_Malloc`, `IN_Init/Restart/Shutdown`, `Sys_GLimpInit/SafeInit`, `RegisterSmartSkin`, etc.), ~25 only in big (`Microseconds`, `FreeAll`, `inPVS`, `VK_CreateSurface`, `Add*LightToScene`, etc.). Decision: [[m3.5-vtable-adapter-proper]]. Supersedes [[m2.4-vtable-adapter-shortcut]] which was a no-op anyway (the `-DBUILD_RENDERER_VULKAN` define never reached engine TUs because of the recursive-submake CFLAGS-override gotcha — see «`override` keyword» bullet below).

- **`override CFLAGS +=` is load-bearing on per-target defines for `release:`/`debug:` builds (M3.5 fix 2026-06-09).**
  `Makefile:1533-1536` (`release:`) invokes `$(MAKE) targets B=... CFLAGS="$(CFLAGS) $(BASE_CFLAGS) ..."` — the sub-make gets `CFLAGS=` as a **command-line override** which, per GNU Make precedence, silently voids any subsequent `CFLAGS +=` Makefile assignment, including target-specific ones. Without the `override` keyword, the per-target append at `Makefile:3082-3092` was completely ignored — the `#ifdef BUILD_RENDERER_VULKAN`-guarded code in `sdl_glimp.c` (M3) and `cl_main.c` (M3.5) was dead. M3 «closed» on link-clean without runtime verification, so this hid until M3.5. **Pattern for any new per-target define in this Makefile:** always use `$(B)/path/X.o: override CFLAGS += -DFOO`. Decision: [[m3.5-vtable-adapter-proper]] (companion finding section).

- **Renderer-side cvar bridge — `code/renderervk/realrtcw_vk_window_bridge.c`.**
  Симметрия к refimport_t adapter, но на другой стороне vtable. Shared `code/sdl/sdl_glimp.c` ждёт 11 renderer-side externs (`r_mode`, `r_fullscreen`, `r_noborder`, `r_colorbits/depthbits/stencilbits`, `r_stereoEnabled`, `r_swapInterval`, `displayAspect`, `haveClampToEdge`, `R_GetModeInfo`) — vendored Quake3e `renderervk/tr_init.c` их выкинул в пользу `ri.Cvar_VariableString` lookup'ов. Bridge определяет эти 11 символов tentative globals + `R_GetModeInfo` portированный из iortcw SP `tr_init.c:346-416`, latch'ит cvar pointers через `ri.Cvar_Get` в `RealRTCW_VkBridgeInit()` (вызов первой строкой `GLimp_Init` под `#ifdef BUILD_RENDERER_VULKAN`). Линкуется только в `Q3VKOBJ`. Decision: [[m3-renderer-side-cvar-bridge]].

- **`tr_subs.o` нельзя в `Q3VKOBJ` — duplicate symbol с renderervk/tr_init.c.**
  `code/renderer/tr_subs.c:26-48` определяет `Com_Printf`/`Com_Error` безусловно; vendored `code/renderervk/tr_init.c:254-275` тоже под `#ifdef USE_RENDERER_DLOPEN`. Linking both into Vulkan DLL → ld duplicate symbol. Pre-M3 vendoring miss — surface'ит только на clean rebuild (между inkremental сборками .o-файлы оставались stale). OpenGL Q3ROBJ оставлен с `tr_subs.o` потому что engine `code/renderer/tr_init.c` НЕ определяет эти функции. Учтено в `Makefile:2174-2185` комментом-warning'ом. История: M3 closure 2026-06-09. Decision: [[m3-renderer-side-cvar-bridge]] (раздел «Pre-M3 vendoring landmine»).

- **Vulkan-DLL-only guard pattern — target-specific CFLAGS append.**
  `-DUSE_VULKAN_API` сидит в `BASE_CFLAGS` (см. `Makefile:520`) и льётся в ОБА билда: `renderer/sdl_glimp.o` (для OpenGL DLL) и `rendv/sdl_glimp.o` (для Vulkan DLL). Поэтому НЕ годится как preprocessor guard для кода, который должен жить только в одном DLL'е. Решение в `Makefile:3082-3093` — GNU Make target-specific append с `override`: `$(B)/rendv/sdl_glimp.o: override CFLAGS += -DBUILD_RENDERER_VULKAN`. Когда нужен ещё один Vulkan-only define на другом rendv/ файле — добавлять такой же per-target append. **`override` обязателен** (см. отдельный bullet выше). Decision: [[m3-renderer-side-cvar-bridge]], [[m3.5-vtable-adapter-proper]].

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
- [[m2.4-vtable-adapter-shortcut]] → `notes/decisions/2026-06-08-m2.4-vtable-adapter-shortcut.md` (**superseded** by M3.5)
- [[color4ub-single-source]] → `notes/decisions/2026-06-08-color4ub-single-source.md`
- [[refentity-shader-anon-union]] → `notes/decisions/2026-06-08-refentity-shader-anon-union.md`
- [[vendor-prefix-convention]] → `notes/decisions/2026-06-08-vendor-prefix-convention.md`
- [[m3-renderer-side-cvar-bridge]] → `notes/decisions/2026-06-09-m3-renderer-side-cvar-bridge.md`
- [[m3.5-vtable-adapter-proper]] → `notes/decisions/2026-06-09-m3.5-vtable-adapter-proper.md`
