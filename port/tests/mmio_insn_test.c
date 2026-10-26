/*
 * mmio_insn_test.c: tests of the decoder for the instructions D3D uses on the NV2A registers.
 * The byte sequences come from D3D's KickOff and register helpers.
 * Built and run by port\build.cmd; exits with code 1 if anything fails.
 */
#include <stdio.h>

#include "../src/gpu/mmio_insn.h"

static int g_failures, g_checks;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        g_checks++;                                                                 \
        if (!(cond)) {                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

static void decodes(const uint8_t *code, uint32_t len, mmio_op_t op, int reg, uint32_t imm) {
    mmio_insn_t in;
    CHECK(mmio_insn_decode(code, &in) == len);
    CHECK(in.op == op);
    CHECK(in.reg == reg);
    CHECK(in.imm == imm);
}

static void rejects(const uint8_t *code) {
    mmio_insn_t in;
    CHECK(mmio_insn_decode(code, &in) == 0);
}

int main(void) {
    /* The write-combine flush of KickOff: load, or, store, then a test loop. */
    static const uint8_t load_disp32[] = {0x8B, 0x88, 0x10, 0x04, 0x10, 0x00};  /* mov ecx,[eax+0x100410] */
    static const uint8_t store_disp32[] = {0x89, 0x88, 0x10, 0x04, 0x10, 0x00}; /* mov [eax+0x100410],ecx */
    static const uint8_t test_imm[] = {0xF7, 0x80, 0x10, 0x04, 0x10, 0x00,      /* test [eax+0x100410], */
                                       0x00, 0x00, 0x01, 0x00};                 /*   0x10000 */
    static const uint8_t store_disp8[] = {0x89, 0x70, 0x40}; /* mov [eax+0x40],esi (PUT) */
    static const uint8_t load_disp8[] = {0x8B, 0x41, 0x44};  /* mov eax,[ecx+0x44] (GET) */
    static const uint8_t load_plain[] = {0x8B, 0x01};        /* mov eax,[ecx] */
    static const uint8_t load_moffs[] = {0xA1, 0x00, 0x01, 0x00, 0xFD};            /* mov eax,[0xFD000100] */
    static const uint8_t store_moffs[] = {0xA3, 0x40, 0x00, 0x80, 0xFD};           /* mov [0xFD800040],eax */
    static const uint8_t store_imm[] = {0xC7, 0x80, 0x40, 0x01, 0x00, 0x00,        /* mov [eax+0x140], */
                                        0x01, 0x00, 0x00, 0x00};                   /*   1 */
    static const uint8_t load_abs[] = {0x8B, 0x15, 0x00, 0x01, 0x00, 0xFD};        /* mov edx,[0xFD000100] */
    static const uint8_t load_sib[] = {0x8B, 0x04, 0x88};                          /* mov eax,[eax+ecx*4] */
    static const uint8_t load_sib_disp8[] = {0x8B, 0x44, 0x24, 0x08};              /* mov eax,[esp+8] */
    static const uint8_t load_sib_nobase[] = {0x8B, 0x04, 0x8D, 0x00, 0x00, 0x00, 0xFD}; /* [ecx*4+disp32] */
    static const uint8_t store_sib_disp32_imm[] = {0xC7, 0x84, 0x24, 0x00, 0x01, 0x00, 0x00, /* mov [esp+0x100], */
                                                   0x78, 0x56, 0x34, 0x12};                 /*   0x12345678 */
    static const uint8_t load_edi_disp32[] = {0x8B, 0xBE, 0x00, 0x02, 0x00, 0x00}; /* mov edi,[esi+0x200] */

    decodes(load_disp32, 6, MMIO_LOAD, 1, 0);
    decodes(store_disp32, 6, MMIO_STORE_REG, 1, 0);
    decodes(test_imm, 10, MMIO_TEST_IMM, 0, 0x10000);
    decodes(store_disp8, 3, MMIO_STORE_REG, 6, 0);
    decodes(load_disp8, 3, MMIO_LOAD, 0, 0);
    decodes(load_plain, 2, MMIO_LOAD, 0, 0);
    decodes(load_moffs, 5, MMIO_LOAD, 0, 0);
    decodes(store_moffs, 5, MMIO_STORE_REG, 0, 0);
    decodes(store_imm, 10, MMIO_STORE_IMM, 0, 1);
    decodes(load_abs, 6, MMIO_LOAD, 2, 0);
    decodes(load_sib, 3, MMIO_LOAD, 0, 0);
    decodes(load_sib_disp8, 4, MMIO_LOAD, 0, 0);
    decodes(load_sib_nobase, 7, MMIO_LOAD, 0, 0);
    decodes(store_sib_disp32_imm, 11, MMIO_STORE_IMM, 0, 0x12345678);
    decodes(load_edi_disp32, 6, MMIO_LOAD, 7, 0);

    {
        static const uint8_t reg_only[] = {0x8B, 0xC1};                          /* mov eax,ecx */
        static const uint8_t opsize[] = {0x66, 0x8B, 0x01};                      /* mov ax,[ecx] */
        static const uint8_t byte_load[] = {0x8A, 0x01};                         /* mov al,[ecx] */
        static const uint8_t not_mem[] = {0xF7, 0x10};                           /* not dword [eax] */
        static const uint8_t c7_bad[] = {0xC7, 0x08, 0, 0, 0, 0};                /* C7 /1: invalid */
        static const uint8_t or_mem[] = {0x81, 0x08, 0x00, 0x00, 0x01, 0x00};    /* or [eax],0x10000 */
        rejects(reg_only);
        rejects(opsize);
        rejects(byte_load);
        rejects(not_mem);
        rejects(c7_bad);
        rejects(or_mem);
    }

    /* TEST flags: ZF, SF, PF set from the result; CF, OF and AF cleared; the rest kept. */
    CHECK(mmio_insn_test_flags(0x202u | 0x801u, 0) == (0x202u | 0x40u | 0x04u));
    CHECK(mmio_insn_test_flags(0x202u, 0x10000u) == (0x202u | 0x04u)); /* low byte 0: even parity */
    CHECK(mmio_insn_test_flags(0x202u, 0x1u) == 0x202u);               /* one bit: odd parity */
    CHECK(mmio_insn_test_flags(0x202u, 0x3u) == (0x202u | 0x04u));
    CHECK(mmio_insn_test_flags(0x202u | 0x10u, 0x80000001u) == (0x202u | 0x80u));

    if (g_failures) {
        fprintf(stderr, "mmio_insn_test: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    printf("mmio_insn_test: OK (%d checks)\n", g_checks);
    return 0;
}
