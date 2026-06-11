/*
===========================================================================
RealRTCW source code is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as published
by the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
===========================================================================
*/
/*
 * Shared single-source declaration of SDL_window, the engine-resident
 * SDL_Window pointer. Defined exactly once in code/sdl/sdl_glimp.c.
 * Consumed by sdl_input.c and any other engine TU that needs to query
 * the current main window.
 *
 * γ' migration (notes/decisions/2026-06-10-m4-window-ownership-model.md):
 * before γ', SDL_window had two definitions — one in each renderer DLL's
 * sdl_glimp.c, plus a third in sdl_input.c. This caused subtle
 * cross-globals bugs (M3.5 dealt with several). After γ', a single
 * engine-side SDL_window is the source of truth; this header is how
 * other engine TUs see it.
 */

#ifndef __SDL_GLW_H__
#define __SDL_GLW_H__

#ifdef USE_LOCAL_HEADERS
#	include "SDL3/SDL.h"
#else
#	include <SDL3/SDL.h>
#endif

extern SDL_Window *SDL_window;

#endif /* __SDL_GLW_H__ */
