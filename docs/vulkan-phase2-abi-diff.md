# RealRTCW ↔ Quake3e Renderer ABI Drift

## Scope

Headers compared:
- RealRTCW (local): `code/renderer/tr_public.h`, `code/renderer/tr_types.h`, `code/qcommon/q_shared.h`, `code/qcommon/qcommon.h`
- Quake3e (remote, `ec-/Quake3e` default branch): `code/qcommon/q_shared.h`, `code/qcommon/qcommon.h`
- Quake3e `tr_public.h`/`tr_types.h` live at `code/renderervk/` (not `code/renderer/`); those paths returned 404 from GitHub, meaning the Quake3e renderervk is self-contained and inherits its ABI exclusively from `q_shared.h` / `qcommon.h`.

Source of vendored `code/renderervk/`: Quake3e checkpoint `95177e7` (imported at branch tip).
Source of vendored `code/renderercommon/`: same Quake3e checkpoint.

**Total drift cataloged: 18 root items, 23 summary-table entries** (paired enum values like `CV_FLOAT`/`CV_INTEGER` and `CVAR_ARCHIVE_ND`/`CVAR_NODEFAULT` get their own table row but share one writeup in the section body).

---

## Summary table

| Identifier | Category | Shim mechanism | Risk |
|---|---|---|---|
| `NORETURN_PTR` | Macro | empty `#define` | Low |
| `FORMAT_PRINTF(x,y)` | Macro | map to `__attribute__((format(printf,x,y)))` | Low |
| `SGN(x)` | Macro | `#define SGN(x) (((x)>=0)?!!(x):-1)` | Low |
| `DotProduct4(a,b)` | Macro | 4-component dot product inline | Low |
| `VectorScale4(a,b,c)` | Macro | 4-component scale inline | Low |
| `CVAR_ARCHIVE_ND` | Macro | `#define CVAR_ARCHIVE_ND (CVAR_ARCHIVE|CVAR_NODEFAULT)` — needs `CVAR_NODEFAULT` added first | Low |
| `CVAR_NODEFAULT` | Macro | add new cvar flag bit `0x4000` to RealRTCW `q_shared.h` | Low |
| `CV_FLOAT` | Enum value | add `cvarValidator_t` enum stub | Low |
| `CV_INTEGER` | Enum value | part of same `cvarValidator_t` stub | Low |
| `CVG_RENDERER` | Enum value | add `cvarGroup_t` enum stub | Low |
| `color4ub_t` | Typedef | `typedef union { byte rgba[4]; uint32_t u32; } color4ub_t;` | Medium |
| `CONTENTS_NODE` | Macro | `#define CONTENTS_NODE -1` (Q3/ioquake3 BSP internal node sentinel) | Medium |
| `MAX_VIDEO_HANDLES` | Macro | expose existing `cl_cin.c` define to renderer headers | Low |
| `VIS_HEADER` | Macro | `#define VIS_HEADER 8` (size of cluster/clusterBytes header in BSP vis lump) | Medium |
| `Q_atof` | Function | shim wrapping `atof()` — Quake3e added bounds-safe wrapper | Low |
| `Q_stradd` | Function | shim: `char *Q_stradd(char *dst, const char *src)` wraps `strcat`+returns end pointer | Low |
| `Com_GenerateHashValue` | Function | shim wrapping existing RealRTCW `generateHashValue` (renderer-private) or re-expose via `qcommon.h` | Medium |
| `log2pad` | Function | shim: `static inline unsigned int log2pad(unsigned int v, int roundup)` — exists in Quake3e `qcommon.h` as `static ID_INLINE` | Low |
| `myftol` | Function | shim: `#define myftol(x) ((int)(x))` or `static inline long myftol(float f){return (long)f;}` | Low |
| `RE_RegisterShaderFromImage` | Function | exists in `code/renderer/tr_shader.c` — needs forward declaration added to a shared header visible to `renderercommon/tr_font.c` | Medium |
| `image_t` / `r_saveFontData` | Engine-side | `image_t` and `r_saveFontData` are renderer-private; `renderercommon/tr_font.c` needs glue | High |
| `refEntity_t.shader` | Engine-side | `color4ub_t shader` field read at runtime (replaces `byte shaderRGBA[4]`) — struct field rename+type change | High |
| `IMGFLAG_CLAMPTOEDGE` / `LIGHTMAP_2D` | Macro | both exist in `code/renderer/tr_local.h` and `code/renderer/tr_shader.c` respectively — need exposure to `renderercommon/` | Medium |

