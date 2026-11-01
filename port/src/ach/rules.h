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

#endif
