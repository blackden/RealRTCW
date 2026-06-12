# M7 + M7.5 + M8: JPG loader engine-side, fog investigation, shader keyword handling

> **Status (2026-06-12 evening):** M7 LANDED. M7.5 + M8 deferred to next session — ragnar's hands needed for the user-driven smoke comparisons. Plan body below preserved for the next session's executor.
>
> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to step through tasks inline. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the four visible non-regressions from M6 smoke through three sequential milestones — M7 fixes the JPG-loader stub (collapses 3 of 4 symptoms), M7.5 investigates whether renderervk's fog pipeline is active in RealRTCW (single-session investigation, no code), M8 handles shader keywords `nofog`/`nocompress`/`allowcompress` with shape determined by M7.5 result.

**Architecture.** M7 puts libjpeg consumer into engine-side code (`code/client/cl_jpeg.c`) following the canonical Quake3e pattern — vendored renderervk's thin shim `tr_image_jpg.c` already delegates to `ri.CL_LoadJPG`, all we add is the engine-side implementation. Zero vendor edits. M7.5 is a manual playtest comparing OpenGL vs Vulkan visual on a fog-heavy RTCW map — output is a decision-note that pins down M8 shape. M8 closes shader keywords via either (Branch A) FS-layer text preprocessing in `vk_FS_ReadFile` if fog dormant, or (Branch B) vendor-edit noFog port to renderervk if fog active.

**Tech Stack:** C99, libjpeg (already linked via JPGOBJ), make, RealRTCW build system, MoltenVK on macOS arm64.

---

## Session log (2026-06-12 evening)

**M7 — LANDED.** Commit on `macos-arm64-vulkan`. 4 files changed: created `code/client/cl_jpeg.c` (~140 lines, port of legacy `R_LoadJPG` with engine-side ri.* → Com_/FS_/Z_Malloc substitution), added prototype to `code/qcommon/qcommon.h`, added `cl_jpeg.o` to Q3OBJ in Makefile + JPEG_LIBS to CLIENT_LIBS + JPGOBJ to USE_RENDERER_DLOPEN!=0 CLIENTBIN link (this was a plan-time miss — original plan only added Q3OBJ entry, but engine binary also needed libjpeg in the link command), rewrote `vk_CL_LoadJPG` in `cl_refvulkan.c` from NULL-stub to delegate.

Smoke: `playtest.sh --vulkan` reaches main menu, background image decodes (orange/brown cinematic tones visible — not the M6 placeholder grayscale). No "couldn't find image realrtcw_background.tga" warnings. No LoadJPG errors. Clean exit 0, no sanitizer hits. Log saved at `/tmp/m7-smoke.log` during the smoke window.

**Epistemic correction — important for M7.5/M8.** Original recon classified the M6 "blurred/grayscale" symptom as a downstream effect of the JPG-stub (placeholder texture). The post-M7 smoke disproves this: image is now decoded into color, but UI textures still render at low effective resolution. So **blur is a separate bug, not JPG fallout**. M7.5 must compare with OpenGL build at the same camera to classify: (a) low-res upstream asset stretched to 1920×1200 = expected, no fix needed; or (b) renderervk sampler/mipmap defect = new M-fix candidate.

**Bonus findings from M7 smoke log** (not blockers, recorded for future M-candidates):
- Two shader files entirely ignored by parser due to upstream syntax errors: `scripts/common.shader` line 842, `scripts/models_mapobjects_et.shader` line 1347 — both "missing closing brace". Legacy renderer is more lenient; renderervk's Quake3e parser rejects whole-file. These shaders contain hundreds of surface definitions — the rejection has wide visual fallout.
- `WARNING: music file sound/music/l_theme.wav is not 22k stereo` — pre-existing audio format mismatch, orthogonal.

**M7.5 + M8 deferred.** Ragnar away; both phases need his hands at the game (interactive screenshot comparisons, viewpos commands, gameplay smoke). Next session continues from this plan body unchanged.

---

## Working directory

All work in `~/fedorov_tech/RealRTCW-vulkan-wt`, branch `macos-arm64-vulkan`. HEAD at task start should be `b5f6b85` (M6 closed). Verify before starting:

```bash
cd ~/fedorov_tech/RealRTCW-vulkan-wt && git status -sb && git log --oneline -1
# Expected: "## macos-arm64-vulkan" + "b5f6b85 docs(m6): plan + decision-note ..."
```

Vendor-edit hook stays disabled for M7 work. For M8 Branch B (only), `REALRTCW_ALLOW_VENDOR_EDIT=1` must be in claude's launch env.