---

## Section 1 — Macro-shimmable

### `NORETURN_PTR`
- **Used in**: `code/renderervk/tr_local.h` (multiple static function pointer declarations)
- **Origin (Quake3e)**: `code/qcommon/q_shared.h` — `#define NORETURN_PTR __attribute__((noreturn))` on GCC/Clang, empty on MSVC
- **RealRTCW status**: RealRTCW `q_shared.h` has `NORETURN` for functions but not `NORETURN_PTR`
- **Shim**: `#define NORETURN_PTR` (no-op; the attribute is a static-analysis hint only, safe to drop)
- **Risk**: Low — pure annotation, no runtime effect

### `FORMAT_PRINTF(x, y)`
- **Used in**: `code/renderervk/tr_local.h`, `code/renderercommon/tr_font.c`
- **Origin (Quake3e)**: `code/qcommon/q_shared.h` — `#define FORMAT_PRINTF(x,y) __attribute__((format(printf,x,y)))`
- **RealRTCW status**: RealRTCW uses the attribute inline (`__attribute__ ((format (printf, 2, 3)))`) but has no `FORMAT_PRINTF` macro
- **Shim**: `#define FORMAT_PRINTF(x,y) __attribute__((format(printf,x,y)))`
- **Risk**: Low — annotation only

### `SGN(x)`
- **Used in**: `code/renderervk/tr_main.c:664` — `SGN(...)` call for billboard orientation
- **Origin (Quake3e)**: `code/qcommon/q_shared.h` — `#define SGN(x) (((x) >= 0) ? !!(x) : -1)`
- **RealRTCW status**: Not present in RealRTCW `q_shared.h`
- **Shim**: `#ifndef SGN` / `#define SGN(x) (((x) >= 0) ? !!(x) : -1)` / `#endif`
- **Risk**: Low — standard signum macro, semantics are unambiguous

### `DotProduct4(a,b)` and `VectorScale4(a,b,c)`
- **Used in**: `code/renderervk/tr_main.c:672` — frustum/sphere cull with 4-component vectors
- **Origin (Quake3e)**: `code/qcommon/q_shared.h`:
  - `#define DotProduct4(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1]+(a)[2]*(b)[2]+(a)[3]*(b)[3])`
  - `#define VectorScale4(a,b,c) ((c)[0]=(a)[0]*(b),(c)[1]=(a)[1]*(b),(c)[2]=(a)[2]*(b),(c)[3]=(a)[3]*(b))`
- **RealRTCW status**: Not present
- **Shim**: Direct `#define` copies from Quake3e source
- **Risk**: Low — pure arithmetic macros, no state

### `CVAR_NODEFAULT` and `CVAR_ARCHIVE_ND`
- **Used in**: `code/renderervk/tr_init.c:1510–1522` — numerous `Cvar_Get(…, CVAR_ARCHIVE_ND)` calls
- **Origin (Quake3e)**: `code/qcommon/q_shared.h`:
  - `#define CVAR_NODEFAULT 0x4000` — do not write to config if value matches default
  - `#define CVAR_ARCHIVE_ND (CVAR_ARCHIVE | CVAR_NODEFAULT)`
- **RealRTCW status**: `CVAR_ARCHIVE` exists (`0x0001`); `CVAR_NODEFAULT` and `CVAR_ARCHIVE_ND` do not
- **Shim**: Add `#define CVAR_NODEFAULT 0x4000` and `#define CVAR_ARCHIVE_ND (CVAR_ARCHIVE|CVAR_NODEFAULT)` to the shim header. The flag bit must not collide with existing RealRTCW cvar flags (RealRTCW uses up to `CVAR_PRIVATE 0x8000`; `0x4000` is unused — safe).
- **Risk**: Low for build; Low-Medium at runtime (engine cvar machinery ignores unknown flags gracefully)

