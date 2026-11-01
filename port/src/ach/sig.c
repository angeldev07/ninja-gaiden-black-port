/*
 * sig.c: see sig.h.
 *
 * The save name routine (PAL FUN_001dab20) reads both the mode and the chapter:
 *     mov al, [mode]  ...  cmp al, 2  ...  movzx eax, byte [chapter]  ...  push L"%d. CHAPTER %d %s"
 * and the load routine (PAL FUN_002fe3c0) restores the chapter from the save block:
 *     mov cl, [saved]  ...  mov [chapter], cl
 * The save block has the same layout in every release (saves of one load in another), so the
 * difficulty, the mode and the karma sit at fixed distances from the saved chapter; the mode's
 * distance cross-checks the first two finds. The rank data pointer keeps its distance to the
 * chapter in the builds seen (PAL and NTSC-U); the caller checks what it points to. So does the
 * stage end state, which is only taken when the code stores 0x80 (game over) into it, as the PAL
 * routines that end a stage do (FUN_002e3f60, FUN_002ff940).
 */
#include "sig.h"

#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }

/* First `pat` in [from, to) of `hay`, or -1. */
static long find(const uint8_t *hay, size_t from, size_t to, const uint8_t *pat, size_t n) {
    size_t i;
    for (i = from; i + n <= to; i++)
        if (hay[i] == pat[0] && !memcmp(hay + i, pat, n))
            return (long)i;
    return -1;
}

/* Stores of 0x80 into `va` (mov byte [imm32], 0x80) in the code. */
static int stores_of_0x80(const uint8_t *t, uint32_t tsize, uint32_t va) {
    uint8_t pat[7] = {0xC6, 0x05, 0, 0, 0, 0, 0x80};
    long at;
    int n = 0;
    memcpy(pat + 2, &va, 4);
    for (at = find(t, 0, tsize, pat, 7); at >= 0; at = find(t, (size_t)at + 1, tsize, pat, 7))
        n++;
    return n;
}

int ach_find_vars(const xbe_t *xbe, ach_vars *out) {
    static const wchar_t fmt[] = L"%d. CHAPTER %d %s";
    const xbe_section_t *text = NULL;
    const uint8_t *t;
    uint32_t fmt_va = 0, s, tsize = 0;
    uint8_t pat[6];
    long at, i;
    memset(out, 0, sizeof *out);
    for (s = 0; s < xbe->section_count; s++) {
        const xbe_section_t *sec = &xbe->sections[s];
        if (!strcmp(sec->name, ".text"))
            text = sec;
        if (!fmt_va && (uint64_t)sec->raw + sec->rsize <= xbe->size &&
            (at = find(xbe->data, sec->raw, sec->raw + sec->rsize, (const uint8_t *)fmt, sizeof fmt)) >= 0)
            fmt_va = sec->va + (uint32_t)at - sec->raw;
    }
    if (text)
        tsize = text->rsize < text->vsize ? text->rsize : text->vsize;
    if (!fmt_va || tsize < 0x60 || (t = xbe_va_ptr(xbe, text->va, tsize)) == NULL)
        return -1;
    pat[0] = 0x68; /* push imm32 */
    memcpy(pat + 1, &fmt_va, 4);
    if ((at = find(t, 0x60, tsize, pat, 5)) < 0 || find(t, (size_t)at + 1, tsize, pat, 5) >= 0)
        return -1;
    for (i = at - 7; i >= at - 0x20 && !out->chapter; i--)
        if (t[i] == 0x0F && t[i + 1] == 0xB6 && t[i + 2] == 0x05) /* movzx eax, byte [imm32] */
            out->chapter = rd32(t + i + 3);
    for (i = at - 0x60; i < at - 5 && !out->mode; i++)
        if (t[i] == 0xA0 && find(t, (size_t)i + 5, (size_t)i + 13, (const uint8_t *)"\x3C\x02", 2) >= 0)
            out->mode = rd32(t + i + 1); /* mov al, [imm32] followed by cmp al, 2 */
    if (!out->chapter || !out->mode)
        return -1;
    pat[0] = 0x88; /* mov [imm32], cl */
    pat[1] = 0x0D;
    memcpy(pat + 2, &out->chapter, 4);
    for (at = find(t, 0x30, tsize, pat, 6); at >= 0 && !out->saved; at = find(t, (size_t)at + 1, tsize, pat, 6))
        for (i = at - 6; i >= at - 0x30; i--)
            if (t[i] == 0x8A && t[i + 1] == 0x0D && rd32(t + i + 2) + 3 == out->mode) { /* mov cl, [imm32] */
                out->saved = rd32(t + i + 2);
                break;
            }
    if (!out->saved) {
        memset(out, 0, sizeof *out);
        return -1;
    }
    out->difficulty = out->saved - 2;
    out->karma = out->saved - 0x1F7;
    out->karma_last = out->saved - 0x1F3;
    out->rank_data = out->chapter - 0x210;
    out->play_time = out->saved - 0x1EF;
    out->scarabs = out->saved - 0x1E3;
    out->mission = out->saved + 1;
    if (stores_of_0x80(t, tsize, out->chapter - 0x240) >= 4)
        out->stage_end = out->chapter - 0x240;
    return 0;
}
