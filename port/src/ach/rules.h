/*
 * rules.h: the game's own end-of-chapter rank and the bookkeeping of the rank achievements. Pure
 * functions, tested by port/tests/ach_rules_test.c. Formula from FUN_002ed790 (PAL), see
 * docs/reverse-engineering/game-state.md.
 */
#ifndef NGB_ACH_RULES_H
#define NGB_ACH_RULES_H

#include <stdint.h>

#define ACH_RANK_MASTER_NINJA 0
#define ACH_DIFFICULTIES 5 /* Ninja Dog, Normal, Hard, Very Hard, Master Ninja */
#define ACH_CHAPTERS 16

/* The rank the game shows, 0 (Master Ninja) to 4, for the karma earned in a chapter and that
 * chapter's {max, min} pair. Difficulty 0 (Ninja Dog) caps it at 2. */
int ach_rank(uint32_t chapter_karma, uint32_t max, uint32_t min, int difficulty);

/* Chapters set in a 16-bit mask (bit n = chapter n + 1). */
int ach_seal_count(uint32_t mask);

/* Mission mode: the 46 missions of the mission menu, nine pages of five and "Eternal Legend" on
 * its own (the menu table, PAL 0x00469668, is the same in every release). */
#define ACH_MISSIONS 46
#define ACH_MISSION_ETERNAL_LEGEND 50

/* The mission's place in the menu (0 to 45), or -1 for an index the menu does not list. */
int ach_mission_slot(int mission);

/* Bits set in a 64-bit mask. */
int ach_bit_count(uint64_t mask);

/* A story run followed from New Game for the challenge achievements: what has happened in it
 * that rules them out. */
#define ACH_RUN_HIT 1u          /* health went down */
#define ACH_RUN_HEAL 2u         /* a healing or ki item was used */
#define ACH_RUN_NINPO 4u        /* ki went down (ninpo cast) */
#define ACH_RUN_UPGRADE 8u      /* max health or max ki went up */
#define ACH_RUN_OTHER_WEAPON 16u /* a melee weapon other than the Dragon Sword in hand */

/* What the run tracker reads each tick. */
typedef struct ach_body {
    unsigned health, health_max, ki, ki_max; /* 0 max: not in play (loading, menus) */
    unsigned weapon;                         /* melee item id, 0xFFFF none */
    unsigned heal_items;                     /* healing and ki items carried, summed */
} ach_body;

/* The ACH_RUN_* bits that the change from `a` to `b` (two reads in a row while playing) sets. */
unsigned ach_run_marks(const ach_body *a, const ach_body *b);

/* 1 for the Dragon Sword's item ids (0x01, and 0xC9 for the True Dragon Sword). */
int ach_is_dragon_sword(unsigned item);
/* 1 for the items that restore health or ki (elixirs, fragrances, Ayane's food, the talisman). */
int ach_is_healing_item(unsigned item);
/* 1 for the fire ninpo: The Art of the Fire Wheels (0x9A) and The Art of the Inferno (0x0F, 0x9B,
 * 0x9C, one id per level); not the Ice Storm (0x9E-0xA0) nor the Inazuma (0xAC-0xAE). */
int ach_is_fire_ninpo(unsigned item);

#endif