---

## Section 2 — Typedef-shimmable

### `color4ub_t`
- **Used in**: `code/renderervk/tr_local.h:362,611,1134,1578,1591,1597,1649,1650,1651,1701` — struct members and function parameters throughout. Also `code/renderervk/tr_backend.c`, `tr_shade_calc.c`, `tr_surface.c`, `vk.c`, `vk_vbo.c`, `vk_flares.c`
- **Origin (Quake3e)**: `code/qcommon/q_shared.h`:
  ```c
  typedef union {
      byte rgba[4];
      uint32_t u32;
  } color4ub_t;
  ```
- **RealRTCW status**: Not present. RealRTCW uses raw `byte[4]` (`shaderRGBA[4]` in `refEntity_t`). The renderervk code actively reads both `.rgba[]` members and the `.u32` member.
- **Shim**: Add the exact union typedef to the shim header. No RealRTCW engine structs need changing for the internal renderervk types — only `refEntity_t.shader` is an engine-boundary issue (see Section 4).
- **Risk**: Medium — the union layout must be correct (rgba[0]=R, [3]=A, matching engine convention). Verify byte-order assumptions against `polyVert_t.modulate` usage in `tr_surface.c:277`.

### `cvarValidator_t` (with `CV_FLOAT`, `CV_INTEGER`)
- **Used in**: `code/renderervk/tr_init.c:1515,1864` — `Cvar_Get` extended call with validator hint
- **Origin (Quake3e)**: `code/qcommon/q_shared.h`:
  ```c
  typedef enum { CV_NONE=0, CV_FLOAT, CV_INTEGER, CV_FSPATH, CV_MAX } cvarValidator_t;
  ```
  and `cvar_t` gains a `cvarValidator_t validator` field.
- **RealRTCW status**: Neither enum nor field exist. RealRTCW `Cvar_Get` has a 3-arg signature.
- **Shim**: Add `cvarValidator_t` enum stub to shim header. The renderervk code passes it as an extra argument to `Cvar_Get` — this means the shim must also handle the extended `Cvar_Get` call signature (see Section 3, `Cvar_Get` wrapper).
- **Risk**: Medium — needs a matching wrapper for the function call, not just the typedef

### `cvarGroup_t` (with `CVG_RENDERER`)
- **Used in**: `code/renderervk/tr_cmds.c:305,417` — group-based cvar change detection
- **Origin (Quake3e)**: `code/qcommon/q_shared.h`:
  ```c
  typedef enum { CVG_NONE=0, CVG_RENDERER, CVG_SERVER, CVG_MAX } cvarGroup_t;
  ```
- **RealRTCW status**: Not present
- **Shim**: Add enum stub. The renderervk calls `Cvar_CheckGroup(CVG_RENDERER)` and `Cvar_ResetGroup(CVG_RENDERER, …)` — those functions also do not exist in RealRTCW (see Section 3).
- **Risk**: Low for the enum itself; Medium for the functions that consume it

---

## Section 3 — Function-shimmable

### `log2pad(v, roundup)`
- **Used in**: `code/renderervk/vk.c:3982,4048` — texture dimension rounding for Vulkan image creation
- **Origin (Quake3e)**: `code/qcommon/qcommon.h` as `static ID_INLINE unsigned int log2pad(unsigned int v, int roundup)` — rounds up (or down) `v` to the nearest power of two
- **RealRTCW status**: Not present. RealRTCW has `Q_log2(int val)` (returns log2 of val, not the padded power).
- **Shim**: Copy the `static inline` implementation verbatim from Quake3e `qcommon.h` into the shim header. Self-contained, no dependencies.
- **Risk**: Low — pure integer math, no external state