---

## M7: JPG loader engine-side

### Task 1: Create `code/client/cl_jpeg.c`

**Files:**
- Create: `code/client/cl_jpeg.c`

**Source pattern:** port of `code/renderer/tr_image_jpg.c` lines 23–267 (the `R_LoadJPG` half only — `SaveJPGToBuffer` / `RE_SaveJPG` are out of scope for M7, they're used for screenshots and depend on different refImport slots).

**Substitutions:**
| Renderer-side | Engine-side |
|---|---|
| `#include "tr_local.h"` | `#include "../qcommon/q_shared.h"` + `#include "../qcommon/qcommon.h"` |
| `ri.Printf(PRINT_ALL, ...)` | `Com_Printf(...)` |
| `ri.Error(ERR_DROP, ...)` | `Com_Error(ERR_DROP, ...)` |
| `ri.FS_ReadFile(...)` | `FS_ReadFile(...)` |
| `ri.FS_FreeFile(...)` | `FS_FreeFile(...)` |
| `ri.Z_Malloc(memcount)` | `Z_Malloc(memcount)` |
| `void R_LoadJPG(...)` | `void CL_LoadJPG(...)` |

- [ ] **Step 1: Create the file**

```c
/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

/*
 * Engine-side libjpeg consumer. Quake3e-canonical: renderercommon's
 * tr_image_jpg.c is a thin shim that delegates R_LoadJPG to ri.CL_LoadJPG.
 * The RealRTCW Vulkan refImport translator in cl_refvulkan.c routes
 * CL_LoadJPG here; legacy OpenGL renderer continues to use its own
 * tr_image_jpg.c (separate libjpeg copy linked into the renderer .dylib).
 *
 * M7 — 2026-06-12.
 */

#include <setjmp.h>

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#ifdef USE_INTERNAL_JPEG
#  define JPEG_INTERNALS
#endif

#include <jpeglib.h>

#ifndef USE_INTERNAL_JPEG
#  if JPEG_LIB_VERSION < 80 && !defined(MEM_SRCDST_SUPPORTED)
#    error Need system libjpeg >= 80 or jpeg_mem_ support
#  endif
#endif

typedef struct q_jpeg_error_mgr_s
{
	struct jpeg_error_mgr pub;
	jmp_buf setjmp_buffer;
} q_jpeg_error_mgr_t;

static void CL_JPGErrorExit( j_common_ptr cinfo )
{
	char buffer[JMSG_LENGTH_MAX];
	q_jpeg_error_mgr_t *jerr = (q_jpeg_error_mgr_t *)cinfo->err;

	(*cinfo->err->format_message)( cinfo, buffer );
	Com_Printf( "Error: %s", buffer );

	longjmp( jerr->setjmp_buffer, 1 );
}

static void CL_JPGOutputMessage( j_common_ptr cinfo )
{
	char buffer[JMSG_LENGTH_MAX];
	(*cinfo->err->format_message)( cinfo, buffer );
	Com_Printf( "%s\n", buffer );
}

void CL_LoadJPG( const char *filename, unsigned char **pic, int *width, int *height )
{
	struct jpeg_decompress_struct cinfo = { NULL };
	q_jpeg_error_mgr_t jerr;
	JSAMPARRAY buffer;
	unsigned int row_stride;
	unsigned int pixelcount, memcount;
	unsigned int sindex, dindex;
	byte *out;
	int len;
	union {
		byte *b;
		void *v;
	} fbuffer;
	byte *buf;

	*pic = NULL;

	len = FS_ReadFile( (char *)filename, &fbuffer.v );
	if ( !fbuffer.b || len < 0 ) {
		return;
	}

	cinfo.err = jpeg_std_error( &jerr.pub );
	cinfo.err->error_exit = CL_JPGErrorExit;
	cinfo.err->output_message = CL_JPGOutputMessage;

	if ( setjmp( jerr.setjmp_buffer ) ) {
		jpeg_destroy_decompress( &cinfo );
		FS_FreeFile( fbuffer.v );
		Com_Printf( ", loading file %s\n", filename );
		return;
	}

	jpeg_create_decompress( &cinfo );
	jpeg_mem_src( &cinfo, fbuffer.b, len );
	(void) jpeg_read_header( &cinfo, TRUE );

	cinfo.out_color_space = JCS_RGB;

	(void) jpeg_start_decompress( &cinfo );

	pixelcount = cinfo.output_width * cinfo.output_height;

	if ( !cinfo.output_width || !cinfo.output_height
	     || ( ( pixelcount * 4 ) / cinfo.output_width ) / 4 != cinfo.output_height
	     || pixelcount > 0x1FFFFFFF || cinfo.output_components != 3 ) {
		FS_FreeFile( fbuffer.v );
		jpeg_destroy_decompress( &cinfo );

		Com_Error( ERR_DROP,
		           "LoadJPG: %s has an invalid image format: %dx%d*4=%d, components: %d",
		           filename,
		           cinfo.output_width, cinfo.output_height,
		           pixelcount * 4, cinfo.output_components );
	}

	memcount = pixelcount * 4;
	row_stride = cinfo.output_width * cinfo.output_components;

	out = Z_Malloc( memcount );

	*width = cinfo.output_width;
	*height = cinfo.output_height;

	while ( cinfo.output_scanline < cinfo.output_height ) {
		buf = ( ( out + ( row_stride * cinfo.output_scanline ) ) );
		buffer = &buf;
		(void) jpeg_read_scanlines( &cinfo, buffer, 1 );
	}

	buf = out;

	/* Expand RGB → RGBA (alpha=255). */
	sindex = pixelcount * cinfo.output_components;
	dindex = memcount;

	do {
		buf[--dindex] = 255;
		buf[--dindex] = buf[--sindex];
		buf[--dindex] = buf[--sindex];
		buf[--dindex] = buf[--sindex];
	} while ( sindex );

	*pic = out;

	jpeg_finish_decompress( &cinfo );
	jpeg_destroy_decompress( &cinfo );

	FS_FreeFile( fbuffer.v );
}
```

### Task 2: Declare `CL_LoadJPG` in qcommon.h

**Files:**
- Modify: `code/qcommon/qcommon.h` (around line 1033, after the `CL_*` declarations cluster)

- [ ] **Step 1: Insert prototype**

Add after `CL_PacketEvent` (line 1033) and before any non-CL declarations:

```c
void CL_LoadJPG( const char *filename, unsigned char **pic, int *width, int *height );
```

### Task 3: Add cl_jpeg.o to Q3OBJ

**Files:**
- Modify: `Makefile` (around line 1966-1967, where cl_keys.o + cl_main.o are listed)

- [ ] **Step 1: Insert object reference**

Insert after the `$(B)/client/cl_main.o \` line:

```make
  $(B)/client/cl_jpeg.o \
```

### Task 4: Wire `vk_CL_LoadJPG` to engine-side `CL_LoadJPG`

**Files:**
- Modify: `code/client/cl_refvulkan.c:407-412`, also the comment at 268-273

- [ ] **Step 1: Replace the stub body**

Change at `cl_refvulkan.c:403-412` from:

```c
/* JPG loader stub. Leaves *pic NULL so the renderer's R_LoadImage
 * format-search-loop falls through to the next extension (TGA, PNG).
 * Game content for stock RTCW SP doesn't ship JPG textures, so this
 * is observationally inert. Wire to real libjpeg if/when needed. */
static void vk_CL_LoadJPG( const char *filename, unsigned char **pic, int *width, int *height ) {
    (void)filename;
    if ( pic ) *pic = NULL;
    if ( width ) *width = 0;
    if ( height ) *height = 0;
}
```

to:

```c
/* JPG loader. Delegates to engine-side CL_LoadJPG (code/client/cl_jpeg.c),
 * which links the same JPGOBJ libjpeg sources that the legacy OpenGL
 * renderer .dylib uses. M7 — 2026-06-12. */
static void vk_CL_LoadJPG( const char *filename, unsigned char **pic, int *width, int *height ) {
    CL_LoadJPG( filename, pic, width, height );
}
```

- [ ] **Step 2: Update the comment block at lines 268-274**

Change from:

```c
    /* --- NO-OP STUB: JPG image loader. RealRTCW engine has no libjpeg
     *     integration. The image loader loop in R_LoadImage tries each
     *     extension; if vk_CL_LoadJPG returns *pic=NULL the fallback
     *     to TGA/PNG continues. JPG textures effectively unsupported on
     *     Vulkan path for now -- iter 10 fix. Add real decode later if
     *     game content actually ships JPG textures. */
    vk_ri.CL_LoadJPG                = vk_CL_LoadJPG;
```

to:

```c
    /* --- DIRECT WIRE (M7): engine-side CL_LoadJPG (code/client/cl_jpeg.c)
     *     decodes JPG via libjpeg. Required for RTCW UI backgrounds like
     *     realrtcw_background.jpg. M6 left this as the visible stub-NULL
     *     symptom — the main menu rendered placeholder texture instead
     *     of the cover art. M7 closes it. */
    vk_ri.CL_LoadJPG                = vk_CL_LoadJPG;
```

### Task 5: Clean build subdirs that touch headers

**Files:**
- Delete: `build/release-darwin-arm64/client/*.o` and `build/release-darwin-arm64/renderer_sp_vulkan/*.o`

(Per [[feedback_clean_rebuild_after_header_edit]] — header touched in qcommon.h, must invalidate cached .o files.)

- [ ] **Step 1: Wipe stale objects**

```bash
cd ~/fedorov_tech/RealRTCW-vulkan-wt
rm -rf build/release-darwin-arm64/client
rm -rf build/release-darwin-arm64/renderer_sp_vulkan
```

### Task 6: Build

- [ ] **Step 1: Build the engine + Vulkan renderer**

```bash
cd ~/fedorov_tech/RealRTCW-vulkan-wt
USE_INTERNAL_LIBS=0 BUILD_RENDERER_VULKAN=1 make -j8 release 2>&1 | tail -40
```

Expected last 5 lines: link of `build/release-darwin-arm64/iowolfsp.arm64` + `renderer_sp_opengl1_arm64.dylib` + `renderer_sp_vulkan_arm64.dylib`, no errors.

If `cl_jpeg.c` fails to compile due to missing `Z_Malloc` declaration — check qcommon.h carefully, `Z_Malloc` lives at `code/qcommon/qcommon.h` (search confirms).

### Task 7: Smoke test (USER-DRIVEN)

Per `mac-game-port-toolkit` and sandbox limitations, ragnar runs the smoke in interactive Terminal.

- [ ] **Step 1: Brief ragnar to run smoke**

He runs:

```bash
cd ~/fedorov_tech/RealRTCW-vulkan-wt && scripts/mac/playtest.sh --vulkan
```

**Acceptance criteria:**
1. Game reaches main menu (same as M6 baseline).
2. **Background image visible as the RTCW cover art**, not grayscale placeholder/checker. This is the M7 success signal.
3. Console log no longer shows the previous "couldn't find image realrtcw_background.tga" warnings (or shows reduced number — assets that are truly TGA still warn if not found).
4. Clean exit on `Esc → Quit`, exit code 0.

If background is still grayscale/wrong — check Com_Printf output during boot for any "LoadJPG: ..." errors. Most common failure mode: `jpeg_mem_src` ABI mismatch between system libjpeg and the one we linked. Verify `USE_INTERNAL_JPEG` env or Makefile setting matches the actual JPGOBJ build.

### Task 8: Commit M7

- [ ] **Step 1: Stage and commit**

```bash
cd ~/fedorov_tech/RealRTCW-vulkan-wt
git add code/client/cl_jpeg.c code/qcommon/qcommon.h Makefile code/client/cl_refvulkan.c
git commit -m "$(cat <<'EOF'
feat(m7): JPG loader wired engine-side via CL_LoadJPG

Quake3e-canonical pattern: renderercommon/tr_image_jpg.c is a thin shim
that delegates R_LoadJPG → ri.CL_LoadJPG. RealRTCW previously stubbed
that slot to NULL because the engine had no libjpeg consumer (only the
legacy OpenGL renderer .dylib linked its own copy).

This commit adds code/client/cl_jpeg.c — port of code/renderer/tr_image_jpg.c
R_LoadJPG half, with ri.* → engine-side Com_/FS_/Z_Malloc substitution.
The CL_LoadJPG declaration is added to qcommon.h, the object is added
to Q3OBJ, and vk_CL_LoadJPG in cl_refvulkan.c now delegates to it.

Closes 3 of 4 M6-smoke non-regressions:
- realrtcw_background.jpg renders in main menu (was: placeholder texture)
- "asset not found realrtcw_background.tga" warning gone (was: search-loop
  fell through to JPG which stubbed-out)
- "grayscale/blurred first frame" symptom gone (was: same placeholder)

Zero vendor edits — all code lives in RealRTCW-owned dirs (code/client/,
code/qcommon/). Legacy OpenGL renderer's libjpeg copy stays untouched
(separate symbol space in the .dylib).

Smoke: scripts/mac/playtest.sh --vulkan reaches main menu, background
image visible as cover art, clean exit 0.

Plan: notes/plans/2026-06-12-m7-jpg-loader-and-m8-shader-keywords.md
EOF
)"
```

- [ ] **Step 2: Verify**

```bash
git log --oneline -1
# Expected: <hash> feat(m7): JPG loader wired engine-side via CL_LoadJPG
```

---

## M7.5: Fog rendering investigation

No code. Single-session manual playtest + write decision-note.

### Task 9: Identify a fog-heavy RTCW SP map

RTCW SP shipped maps with prominent fog effects. From [[q3-engine-archaeology]] and pak0.pk3 inspection — typical fog-heavy maps:
- `escape1` (Castle Wolfenstein escape, cellar/dungeon fog)
- `crypt1` / `crypt2` (catacombs)
- `forest` (foggy outdoor)
- `village1` (overcast/atmospheric)

- [ ] **Step 1: Pick `escape1` (canonical first map, heavy fog in the first cellar/cave sections)**

If `escape1` is unavailable in installed assets, fallback: try `village1` or any map ragnar suggests as fog-prominent.

### Task 10: Capture OpenGL baseline screenshot

- [ ] **Step 1: Brief ragnar to load OpenGL build**

```bash
cd ~/fedorov_tech/RealRTCW-vulkan-wt && scripts/mac/playtest.sh
# (no --vulkan flag → OpenGL renderer)
```

In console (`~` to open):
```
\map escape1
```

Walk to a fog-prominent location (e.g., first cave area after the opening cellar).
Press F11 or use `\screenshot` from console.

Screenshot saves to `~/Library/Application Support/RtCW/screenshots/` or similar.

- [ ] **Step 2: Note camera coordinates for repeatability**

In console:
```
\viewpos
```

Record output (e.g., `Viewpos: (528 -160 24) : 0 90 0`). M7.5 step 11 will use the same viewpos in Vulkan.

### Task 11: Capture Vulkan comparison screenshot

- [ ] **Step 1: Brief ragnar to load Vulkan build at same viewpos**

```bash
cd ~/fedorov_tech/RealRTCW-vulkan-wt && scripts/mac/playtest.sh --vulkan
```

In console:
```
\map escape1
\setviewpos 528 -160 24 0 90 0   # use Task 10 step 2 values
\screenshot
```

### Task 12: Compare visuals + write decision-note

- [ ] **Step 1: Visual diff**

Open both screenshots side-by-side. Look for:
- **Fog visible in OpenGL screenshot.** Volumetric/depth-based fade at distance, color tinted (RTCW typical: grey/blue).
- **Fog visible in Vulkan screenshot.** Same fade pattern, similar color.

Classify:
- **(a) Vulkan shows fog same as OpenGL** → renderervk fog pipeline IS active in RealRTCW. M8 = Branch B (full noFog port).
- **(b) Vulkan shows NO fog** (clear sky/depth, no fade) → renderervk fog pipeline is dormant. M8 = Branch A (FS-layer silencer).
- **(c) Vulkan shows wrong fog** (different color, broken pattern) → record finding, default to Branch A (silencer covers all 3 keywords without making the wrong fog issue worse).

- [ ] **Step 2: Write `notes/decisions/2026-06-12-m75-fog-investigation.md`**

```markdown
# M7.5 fog investigation (2026-06-12)

Single-session manual investigation, no code changes. Determines M8 shape.

## Setup

Tested on `escape1` (first RTCW SP map). OpenGL baseline + Vulkan comparison at viewpos `<X Y Z> <pitch yaw roll>`. Screenshots: `screenshots/m75-opengl.jpg`, `screenshots/m75-vulkan.jpg`.

## Result

[FILL IN — one of (a), (b), or (c). Include a short prose description of what was visually observed.]

## M8 implication

[FILL IN — Branch A or Branch B, with one-sentence reasoning.]

## Cross-links

- [[project-m6-types-unification]] — M6 baseline this builds on.
- `notes/plans/2026-06-12-m7-jpg-loader-and-m8-shader-keywords.md` — the plan.
```

- [ ] **Step 3: Commit M7.5 decision**

```bash
cd ~/fedorov_tech/RealRTCW-vulkan-wt
git add notes/decisions/2026-06-12-m75-fog-investigation.md
git add screenshots/m75-opengl.jpg screenshots/m75-vulkan.jpg 2>/dev/null || true
git commit -m "$(cat <<'EOF'
docs(m7.5): fog rendering investigation - renderervk on RealRTCW

Single-session manual playtest: load escape1 in OpenGL build and Vulkan
build at matching viewpos, screenshot, visually compare fog rendering.

Result: <FILL IN from decision note>

Implication for M8: <Branch A | Branch B>

Decision note: notes/decisions/2026-06-12-m75-fog-investigation.md
EOF
)"
```

---

## M8: Shader keyword handling

Shape depends on M7.5 outcome. Execute exactly one of the two branches.

### M8 Branch A — FS-layer silencer (if M7.5 says fog dormant or wrong)

**Idea.** Intercept `.shader` text at the FS boundary inside `vk_FS_ReadFile`. Comment-out lines whose first non-whitespace token is `nofog`, `nocompress`, or `allowcompress` by prefixing them with `//`. Preserves byte layout (no length change, no line-number drift). Renderervk's parser sees a comment, ignores it cleanly. Zero vendor edits.

#### Task A1: Add `vk_StripRTCWExtendedKeywords` to cl_refvulkan.c

**Files:**
- Modify: `code/client/cl_refvulkan.c` (add helper above `vk_FS_ReadFile` at line ~470)

- [ ] **Step 1: Add the helper function**

Insert before `vk_FS_ReadFile`:

```c
/* RTCW-extended shader keywords that renderervk (Quake3e parser) doesn't
 * recognize. Without intervention, encountering any of these makes
 * renderervk's ParseShader return qfalse → entire shader rejected →
 * surface renders with the default checker texture.
 *
 * We mask each occurrence at the FS boundary by prefixing the keyword
 * with "//", which the parser then skips as a comment. Byte layout is
 * preserved (no length change, no line drift). M8 — 2026-06-12.
 *
 * Why not handle in renderervk's parser directly: that's a vendor edit
 * with a permanent drift surface vs Quake3e upstream. The FS-layer mask
 * lives in the M3.5 translator and disappears the moment we re-vendor
 * a Quake3e snapshot that handles these keywords natively.
 */
static const char *vk_rtcw_extended_keywords[] = {
    "nofog",
    "nocompress",
    "allowcompress",
    NULL
};

static qboolean vk_TokenMatches( const char *p, const char *token ) {
    size_t n = strlen( token );
    if ( Q_strncmp( p, token, n ) != 0 )
        return qfalse;
    /* Must be followed by whitespace or end-of-line to be a token, not a
     * prefix of a longer identifier. */
    char trailing = p[n];
    return ( trailing == '\0' || trailing == ' ' || trailing == '\t'
          || trailing == '\r' || trailing == '\n' ) ? qtrue : qfalse;
}

static void vk_StripRTCWExtendedKeywords( char *text, int len ) {
    int i = 0;
    qboolean in_block_comment = qfalse;

    while ( i < len ) {
        /* Track /* */ block comments — never mask inside them. */
        if ( !in_block_comment && i + 1 < len && text[i] == '/' && text[i+1] == '*' ) {
            in_block_comment = qtrue;
            i += 2;
            continue;
        }
        if ( in_block_comment ) {
            if ( i + 1 < len && text[i] == '*' && text[i+1] == '/' ) {
                in_block_comment = qfalse;
                i += 2;
                continue;
            }
            i++;
            continue;
        }

        /* Find start of next line (or current position if at line start). */
        int line_start = i;
        while ( line_start > 0 && text[line_start - 1] != '\n' )
            line_start--;

        /* Skip leading whitespace on the line. */
        int p = line_start;
        while ( p < len && ( text[p] == ' ' || text[p] == '\t' ) )
            p++;

        /* If we've passed our current position trying to find a line start
         * AHEAD of i, the math is wrong — recompute by scanning forward. */
        if ( p < i ) {
            /* Move i to the next newline + 1 and retry. */
            while ( i < len && text[i] != '\n' ) i++;
            if ( i < len ) i++;
            continue;
        }

        /* Check each RTCW-extended keyword at position p. */
        qboolean matched = qfalse;
        for ( const char **kw = vk_rtcw_extended_keywords; *kw; kw++ ) {
            if ( vk_TokenMatches( &text[p], *kw ) ) {
                /* Mask by overwriting first two chars with "//". This
                 * guarantees the keyword becomes part of a comment from
                 * here to end-of-line. */
                if ( p + 1 < len ) {
                    text[p] = '/';
                    text[p+1] = '/';
                }
                matched = qtrue;
                break;
            }
        }
        (void)matched;

        /* Advance to next line. */
        while ( i < len && text[i] != '\n' ) i++;
        if ( i < len ) i++;
    }
}
```

#### Task A2: Wire the strip into `vk_FS_ReadFile`

**Files:**
- Modify: `code/client/cl_refvulkan.c:472-474` (the existing `vk_FS_ReadFile`)

- [ ] **Step 1: Wrap the read**

Change:

```c
static int vk_FS_ReadFile( const char *qpath, void **buffer ) {
    return (int)FS_ReadFile( qpath, buffer );
}
```

to:

```c
static int vk_FS_ReadFile( const char *qpath, void **buffer ) {
    int len = (int)FS_ReadFile( qpath, buffer );
    if ( len > 0 && buffer && *buffer && qpath ) {
        /* Mask RTCW-extended shader keywords for .shader files only. */
        size_t qpath_len = strlen( qpath );
        if ( qpath_len > 7 && Q_stricmp( qpath + qpath_len - 7, ".shader" ) == 0 ) {
            vk_StripRTCWExtendedKeywords( (char *)*buffer, len );
        }
    }
    return len;
}
```

#### Task A3: Build + clean

- [ ] **Step 1: Wipe stale objects in client**

```bash
cd ~/fedorov_tech/RealRTCW-vulkan-wt
rm -rf build/release-darwin-arm64/client/cl_refvulkan.o
```

- [ ] **Step 2: Build**

```bash
USE_INTERNAL_LIBS=0 BUILD_RENDERER_VULKAN=1 make -j8 release 2>&1 | tail -20
```

#### Task A4: Smoke (USER-DRIVEN)

- [ ] **Step 1: Brief ragnar**

He runs `scripts/mac/playtest.sh --vulkan`, loads a fog-heavy map (`\map escape1` or his choice), inspects:
- No more "unknown general shader parameter 'nofog'" warnings in log
- Surfaces that previously rendered as default checker now render with intended textures
- Visual regression check: nothing previously-working got worse

#### Task A5: Commit M8 Branch A

- [ ] **Step 1: Stage and commit**

```bash
cd ~/fedorov_tech/RealRTCW-vulkan-wt
git add code/client/cl_refvulkan.c
git commit -m "$(cat <<'EOF'
feat(m8): mask RTCW-extended shader keywords at FS boundary

renderervk's Quake3e-vintage shader parser rejects entire shaders that
contain `nofog`, `nocompress`, or `allowcompress` — those keywords hit
the "unknown general shader parameter" branch which returns qfalse and
falls back to the default checker texture for any surface using them.

Masking the keywords at the FS-read boundary inside the vk_FS_ReadFile
translator (cl_refvulkan.c) prefixes each occurrence with `//`, turning
the keyword into a comment that the parser cleanly skips. Byte layout
preserved (no length change, no line-number drift).

This is the structurally honest fix given M7.5's finding that
renderervk's fog pipeline is <dormant|wrong-shape> in RealRTCW (see
notes/decisions/2026-06-12-m75-fog-investigation.md). With no fog
rendering anyway, the `nofog` keyword has no visual referent and
silently dropping it is observationally inert. `nocompress` is similarly
inert because renderervk has no texture compression path at boot.

