/*
 * sig.h: finding the game variables the achievements read, in the game's own code. Their
 * addresses move between releases (PAL, NTSC-U and modified builds), so they are read out of two
 * routines at start instead of a table per release. See docs/reverse-engineering/game-state.md.
 * Tested with synthetic code by port/tests/ach_sig_test.c.
 */
#ifndef NGB_ACH_SIG_H
#define NGB_ACH_SIG_H

#include <stdint.h>

#include "../xbe.h"

typedef struct ach_vars {
    uint32_t chapter;    /* u8, 0-based; set when a chapter starts (PAL 0x00E130D4) */
    uint32_t saved;      /* u8, its copy in the save block; a load restores it (PAL 0x005B4977) */
    uint32_t difficulty; /* u8: 0 Ninja Dog .. 4 Master Ninja (PAL 0x005B4975) */
    uint32_t mode;       /* u8: 1 story, 2 to 4 missions (PAL 0x005B497A) */
    uint32_t karma;      /* u32, karma of the run so far (PAL 0x005B4780) */
    uint32_t karma_last; /* u32, the karma at the last chapter end (PAL 0x005B4784) */
    uint32_t rank_data;  /* ptr to the game data file with the rank table (PAL 0x00E12EC4) */
} ach_vars;

/* 0 and every field set, or -1 if the code is not there (another game, or a build that
 * changed those routines). */
int ach_find_vars(const xbe_t *xbe, ach_vars *out);

#endif
