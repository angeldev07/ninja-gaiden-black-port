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

#endif
