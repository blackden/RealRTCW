/*
 * RealRTCW shims for vendored Quake3e renderervk/renderercommon.
 *
 * Included via Makefile's -include flag before every vendored source file
 * is compiled. NEVER include this from engine-side code.
 *
 * Edit policy: this file IS RealRTCW-authored (filename prefix
 * realrtcw_ signals that to the vendor-guard hook), so editing is
 * unblocked. See docs/vulkan-phase2-abi-diff.md for the drift catalog
 * each entry below comes from.
 */
#ifndef REALRTCW_RENDERERVK_SHIMS_H
#define REALRTCW_RENDERERVK_SHIMS_H

/* Macro shims — see abi-diff doc Section 1. */
/* (filled in Task 2.2) */

/* Typedef / enum shims — see abi-diff doc Section 2. */
/* (filled in Task 2.2) */

/* Inline function shims — see abi-diff doc Section 3 where shim mechanism
 * is "static inline in header". */
/* (filled in Task 2.3) */

/* Function shim prototypes — implementations live in realrtcw_engine_glue.c.
 * See abi-diff doc Section 3 where shim mechanism is "wrapper in glue file". */
/* (filled in Task 2.3, 2.4) */

/* Vtable adapter types — Quake3e-style refimport_t typedef that
 * realrtcw_engine_glue.c populates from RealRTCW functions. */
/* (filled in Task 2.4) */

#endif /* REALRTCW_RENDERERVK_SHIMS_H */