### `myftol(f)`
- **Used in**: `code/renderervk/tr_light.c:390`, `code/renderervk/tr_sky.c:590` — float-to-int truncation in lighting calc and sky rendering
- **Origin (Quake3e)**: Defined as a macro or inline; semantics are truncate-toward-zero (C cast)
- **RealRTCW status**: `myftol` exists in `sdk/rtcw-bspc-custom` and RealRTCW's own `tr_shade_calc.c` uses it as a private function. Not declared in any shared header.
- **Shim**: `#define myftol(x) ((int)(x))` — matches the truncation semantics used in the diffuse lighting path
- **Risk**: Low — identical semantics to C cast on all arm64 platforms

### `SGN` — listed in Section 1 (macro), not here

### `Q_atof(str)`
- **Used in**: `code/renderervk/tr_shader.c:96,123,315,333,375,411,433,462` — shader text parsing
- **Origin (Quake3e)**: `code/qcommon/q_shared.h` — `float Q_atof(const char *str)` — locale-independent `atof` wrapper
- **RealRTCW status**: Not declared in `q_shared.h`; plain `atof()` is used instead
- **Shim**: `static inline float Q_atof(const char *str) { return (float)atof(str); }` — acceptable on a fixed-locale engine build. Or forward-declare and implement in `realrtcw_engine_glue.c`.
- **Risk**: Low — renderervk uses it purely for float parsing of shader numeric values

### `Q_stradd(dst, src)`
- **Used in**: `code/renderervk/vk.c:1698,1703` — building Vulkan extension name strings
- **Origin (Quake3e)**: `code/qcommon/q_shared.h` — `char *Q_stradd(char *dst, const char *src)` — appends `src` to `dst`, returns pointer to new NUL terminator (like `stpcpy`)
- **RealRTCW status**: Not present
- **Shim**: `static inline char *Q_stradd(char *dst, const char *src) { char c; while ((c = *src++) != '\0') *dst++ = c; *dst = '\0'; return dst; }`
- **Risk**: Low — trivial string utility

### `Com_GenerateHashValue(fname, size)`
- **Used in**: `code/renderervk/tr_image.c:1050,1270`, `code/renderervk/tr_shader.c:84` — image/shader hash table lookups
- **Origin (Quake3e)**: `code/qcommon/q_shared.h` — `unsigned long Com_GenerateHashValue(const char *fname, const unsigned int size)`
- **RealRTCW status**: RealRTCW has a renderer-private `generateHashValue` in `code/renderer/tr_shader.c` and `tr_image.c` (static functions, not exported). The Quake3e version is identical in semantics.
- **Shim**: Add `unsigned long Com_GenerateHashValue(const char *fname, unsigned int size);` declaration to the shim header and implement in `realrtcw_engine_glue.c` by calling the same hash algorithm (copy from Quake3e `q_shared.c` — pure function, no dependencies). Alternatively, `#define Com_GenerateHashValue generateHashValue` if the local static is made non-static in the vk renderer build.
- **Risk**: Medium — must ensure identical hash semantics (same algorithm) to avoid hash-table collisions between opengl1 and vulkan renderer paths at runtime

### Cvar API extensions: `Cvar_CheckGroup`, `Cvar_ResetGroup`
- **Used in**: `code/renderervk/tr_cmds.c:305,417` — detect and reset renderer cvar group changes each frame
- **Origin (Quake3e)**: `code/qcommon/qcommon.h`:
  - `int Cvar_CheckGroup(cvarGroup_t group)`
  - `void Cvar_ResetGroup(cvarGroup_t group, qboolean resetModifiedFlags)`
- **RealRTCW status**: Not present
- **Shim**: Add no-op stubs: `static inline int Cvar_CheckGroup(cvarGroup_t g){(void)g;return 0;}` and `static inline void Cvar_ResetGroup(cvarGroup_t g, qboolean b){(void)g;(void)b;}`. The effect is that the renderer never detects group-level cvar changes, which means it falls back to per-cvar `->modified` checks it already has. Acceptable degradation.
- **Risk**: Low-Medium — feature degradation (group-change fast path disabled) but no crash risk

