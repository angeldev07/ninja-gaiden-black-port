/*
 * mmio_insn.c: decoder for the x86 instructions D3D uses on the NV2A registers (mmio_insn.h).
 */
#include "mmio_insn.h"

#include <string.h>

#define EFL_CF 0x001u
#define EFL_PF 0x004u
#define EFL_AF 0x010u
#define EFL_ZF 0x040u
#define EFL_SF 0x080u
#define EFL_OF 0x800u

uint32_t mmio_insn_decode(const uint8_t *p, mmio_insn_t *out) {
    uint32_t len;
    int mod, rm;
    memset(out, 0, sizeof *out);
    switch (p[0]) {
    case 0x8B: out->op = MMIO_LOAD; break;
    case 0x89: out->op = MMIO_STORE_REG; break;
    case 0xC7: out->op = MMIO_STORE_IMM; break;
    case 0xF7: out->op = MMIO_TEST_IMM; break;
    case 0xA1: out->op = MMIO_LOAD; return 5; /* EAX, 32-bit offset */
    case 0xA3: out->op = MMIO_STORE_REG; return 5;
    default: return 0;
    }
    mod = p[1] >> 6;
    out->reg = (p[1] >> 3) & 7;
    rm = p[1] & 7;
    if (mod == 3) /* register operand: no memory access */
        return 0;
    if ((out->op == MMIO_STORE_IMM || out->op == MMIO_TEST_IMM) && out->reg != 0)
        return 0; /* C7 /1-7 are invalid; F7 /2-7 are NOT, NEG, MUL, IMUL, DIV, IDIV */
    len = 2;
    if (rm == 4) { /* SIB byte; base 5 with mod 0 means a 32-bit displacement and no base */
        if (mod == 0 && (p[2] & 7) == 5)
            len += 4;
        len++;
    } else if (mod == 0 && rm == 5) { /* 32-bit absolute address */
        len += 4;
    }
    if (mod == 1)
        len += 1;
    else if (mod == 2)
        len += 4;
    if (out->op == MMIO_STORE_IMM || out->op == MMIO_TEST_IMM) {
        memcpy(&out->imm, p + len, 4);
        out->reg = 0;
        len += 4;
    }
    return len;
}

uint32_t mmio_insn_test_flags(uint32_t eflags, uint32_t result) {
    uint32_t low = result & 0xFF;
    low ^= low >> 4;
    low ^= low >> 2;
    low ^= low >> 1;
    eflags &= ~(EFL_CF | EFL_PF | EFL_AF | EFL_ZF | EFL_SF | EFL_OF);
    if (!(low & 1))
        eflags |= EFL_PF; /* even number of set bits in the low byte */
    if (result == 0)
        eflags |= EFL_ZF;
    if (result & 0x80000000u)
        eflags |= EFL_SF;
    return eflags;
}
