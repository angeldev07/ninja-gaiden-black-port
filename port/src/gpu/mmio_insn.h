/*
 * mmio_insn.h: decoder for the few x86 instructions that D3D uses on the NV2A registers.
 *
 * The VEH (nv2a.c, fast_access) emulates them in place, with one exception and no system
 * call, instead of opening the register page and single-stepping the instruction. The forms:
 *
 *   8B /r        mov r32, r/m32         load
 *   89 /r        mov r/m32, r32         store of a register
 *   C7 /0 id     mov r/m32, imm32       store of an immediate
 *   F7 /0 id     test r/m32, imm32      load that only sets the flags
 *   A1 od        mov eax, moffs32       load
 *   A3 od        mov moffs32, eax       store of a register
 *
 * Only the length and the register or immediate operand are decoded: the address comes from
 * the fault itself. Prefixes (operand size, segment, lock) and register-only forms are not
 * accepted, and those accesses take the single-step path.
 *
 * The decoder is pure; it is tested in port/tests/mmio_insn_test.c.
 */
#pragma once

#include <stdint.h>

typedef enum { MMIO_LOAD, MMIO_STORE_REG, MMIO_STORE_IMM, MMIO_TEST_IMM } mmio_op_t;

typedef struct mmio_insn {
    mmio_op_t op;
    int reg;      /* register operand (0 = EAX ... 7 = EDI), for MMIO_LOAD and MMIO_STORE_REG */
    uint32_t imm; /* immediate, for MMIO_STORE_IMM and MMIO_TEST_IMM */
} mmio_insn_t;

/* Decodes the instruction at p: its length in bytes, or 0 if it is not one of the forms. */
uint32_t mmio_insn_decode(const uint8_t *p, mmio_insn_t *out);

/* EFLAGS after TEST with the given result: CF and OF clear; ZF, SF and PF from the result.
 * AF is left clear, as on the processors D3D ran on (it is undefined after TEST). */
uint32_t mmio_insn_test_flags(uint32_t eflags, uint32_t result);
