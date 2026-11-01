/*
 * rules.c: see rules.h.
 */
#include "rules.h"

int ach_rank(uint32_t karma, uint32_t max, uint32_t min, int difficulty) {
    static const int ninja_dog[5] = {2, 3, 3, 4, 4};
    uint32_t third = max > min ? (max - min) / 3 : 0;
    int rank;
    if (karma < min)
        rank = 4;
    else if (karma < min + third)
        rank = 3;
    else if (karma < min + third * 2)
        rank = 2;
    else if (karma < min + third * 3) /* the game's own bound, a little under max when it rounds */
        rank = 1;
    else
        rank = 0;
    return difficulty == 0 ? ninja_dog[rank] : rank;
}

int ach_seal_count(uint32_t mask) {
    int n = 0;
    for (mask &= 0xFFFFu; mask; mask &= mask - 1)
        n++;
    return n;
}