### Extended `Cvar_Get` / `Cvar_CheckRange` signature
- **Used in**: `code/renderervk/tr_init.c` — calls like `ri.Cvar_Get("r_…", "…", CVAR_ARCHIVE_ND, CV_FLOAT)`
- **Origin (Quake3e)**: `refimport_t.Cvar_Get` takes an extra `cvarValidator_t` 4th argument
- **RealRTCW status**: `refimport_t.Cvar_Get` has 3 args `(name, value, flags)`
- **Shim**: In the shim header add `#define ri_Cvar_Get(n,v,f,t) ri.Cvar_Get(n,v,f)` — but since the vendored code calls `ri.Cvar_Get(…)` directly via function pointer, the safest shim is a wrapper macro that discards the 4th argument. **However** this requires the function pointer type to match — the real fix is to make the renderervk Cvar_Get calls compatible by providing a shim `refimport_t` adapter in `realrtcw_engine_glue.c`. Flag as Medium: needs careful vtable handling.
- **Risk**: Medium — ABI mismatch in function pointer call if the Quake3e `refimport_t` and RealRTCW `refimport_t` definitions differ (confirmed: they do — Quake3e adds the validator arg)

### `RE_RegisterShaderFromImage` visibility
- **Used in**: `code/renderercommon/tr_font.c:506` — registers a font atlas texture as a shader
- **Origin**: Exists in **both** `code/renderer/tr_shader.c` (RealRTCW GL1) and Quake3e renderervk. The function is present but declared only in the renderer-private `tr_local.h`, not in any shared header.
- **RealRTCW status**: Function body exists, signature: `qhandle_t RE_RegisterShaderFromImage(const char *name, int lightmapIndex, image_t *image, qboolean mipRawImage)`
- **Shim**: Add a forward declaration to the shim header, or to a shared internal header that both `renderercommon/` and `renderervk/` see. Also requires `image_t` to be visible (see Section 4, `image_t`).
- **Risk**: Medium — `image_t` forward declaration needed; signature must match

### `CONTENTS_NODE`
- **Used in**: `code/renderervk/tr_marks.c:142`, `tr_world.c:455,650,754` — BSP traversal (skip internal nodes, only process leaves)
- **Origin (Quake3e / ioquake3)**: `#define CONTENTS_NODE -1` — used as the sentinel for internal BSP nodes (nodes have `contents == -1`, leaves have real content flags)
- **RealRTCW status**: `code/qcommon/surfaceflags.h` confirmed: no `CONTENTS_NODE`. The RTCW surfaceflags.h has all leaf-level `CONTENTS_*` values but not this tree-traversal sentinel.
- **Shim**: `#define CONTENTS_NODE -1`
- **Risk**: Medium — value (-1) must match what the CM/BSP loader stores in internal nodes. Confirmed correct: BSP format stores -1 for internal nodes (Q3 BSP spec).

### `VIS_HEADER`
- **Used in**: `code/renderervk/tr_bsp.c:500,508,509` — skipping the 8-byte vis lump header (numClusters + clusterBytes) when reading raw vis data
- **Origin (Quake3e)**: Likely `#define VIS_HEADER 8` (2 × 4-byte ints at start of vis lump). Origin uncertain — not found in Quake3e `q_shared.h` or `qcommon.h` headers retrieved. Suggest checking `code/renderervk/tr_bsp.c` local defines or Quake3e `tr_local.h`.
- **Shim**: `#define VIS_HEADER 8`
- **Risk**: Medium — value must match BSP format (8 bytes = numClusters int + clusterBytes int, confirmed in Q3 BSP spec and `code/renderer/tr_bsp.c:R_LoadVisibility`). If Quake3e defines it differently, check `code/renderervk/tr_bsp.c` first before committing this value.

### `MAX_VIDEO_HANDLES`
- **Used in**: `code/renderervk/tr_local.h:1187` — `image_t *scratchImage[MAX_VIDEO_HANDLES]` in `trGlobals_t`
- **Origin**: Defined in `code/client/cl_cin.c:73` as `#define MAX_VIDEO_HANDLES 16`
- **RealRTCW status**: Exists but is local to `cl_cin.c`, not exposed to renderer headers
- **Shim**: `#define MAX_VIDEO_HANDLES 16` in the shim header. Must stay in sync with `cl_cin.c` definition (currently 16 — array size must match or renderer will under/over-allocate scratch images).
- **Risk**: Low for build; note if `cl_cin.c` value ever changes, both must be updated