Zero vendor edits — all logic in cl_refvulkan.c which is the existing
M3.5 translator layer. Disappears for free the moment we re-vendor a
Quake3e snapshot that handles these keywords natively.

Plan: notes/plans/2026-06-12-m7-jpg-loader-and-m8-shader-keywords.md
EOF
)"
```

---

### M8 Branch B — full noFog port (if M7.5 says fog active and correct)

**Idea.** Add `qboolean noFog` to renderervk's `shader_t`, parse the three keywords in renderervk's `tr_shader.c`, port the four read sites from legacy `tr_shade.c`. Vendor edit acknowledged in decision-note.

**REQUIRES:** `REALRTCW_ALLOW_VENDOR_EDIT=1` in claude's launch env. If unset, stop and request relaunch.

#### Task B1: Verify hook env

- [ ] **Step 1: Check env**

```bash
echo "REALRTCW_ALLOW_VENDOR_EDIT=${REALRTCW_ALLOW_VENDOR_EDIT:-UNSET}"
```

If unset → block, ask ragnar to relaunch claude with the env var.

#### Task B2: Add `qboolean noFog` to renderervk `shader_t`

**Files:**
- Modify: `code/renderervk/tr_local.h` (around line 467 where `fogPass_t fogPass` lives)

- [ ] **Step 1: Insert field**

After the `fogPass` line:

```c
	fogPass_t	fogPass;				// draw a blended pass, possibly with depth test equals
	qboolean	noFog;					// RTCW-extended: suppress per-stage fog rendering (M8 vendor edit)
