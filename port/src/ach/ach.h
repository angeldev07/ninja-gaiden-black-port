/*
 * ach.h: port-side achievements (feasibility spike, see docs/target/achievements-brief.md).
 *
 * The game is never patched: the main thread reads a few of the game's variables every 5 ms,
 * turns their changes into events ("chapter N complete") and records each unlock in
 * %NGB_DATA%\achievements.json, which the launcher reads. The presenter draws a toast
 * (gfx/toast.c) over the frame.
 *
 * The variables are located by address in the reference build only (PAL default.xbe, title id
 * 0x5443000D); ngb_ach_init() checks a known string at a known address and switches the
 * detectors off on any other build.
 *
 *   ngb_ach_init(&xbe, demo);   // after the XBE is mapped
 *   ngb_ach_tick();             // main thread, every few ms
 */
#ifndef NGB_ACH_H
#define NGB_ACH_H

#include "../xbe.h"

/* `demo` = 1 (--ach-demo): a test unlock 5 s after start, to check the toast and the file
 * without playing. */
void ngb_ach_init(const xbe_t *xbe, int demo);
void ngb_ach_tick(void);

#endif