### `IMGFLAG_CLAMPTOEDGE` and `LIGHTMAP_2D`
- **Used in**: `code/renderercommon/tr_font.c:505,506` — creating and registering font atlas texture
- **Origin**: Both defined in `code/renderer/tr_local.h` (renderer-private):
  - `IMGFLAG_CLAMPTOEDGE = 0x0040` in `imgFlags_t` enum
  - `LIGHTMAP_2D` is a `#define` in `code/renderer/tr_shader.c` (value `-4`)
- **RealRTCW status**: Both exist in the GL1 renderer's private headers. `renderercommon/tr_font.c` can't see them because it doesn't include `tr_local.h` in the renderervk build.
- **Shim**: Define both in the shim header with matching values: `#define IMGFLAG_CLAMPTOEDGE 0x0040` and `#define LIGHTMAP_2D (-4)`. Values taken directly from existing RealRTCW renderer code — no semantic change.
- **Risk**: Low — shim merely duplicates existing values; if upstream changes them the shim must track

---

## Section 4 — Engine-side-needed (last resort)

### `refEntity_t.shader` (read at runtime)
- **Used in**:
  - `code/renderervk/tr_shade_calc.c:608` — `backEnd.currentEntity->e.shader.u32` (read)
  - `code/renderervk/tr_shade_calc.c:629–632` — `backEnd.currentEntity->e.shader.rgba[0..3]` (read)
  - `code/renderervk/tr_surface.c:248` — `backEnd.currentEntity->e.shader` passed to `RB_AddQuadStamp` (read)
- **Origin (Quake3e)**: Quake3e renamed `refEntity_t.shaderRGBA[4]` (type `byte[4]`) to `refEntity_t.shader` (type `color4ub_t`, a union `{byte rgba[4]; uint32_t u32}`). The field provides identical storage but the union allows both byte-array and 32-bit-integer access.
- **RealRTCW status**: `refEntity_t` (in `code/renderer/tr_types.h`) has `byte shaderRGBA[4]` — not a `color4ub_t` union and not named `.shader`.
- **Assessment**: The renderervk code **reads** `.shader.u32` and `.shader.rgba[]`. This is not a write-only annotation — it is a live data path. The engine submits `refEntity_t` structs from the game/client, which fill `shaderRGBA[4]`. The renderer reads `e.shader.rgba[]` in its place.
- **Resolution options**:
  1. **Add `.shader` alias** (preferred): Add `color4ub_t shader;` to `refEntity_t` alongside `byte shaderRGBA[4]`, and on the engine side keep filling `shaderRGBA`. In the glue layer, before renderervk receives each entity, copy: `e.shader.u32 = *(uint32_t*)e.shaderRGBA`. This is a translation layer in `realrtcw_engine_glue.c`.
  2. **Union overlay**: Make `shaderRGBA` part of a `color4ub_t` union in `refEntity_t`. This is a breaking ABI change to the game module interface — avoid.
  3. **Rename in renderervk**: In the shim header, add `#define shader shaderRGBA` — but this breaks the `.rgba[]` / `.u32` member access syntax since `shaderRGBA` is `byte[4]`, not a union. **Does not work.**