```

#### Task B3: Parse the three keywords in renderervk

**Files:**
- Modify: `code/renderervk/tr_shader.c:2049` (the unknown-keyword fall-through)

- [ ] **Step 1: Insert keyword cases**

Change at line ~2049 from:

```c
			else
			{
				ri.Printf( PRINT_WARNING, "WARNING: unknown general shader parameter '%s' in '%s'\n", token, shader.name );
				return qfalse;
			}
```

to:

```c
			/* M8 vendor edit: RTCW-extended keywords. `nofog` sets the
			 * per-shader noFog flag read in tr_shade.c stage-iter sites.
			 * `nocompress`/`allowcompress` are no-ops in renderervk
			 * (no texture-compression path exists), accepted silently
			 * to avoid shader rejection. */
			else if ( !Q_stricmp( token, "nofog" ) ) {
				shader.noFog = qtrue;
				continue;
			}
			else if ( !Q_stricmp( token, "nocompress" )
			       || !Q_stricmp( token, "allowcompress" ) ) {
				continue;
			}
			else
			{
				ri.Printf( PRINT_WARNING, "WARNING: unknown general shader parameter '%s' in '%s'\n", token, shader.name );
				return qfalse;
			}
```

#### Task B4: Port the four read sites in renderervk `tr_shade.c`

**Files:**
- Modify: `code/renderervk/tr_shade.c` — port the noFog conditional from legacy `code/renderer/tr_shade.c:418/420/1251/1253`

- [ ] **Step 1: Find the equivalent sites in renderervk**

The legacy sites are inside `ComputeColors` (line ~418/420) and `RB_StageIteratorGeneric` (line ~1251/1253). In renderervk the equivalent functions exist but file structure may differ — grep for these patterns:

```bash
grep -n "tess.fogNum\|tess\.shader.*fogPass\|isFogged" code/renderervk/tr_shade.c | head -20
```

For each site found, if it processes per-stage fog and uses `tess.shader->fogPass` decisions, add the `noFog` short-circuit before the fog application. Mirror legacy structure as closely as possible.

This step requires careful reading — list each insertion in the commit message. Estimate: 30-60 minutes of careful porting.

#### Task B5: Build + smoke + commit

- [ ] Same shape as Branch A's A3/A4/A5, but commit message reflects vendor edit + four-site port + REALRTCW_ALLOW_VENDOR_EDIT acknowledgement.

---

## Final ritual

### Task 13: Update memory

Per [[codify-findings]] skill (loaded explicitly if needed):
- Update `[[project-m6-types-unification]]` to note M7+M7.5+M8 sequel landed
- Add new memory `[[project-m7-jpg-loader-closed]]` and `[[project-m8-shader-keywords-<a|b>-closed]]`

### Task 14: Write handoff to .remember/remember.md

Use `remember` skill. Cover:
- M6/M7/M7.5/M8 all closed (which branch of M8)
- Build still green, smoke still passing
- Next direction candidates from this session's exit state
- intShaderTime ABI gap still open as post-M6 work

### Task 15: NO PUSH

Per [[feedback-push-gating]] — do NOT push to origin without explicit ragnar OK. Commits land locally. Final commit count and visual proof reported to ragnar; he decides push.

---

## Self-review

**Spec coverage:**
- M7 (JPG): Tasks 1-8 cover create-file, declare, makefile, wire, clean, build, smoke, commit. ✓
- M7.5 (investigation): Tasks 9-12 cover map pick, baseline screenshot, comparison screenshot, decision-note. ✓
- M8 (Branch A): Tasks A1-A5 cover helper, wire, build, smoke, commit. ✓
- M8 (Branch B): Tasks B1-B5 cover env check, field add, parser change, backend port, build/smoke/commit. ✓
- Final ritual: Tasks 13-15 cover memory, handoff, no-push. ✓

**Placeholder scan:**
- M8 Branch B Task B4 says "list each insertion in the commit message" — that's instruction to the executor, not a placeholder for the executor to fill at runtime. Acceptable.
- M7.5 Tasks 10-12 contain `<FILL IN>` markers — those are INTENDED to be filled at runtime during the actual investigation. That's the whole point of M7.5 being a discovery milestone.

**Type consistency:**
- `CL_LoadJPG` signature `(const char *, unsigned char **, int *, int *)` matches the refImport slot declared in `code/renderercommon/tr_public.h:218`. ✓
- `vk_FS_ReadFile` signature `(const char *, void **) → int` preserved from existing code. ✓
- `vk_StripRTCWExtendedKeywords` signature `(char *, int) → void` consistent throughout. ✓
