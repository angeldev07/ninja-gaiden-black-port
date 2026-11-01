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
    uint32_t play_time;  /* u32, game frames played in the run, 60 a second (PAL 0x005B4788) */
    uint32_t scarabs;    /* u32, Golden Scarabs handed to Muramasa, at most 50 (PAL 0x005B4794) */
    uint32_t mission;    /* u8, the mission being played in mission mode (PAL 0x005B4978) */
    uint32_t inventory;  /* 128 x {u16 item, u8 count, u8}, 0xFFFF empty (PAL 0x005B43F0) */
    uint32_t kills;      /* u32, enemies defeated in the run (PAL 0x005B4614) */
    uint32_t weapon;     /* u16, the melee weapon in hand, an item id (PAL 0x005B47DA) */
    uint32_t health;     /* u16 max health, then u16 health, of player 1 (PAL 0x005B5784) */
    uint32_t ninpo;      /* u16[8] ninpo charges, the max 0x10 bytes lower (PAL 0x005B5754) */
    uint32_t ki;         /* u16 max ki, then u16 ki (PAL 0x005B578C) */
    uint32_t player;     /* ptr to player 1's object: +4 the action, +0xBBE "defeated one" (PAL 0x00E1349C) */
    uint32_t checks;     /* u8[24], the game's "changed on purpose" flags of its anti-tamper sums
                          * (PAL 0x0081CCB4, FUN_00164db0); 0 if not found */
    uint32_t stage_end;  /* u8: 0x80 game over, 0x81 cleared (PAL 0x00E12E94); 0 if not found */
} ach_vars;

/* 0 and every field set, or -1 if the code is not there (another game, or a build that
 * changed those routines). */
int ach_find_vars(const xbe_t *xbe, ach_vars *out);

#endif