- **Recommended**: Option 1 (translation layer). Low memory cost (4 bytes extra in `trRefEntity_t`'s copy — not the public `refEntity_t`). The vk renderer works on `trRefEntity_t` (its own extended wrapper); add `color4ub_t shader` to `trRefEntity_t` and populate it when copying from the public `refEntity_t`.
- **Risk**: **High** — incorrect translation will corrupt all entity tinting/alpha (player RGBA modulation, spectator, etc.)

### `image_t` type and `r_saveFontData` cvar
- **Used in**: `code/renderercommon/tr_font.c:344,500,505` — `image_t *image` local variable; `r_saveFontData->integer` cvar access
- **Origin**: Both are renderer-private to `code/renderer/tr_local.h`. The `renderercommon/tr_font.c` file is shared across GL1 and VK renderers — it was designed to be compiled into each renderer, so it expects access to the renderer's private types.
- **RealRTCW status**: `image_t` is defined in `code/renderer/tr_local.h` (GL1). `r_saveFontData` is declared as `cvar_t *r_saveFontData` in `code/renderer/tr_init.c`.
- **Assessment**: `renderercommon/tr_font.c` **must** be compiled in the context of the renderer that owns it. In Quake3e, `tr_font.c` is compiled as part of `renderervk/`, which includes `tr_local.h`. The fix is to include the renderervk's own `tr_local.h` when compiling `renderercommon/tr_font.c` — this is already how the Makefile should set it up (include path ordering). If `tr_font.c` cannot see `renderervk/tr_local.h`, it must be moved to `renderervk/` or the include path must be corrected in the build system.
- **Resolution**: Adjust build system to ensure `renderercommon/tr_font.c` is compiled with `-Icode/renderervk` before `-Icode/renderercommon`. This exposes both `image_t` and `r_saveFontData` (which are declared in `renderervk/tr_local.h` and `renderervk/tr_init.c` respectively). **Not a shim — a build system fix.**
- **Risk**: **High** — if include path is wrong, all font rendering in the VK renderer is broken

---

## Notes

### `Com_Error` signature divergence
- **Error location**: `code/renderervk/tr_init.c:255` — `conflicting types for 'Com_Error'`
- **Quake3e signature** (`q_shared.h`): `void NORETURN FORMAT_PRINTF(2,3) QDECL Com_Error(errorParm_t level, const char *fmt, ...)`
- **RealRTCW signature** (`q_shared.h`): `void QDECL Com_Error(int level, const char *error, ...) __attribute__((noreturn, format(printf, 2, 3)))`
- **Difference**: Quake3e uses `errorParm_t` (an enum) as the first arg; RealRTCW uses `int`. These are ABI-compatible on all platforms (`errorParm_t` is implicitly `int`-sized). The conflict arises from the `NORETURN` macro placement — Quake3e puts `NORETURN` **before** the return type as a macro, RealRTCW puts `__attribute__((noreturn))` in the attribute list.
- **Resolution**: The `NORETURN` macro in the shim header should expand to `__attribute__((noreturn))` (matching RealRTCW), which collapses the `NORETURN Com_Error` forward declaration to the same attribute form RealRTCW already uses. With `FORMAT_PRINTF` shimmed, the declarations will match.
- **Risk**: Low once `NORETURN` and `FORMAT_PRINTF` macros are shimmed correctly

### `refEntity_t.shader` vs `shaderRGBA` — write path
- The engine side (client, cgame) writes to `refEntity_t.shaderRGBA`. The renderervk code reads `e.shader.rgba[]` and `e.shader.u32`. The translation must happen in the renderervk's `R_AddEntityToScene` (or equivalent function in `tr_scene.c`) where the public `refEntity_t` is copied into the internal `trRefEntity_t`. At that copy point, add: `ent->e_shader.u32 = *(const uint32_t *)re->shaderRGBA;` (where `e_shader` is the `color4ub_t` field added to `trRefEntity_t`). All renderervk shader RGBA reads then use `e.shader` consistently. This is a 4-byte copy at entity submission time — negligible overhead.

### BSP `REFENTITYNUM_BITS` conflict
- The baseline log shows a diagnostic around `REFENTITYNUM_BITS` — Quake3e uses 12, RealRTCW uses 11. This is visible in the log at line ~2719 as a note, not a build error. Not cataloged as a drift identifier here because it does not produce an `error:` line, but it **should be reviewed** before enabling the VK renderer in production: the dlight mask bit packing in `tr_local.h:drawsurf_t` depends on this value.

### Quake3e `tr_public.h`/`tr_types.h` location
- These headers do not exist at `code/renderer/` in Quake3e's repository — the `ec-/Quake3e` renderervk is fully self-contained. `refimport_t` / `refexport_t` are defined inside `code/renderervk/tr_local.h` directly, not in a separate `tr_public.h`. This means the vtable drift (Cvar_Get extra arg, etc.) is visible only by comparing `renderervk/tr_local.h` against RealRTCW's `code/renderer/tr_public.h`.
