/*
 * ach.h: port-side achievements (docs/target/achievements-design.md).
 *
 * The game is never patched: the loader's main thread reads a few of the game's variables every
 * 5 ms (ngb_ach_tick), turns their changes into unlocks, records them in
 * %NGB_DATA%\achievements.json (store.c) and queues a toast (gfx/toast.c). The variables are found
 * in the game's own code at start (sig.c), so the same detectors work on PAL, NTSC-U and modified
 * builds of either; when the code is not there, the detectors stay off and the log says so.
 *
 *   ngb_ach_init(&xbe);       // after the XBE is mapped
 *   ngb_ach_tick();           // main thread, every few ms
 *   ngb_ach_screenshot();     // F12 saved a shot
 *   ngb_ach_shutdown();       // from ngb_stop: saves the play time
 */
#ifndef NGB_ACH_H
#define NGB_ACH_H

#include "../xbe.h"

void ngb_ach_init(const xbe_t *xbe);
void ngb_ach_tick(void);
void ngb_ach_screenshot(void);
void ngb_ach_shutdown(void);

#endif
