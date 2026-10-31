/*
 * rules.c: see rules.h.
 */
#include "rules.h"

#include <stddef.h>

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

int ach_seal_count(uint32_t mask) { return ach_bit_count(mask & 0xFFFFu); }

int ach_bit_count(uint64_t mask) {
    int n = 0;
    for (; mask; mask &= mask - 1)
        n++;
    return n;
}

int ach_is_dragon_sword(unsigned item) { return item == 0x01 || item == 0xC9; }

int ach_is_healing_item(unsigned item) {
    /* Item ids from the game's messtr.mes (docs/reverse-engineering/game-state.md § Inventory). */
    static const unsigned short heal[] = {
        0xBE, 0xBF,          /* Elixir of Spiritual Life, Great Spirit Elixir */
        0xC0, 0xC1,          /* Elixir of the Devil Way, Great Devil Elixir */
        0x17E, 0x181,        /* Fragrance of Hydrangea, of Dayflower */
        0x172, 0x180,        /* Ayane's Rations, Ration Bundle */
        0x187, 0x188, 0x189, /* Ayane's Chestnut, Walnut, Cherry */
        0xFB,                /* The Talisman of Rebirth */
    };
    size_t i;
    for (i = 0; i < sizeof heal / sizeof heal[0]; i++)
        if (heal[i] == item)
            return 1;
    return 0;
}

unsigned ach_run_marks(const ach_body *a, const ach_body *b) {
    unsigned m = 0;
    if (!a->health_max || !b->health_max) /* a load or a menu between the two reads */
        return 0;
    if (b->health_max > a->health_max || (a->ki_max && b->ki_max > a->ki_max))
        m |= ACH_RUN_UPGRADE;
    if (b->health_max == a->health_max && b->health < a->health)
        m |= ACH_RUN_HIT;
    if (b->ki_max == a->ki_max && b->ki < a->ki)
        m |= ACH_RUN_NINPO;
    if (b->heal_items < a->heal_items)
        m |= ACH_RUN_HEAL;
    if (b->weapon != 0xFFFF && !ach_is_dragon_sword(b->weapon))
        m |= ACH_RUN_OTHER_WEAPON;
    return m;
}

int ach_mission_slot(int mission) {
    /* The game's mission menu, page by page (PAL 0x00469668, NTSC-U 0x004474B8): mission indices
     * as the game numbers them in the profile. */
    static const unsigned char menu[ACH_MISSIONS] = {
        0x25, 0x1A, 0x24, 0x37, 0x23, 0x29, 0x14, 0x0D, 0x1B, 0x2C, 0x0F, 0x10, 0x1F, 0x12, 0x13, 0x05,
        0x07, 0x34, 0x09, 0x38, 0x1C, 0x19, 0x22, 0x2B, 0x21, 0x17, 0x16, 0x15, 0x2E, 0x31, 0x3C, 0x3E,
        0x3D, 0x3F, 0x40, 0x03, 0x0E, 0x1D, 0x04, 0x27, 0x43, 0x44, 0x42, 0x39, 0x41, ACH_MISSION_ETERNAL_LEGEND,
    };
    int i;
    for (i = 0; i < ACH_MISSIONS; i++)
        if (menu[i] == mission)
            return i;
    return -1;
}
