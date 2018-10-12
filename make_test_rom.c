#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "emit_6502.h"
#include "opdefs.h"
#include "util.h"

static const size_t k_rom_size = 16384;

static size_t
set_new_index(size_t index, size_t new_index) {
  assert(new_index >= index);
  return new_index;
}

static void
emit_REQUIRE_ZF(struct util_buffer* p_buf, int require) {
  if (require) {
    emit_BEQ(p_buf, 1);
  } else {
    emit_BNE(p_buf, 1);
  }
  emit_CRASH(p_buf);
}

static void
emit_REQUIRE_NF(struct util_buffer* p_buf, int require) {
  if (require) {
    emit_BMI(p_buf, 1);
  } else {
    emit_BPL(p_buf, 1);
  }
  emit_CRASH(p_buf);
}

static void
emit_REQUIRE_CF(struct util_buffer* p_buf, int require) {
  if (require) {
    emit_BCS(p_buf, 1);
  } else {
    emit_BCC(p_buf, 1);
  }
  emit_CRASH(p_buf);
}

static void
emit_REQUIRE_OF(struct util_buffer* p_buf, int require) {
  if (require) {
    emit_BVS(p_buf, 1);
  } else {
    emit_BVC(p_buf, 1);
  }
  emit_CRASH(p_buf);
}

int
main(int argc, const char* argv[]) {
  int fd;
  ssize_t write_ret;

  size_t index = 0;
  unsigned char* p_mem = malloc(k_rom_size);
  struct util_buffer* p_buf = util_buffer_create();

  (void) argc;
  (void) argv;

  (void) memset(p_mem, '\xf2', k_rom_size);
  util_buffer_setup(p_buf, p_mem, k_rom_size);

  /* Reset vector: jump to 0xC000, start of OS ROM. */
  p_mem[0x3ffc] = 0x00;
  p_mem[0x3ffd] = 0xc0;
  /* IRQ vector: also jumped to by the BRK instruction. */
  p_mem[0x3ffe] = 0x00;
  p_mem[0x3fff] = 0xff;

  /* Check PHP, including initial 6502 boot-up flags status. */
  util_buffer_set_pos(p_buf, 0x0000);
  emit_PHP(p_buf);
  emit_LDA(p_buf, k_abs, 0x0100);
  emit_CMP(p_buf, k_imm, 0x34);   /* I, BRK, 1 */
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDA(p_buf, k_imm, 0xFF);   /* Set all flags upon the PLP. */
  emit_STA(p_buf, k_abs, 0x0100);
  emit_PLP(p_buf);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_REQUIRE_NF(p_buf, 1);
  emit_PHP(p_buf);
  emit_LDA(p_buf, k_abs, 0x0100);
  emit_PLP(p_buf);
  emit_CMP(p_buf, k_imm, 0xff);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC040);

  /* Check TSX / TXS stack setup. */
  util_buffer_set_pos(p_buf, 0x0040);
  index = set_new_index(index, 0x40);
  emit_TSX(p_buf);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDX(p_buf, k_imm, 0xFF);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_REQUIRE_NF(p_buf, 1);
  emit_TXS(p_buf);
  emit_JMP(p_buf, k_abs, 0xC080);

  /* Check CMP vs. flags. */
  util_buffer_set_pos(p_buf, 0x0080);
  emit_LDX(p_buf, k_imm, 0xEE);
  emit_CPX(p_buf, k_imm, 0xEE);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_NF(p_buf, 0);
  emit_CPX(p_buf, k_imm, 0x01);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_NF(p_buf, 1);
  emit_CPX(p_buf, k_imm, 0xFF);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_REQUIRE_CF(p_buf, 0);
  emit_REQUIRE_NF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC0C0);

  /* Some ADC tests. */
  util_buffer_set_pos(p_buf, 0x00c0);
  emit_SEC(p_buf);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_CMP(p_buf, k_imm, 0x03);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_ADC(p_buf, k_imm, 0x7f);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_REQUIRE_NF(p_buf, 1);
  emit_REQUIRE_CF(p_buf, 0);
  emit_REQUIRE_OF(p_buf, 1);
  emit_ADC(p_buf, k_imm, 0x7f);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_OF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xC100);

  /* Some SBC tests. */
  util_buffer_set_pos(p_buf, 0x0100);
  emit_CLC(p_buf);
  emit_LDA(p_buf, k_imm, 0x02);
  emit_SBC(p_buf, k_imm, 0x01);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_REQUIRE_NF(p_buf, 0);
  emit_REQUIRE_CF(p_buf, 1);
  emit_REQUIRE_OF(p_buf, 0);
  emit_SBC(p_buf, k_imm, 0x80);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_REQUIRE_NF(p_buf, 1);
  emit_REQUIRE_CF(p_buf, 0);
  emit_REQUIRE_OF(p_buf, 1);
  emit_SEC(p_buf);
  emit_LDA(p_buf, k_imm, 0x10);
  emit_SBC(p_buf, k_imm, 0x7f);
  emit_REQUIRE_NF(p_buf, 1);
  emit_REQUIRE_CF(p_buf, 0);
  emit_REQUIRE_OF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xC140);

  /* Some ROR / ROL tests. */
  util_buffer_set_pos(p_buf, 0x0140);
  emit_LDA(p_buf, k_imm, 0x01);
  emit_SEC(p_buf);
  emit_ROR(p_buf, k_nil, 0);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_REQUIRE_NF(p_buf, 1);
  emit_REQUIRE_CF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC180);

  /* Test BRK! */
  util_buffer_set_pos(p_buf, 0x0180);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_STX(p_buf, k_zpg, 0x00);
  emit_LDX(p_buf, k_imm, 0xFF);
  emit_TXS(p_buf);
  emit_BRK(p_buf);                /* Calls vector $FFFE -> $FF00 (RTI) */
  emit_CRASH(p_buf);              /* Jumped over by RTI. */
  emit_LDX(p_buf, k_zpg, 0x00);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xC1C0);

  /* Test shift / rotate instuction coalescing. */
  util_buffer_set_pos(p_buf, 0x01c0);
  emit_LDA(p_buf, k_imm, 0x05);
  emit_ASL(p_buf, k_nil, 0);
  emit_ASL(p_buf, k_nil, 0);
  emit_CMP(p_buf, k_imm, 0x14);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_SEC(p_buf);
  emit_ROR(p_buf, k_nil, 0);
  emit_ROR(p_buf, k_nil, 0);
  emit_ROR(p_buf, k_nil, 0);
  emit_REQUIRE_CF(p_buf, 1);
  emit_CMP(p_buf, k_imm, 0x22);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC200);

  /* Test indexed zero page addressing. */
  util_buffer_set_pos(p_buf, 0x0200);
  emit_LDA(p_buf, k_imm, 0xfd);
  emit_STA(p_buf, k_zpg, 0x07);
  emit_LDA(p_buf, k_imm, 0xff);
  emit_STA(p_buf, k_zpg, 0x08);
  emit_LDX(p_buf, k_imm, 0x02);
  emit_LDA(p_buf, k_zpx, 0x05);
  emit_CMP(p_buf, k_imm, 0xfd);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDX(p_buf, k_imm, 0xd1);
  emit_LDA(p_buf, k_zpx, 0x37);   /* Zero page wrap. */ /* Addr: $08 */
  emit_CMP(p_buf, k_imm, 0xff);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC240);

  /* Test indirect indexed zero page addressing. */
  util_buffer_set_pos(p_buf, 0x0240);
  emit_LDX(p_buf, k_imm, 0xd1);
  emit_LDA(p_buf, k_idx, 0x36);   /* Zero page wrap. */
  emit_CMP(p_buf, k_imm, 0xc0);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC280);

  /* Test simple JSR / RTS pair. */
  util_buffer_set_pos(p_buf, 0x0280);
  emit_JSR(p_buf,  0xC286);
  emit_JMP(p_buf, k_abs, 0xC2C0);
  emit_RTS(p_buf);

  /* Test BIT. */
  util_buffer_set_pos(p_buf, 0x02c0);
  emit_LDA(p_buf, k_imm, 0xC0);
  emit_STA(p_buf, k_zpg, 0x00);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_INX(p_buf);
  emit_BIT(p_buf, k_zpg, 0x00);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_REQUIRE_OF(p_buf, 1);
  emit_REQUIRE_NF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC300);

  /* Test RTI. */
  util_buffer_set_pos(p_buf, 0x0300);
  emit_LDA(p_buf, k_imm, 0xC3);
  emit_PHA(p_buf);
  emit_LDA(p_buf, k_imm, 0x40);
  emit_PHA(p_buf);
  emit_PHP(p_buf);
  emit_RTI(p_buf);

  /* Test most simple self-modifying code. */
  util_buffer_set_pos(p_buf, 0x0340);
  emit_LDA(p_buf, k_imm, 0x60);   /* RTS */
  emit_STA(p_buf, k_abs, 0x2000);
  emit_JSR(p_buf, 0x2000);
  emit_LDA(p_buf, k_imm, 0xE8);   /* INX */
  emit_STA(p_buf, k_abs, 0x2000);
  emit_LDA(p_buf, k_imm, 0x60);   /* RTS */
  emit_STA(p_buf, k_abs, 0x2001);
  emit_LDX(p_buf, k_imm, 0xFF);
  emit_JSR(p_buf, 0x2000);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC380);

  /* Test self-modifying an operand of an opcode. */
  util_buffer_set_pos(p_buf, 0x0380);
  /* Stores LDA #$00; RTS at $1000. */
  emit_LDA(p_buf, k_imm, 0xA9);
  emit_STA(p_buf, k_abs, 0x1000);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_abs, 0x1001);
  emit_LDA(p_buf, k_imm, 0x60);
  emit_STA(p_buf, k_abs, 0x1002);
  emit_JSR(p_buf, 0x1000);
  emit_REQUIRE_ZF(p_buf, 1);
  /* Modify LDA #$00 at $1000 to be LDA #$01. */
  emit_LDA(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_abs, 0x1001);
  emit_JSR(p_buf, 0x1000);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xC3C0);

  /* Copy some ROM to RAM so we can test self-modifying code easier.
   * This copy uses indirect Y addressing which wasn't actually previously
   * tested either.
   */
  util_buffer_set_pos(p_buf, 0x03C0);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_STA(p_buf, k_zpg, 0xF0);
  emit_STA(p_buf, k_zpg, 0xF2);
  emit_LDA(p_buf, k_imm, 0xF0);
  emit_STA(p_buf, k_zpg, 0xF1);
  emit_LDA(p_buf, k_imm, 0x30);
  emit_STA(p_buf, k_zpg, 0xF3);
  emit_LDY(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_idy, 0xF0);
  emit_STA(p_buf, k_idy, 0xF2);
  emit_INY(p_buf);
  emit_BNE(p_buf, -7);
  emit_JMP(p_buf, k_abs, 0xC400);

  /* Test some more involved self-modifying code situations. */
  util_buffer_set_pos(p_buf, 0x0400);
  emit_JSR(p_buf, 0x3000);        /* Sets X to 0, INX. */
  emit_JSR(p_buf, 0x3002);        /* INX */
  emit_CPX(p_buf, k_imm, 0x02);
  emit_REQUIRE_ZF(p_buf, 1);
  /* Flip INX to DEX at $3002. */
  emit_LDA(p_buf, k_imm, 0xCA);
  emit_STA(p_buf, k_abs, 0x3002);
  emit_JSR(p_buf, 0x3000);        /* Sets X to 0, DEX. */
  emit_REQUIRE_NF(p_buf, 1);
  /* Flip LDX #$00 to LDX #$60 at $3000. */
  emit_LDA(p_buf, k_imm, 0x60);
  emit_STA(p_buf, k_abs, 0x3001);
  emit_JSR(p_buf, 0x3000);        /* Sets X to 0x60, DEX. */
  emit_REQUIRE_NF(p_buf, 0);
  /* The horrors: jump into the middle of an instruction. */
  emit_JSR(p_buf, 0x3001);        /* 0x60 == RTS */
  emit_JSR(p_buf, 0x3000);
  emit_JMP(p_buf, k_abs, 0xC440);

  /* Tests a real self-modifying copy loop. */
  util_buffer_set_pos(p_buf, 0x0440);
  emit_LDA(p_buf, k_imm, 0xE1);
  emit_STA(p_buf, k_abs, 0x1CCC);
  emit_JSR(p_buf, 0x3010);
  emit_LDA(p_buf, k_abs, 0x0CCC);
  emit_CMP(p_buf, k_imm, 0xE1);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC480);

  /* Tests a sequence of forwards / backwards jumps that confused the JIT
   * address tracking.
   */
  util_buffer_set_pos(p_buf, 0x0480);
  emit_LDX(p_buf, k_imm, 0xFF);
  emit_LDY(p_buf, k_imm, 0x01);
  emit_JMP(p_buf, k_abs, 0xC488); /* L1 */
  emit_INX(p_buf);
  emit_BNE(p_buf, -3);            /* L1 here. */
  emit_DEY(p_buf);
  emit_BEQ(p_buf, -5);
  emit_JMP(p_buf, k_abs, 0xC4C0);

  /* Test self-modifying within a block. */
  util_buffer_set_pos(p_buf, 0x04C0);
  emit_JSR(p_buf, 0x3030);
  emit_REQUIRE_ZF(p_buf, 0);
  emit_JMP(p_buf, k_abs, 0xC500);

  /* Test self-modifying code that may invalidate assumptions about instruction
   * flag optimizations.
   */
  util_buffer_set_pos(p_buf, 0x0500);
  emit_JSR(p_buf, 0x3040);
  emit_LDA(p_buf, k_imm, 0x60);
  /* Store RTS at $3042. */
  emit_STA(p_buf, k_abs, 0x3042);
  emit_LDA(p_buf, k_imm, 0x00);
  emit_JSR(p_buf, 0x3040);
  /* Test to see if the flags update went missing due to the self modifying
   * code changing flag expectations within the block.
   */
/*  p_mem[index++] = 0xd0; */ /* BNE (should be ZF=0) */
/*  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; */ /* FAIL */
  emit_JMP(p_buf, k_abs, 0xC540);

  /* Test various simple hardware register read / writes. */
  util_buffer_set_pos(p_buf, 0x0540);
  emit_LDA(p_buf, k_imm, 0x41);
  emit_STA(p_buf, k_abs, 0xFE4A);
  emit_LDA(p_buf, k_abs, 0xFE4A);
  emit_CMP(p_buf, k_imm, 0x41);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDX(p_buf, k_imm, 0xCA);
  emit_LDA(p_buf, k_abx, 0xFD80);
  emit_CMP(p_buf, k_imm, 0x41);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_LDX(p_buf, k_imm, 0x0A);
  emit_SEC(p_buf);
  emit_ROR(p_buf, k_abx, 0xFE40);
  emit_REQUIRE_CF(p_buf, 1);
  emit_LDA(p_buf, k_abs, 0xFE4A);
  emit_CMP(p_buf, k_imm, 0xA0);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_DEC(p_buf, k_abs, 0xFE4A);
  emit_LDA(p_buf, k_abs, 0xFE4A);
  emit_CMP(p_buf, k_imm, 0x9F);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC580);

  /* Test writing to ROM memory. */
  util_buffer_set_pos(p_buf, 0x0580);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_LDY(p_buf, k_imm, 0x00);
  emit_LDA(p_buf, k_imm, 0x39);
  emit_STA(p_buf, k_zpg, 0x02);
  emit_LDA(p_buf, k_imm, 0xC0);
  emit_STA(p_buf, k_zpg, 0x03);
  emit_LDA(p_buf, k_idy, 0x02);
  emit_STA(p_buf, k_zpg, 0x04);
  emit_CLC(p_buf);
  emit_ADC(p_buf, k_imm, 0x01);
  emit_STA(p_buf, k_idy, 0x02);
  emit_LDA(p_buf, k_idy, 0x02);
  emit_CMP(p_buf, k_zpg, 0x04);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_STA(p_buf, k_idx, 0x02);
  emit_LDA(p_buf, k_imm, 0xFF);
  emit_STA(p_buf, k_zpg, 0x02);
  emit_STA(p_buf, k_zpg, 0x03);
  emit_STA(p_buf, k_idy, 0x02);
  emit_JMP(p_buf, k_abs, 0xC5C0);

  /* Test LDX with aby addressing, which was broken, oops! */
  util_buffer_set_pos(p_buf, 0x05C0);
  emit_LDX(p_buf, k_imm, 0x00);
  emit_LDY(p_buf, k_imm, 0x04);
  emit_LDX(p_buf, k_aby, 0xC5C0);
  emit_CPX(p_buf, k_imm, 0xBE);
  emit_REQUIRE_ZF(p_buf, 1);
  emit_JMP(p_buf, k_abs, 0xC600);

  /* Test a variety of additional high-address reads and writes of interest. */
  index = set_new_index(index, 0x600);
  p_mem[index++] = 0xa2; /* LDX #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xbd; /* LDA $FBFF,X */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xfb;
  p_mem[index++] = 0xc9; /* CMP #$7D */
  p_mem[index++] = 0x7d;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x8d; /* STA $801B */
  p_mem[index++] = 0x1b;
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xad; /* LDA $C603 */
  p_mem[index++] = 0x03;
  p_mem[index++] = 0xc6;
  p_mem[index++] = 0xc9; /* CMP #$FF */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xce; /* DEC $C603 */
  p_mem[index++] = 0x03;
  p_mem[index++] = 0xc6;
  p_mem[index++] = 0xd0; /* BNE (should be ZF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x18; /* CLC */
  p_mem[index++] = 0x6e; /* ROR $C603 */
  p_mem[index++] = 0x03;
  p_mem[index++] = 0xc6;
  p_mem[index++] = 0xb0; /* BCS (should be CF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C640 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0xc6;

  /* Test an interesting bug we had with self-modifying code where two
   * adjacent instructions are clobbered.
   */
  index = set_new_index(index, 0x640);
  p_mem[index++] = 0xea; /* NOP */
  p_mem[index++] = 0x20; /* JSR $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xa9; /* LDA #$60 */
  p_mem[index++] = 0x60;
  p_mem[index++] = 0x8d; /* STA $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x8d; /* STA $3051 */
  p_mem[index++] = 0x51;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x20; /* JSR $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x4c; /* JMP $C680 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xc6;

  /* Test JIT invalidation through different write modes. */
  index = set_new_index(index, 0x680);
  p_mem[index++] = 0x20; /* JSR $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xa2; /* LDX #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xa0; /* LDY #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xa9; /* LDA #$ca */ /* DEX opcode */
  p_mem[index++] = 0xca;
  p_mem[index++] = 0x9d; /* STA $3050,X */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x20; /* JSR $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xe0; /* CPX #$ff */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa9; /* LDA #$88 */ /* DEY opcode */
  p_mem[index++] = 0x88;
  p_mem[index++] = 0x99; /* STA $3050,Y */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x20; /* JSR $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xc0; /* CPY #$ff */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C6C0 */
  p_mem[index++] = 0xc0;
  p_mem[index++] = 0xc6;

  /* Test JIT invalidation through remaining write modes. */
  index = set_new_index(index, 0x6c0);
  p_mem[index++] = 0xa9; /* LDA #$ea */ /* NOP opcode */
  p_mem[index++] = 0xea;
  p_mem[index++] = 0x8d; /* STA $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x20; /* JSR $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xa9; /* LDA #$50 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x85; /* STA $8F */
  p_mem[index++] = 0x8f;
  p_mem[index++] = 0xa9; /* LDA #$30 */
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x85; /* STA $90 */
  p_mem[index++] = 0x90;
  p_mem[index++] = 0xa0; /* LDY #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xa2; /* LDX #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xa9; /* LDA #$e8 */ /* INX opcode */
  p_mem[index++] = 0xe8;
  p_mem[index++] = 0x91; /* STA ($8F),Y */
  p_mem[index++] = 0x8f;
  p_mem[index++] = 0x20; /* JSR $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xe0; /* CPX #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa2; /* LDX #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xa9; /* LDA #$c8 */ /* INY opcode */
  p_mem[index++] = 0xc8;
  p_mem[index++] = 0x81; /* STA ($8F,X) */
  p_mem[index++] = 0x8f;
  p_mem[index++] = 0x20; /* JSR $3050 */
  p_mem[index++] = 0x50;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xc0; /* CPY #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C700 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xc7;

  /* Test JIT recompilation bug leading to corrupted code generation.
   * Trigger condition is replacing an opcode resulting in short generated code
   * with one resulting in longer generated code.
   */
  index = set_new_index(index, 0x700);
  p_mem[index++] = 0x20; /* JSR $3070 */
  p_mem[index++] = 0x70;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xa9; /* LDA #$48 */ /* PHA */
  p_mem[index++] = 0x48;
  p_mem[index++] = 0x8d; /* STA $3070 */
  p_mem[index++] = 0x70;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xa9; /* LDA #$68 */ /* PLA */
  p_mem[index++] = 0x68;
  p_mem[index++] = 0x8d; /* STA $3071 */
  p_mem[index++] = 0x71;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x20; /* JSR $3070 */
  p_mem[index++] = 0x70;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x4c; /* JMP $C740 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0xc7;

  /* Test a few simple VIA behaviors. */
  index = set_new_index(index, 0x740);
  p_mem[index++] = 0xad; /* LDA $FE60 */ /* User VIA ORB */
  p_mem[index++] = 0x60;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xc9; /* CMP #$FF */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xad; /* LDA $FE70 */ /* User VIA ORB */ /* Alt address */
  p_mem[index++] = 0x70;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xc9; /* CMP #$FF */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa9; /* LDA #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x8d; /* STA $FE62 */ /* User VIA DDRB */
  p_mem[index++] = 0x62;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xad; /* LDA $FE60 */ /* User VIA ORB */
  p_mem[index++] = 0x60;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xc9; /* CMP #$FE */
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa9; /* LDA #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x8d; /* STA $FE60 */ /* User VIA ORB */
  p_mem[index++] = 0x60;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xad; /* LDA $FE60 */ /* User VIA ORB */
  p_mem[index++] = 0x60;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xc9; /* CMP #$FF */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C780 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xc7;

  /* Test the firing of a timer interrupt. */
  index = set_new_index(index, 0x780);
  p_mem[index++] = 0x78; /* SEI */
  p_mem[index++] = 0xad; /* LDA $FE4E */ /* sysvia IER */
  p_mem[index++] = 0x4e;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xc9; /* CMP #$80 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa9; /* LDA #$0e */
  p_mem[index++] = 0x0e;
  p_mem[index++] = 0x8d; /* STA $FE44 */ /* sysvia T1CL */
  p_mem[index++] = 0x44;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xa9; /* LDA #$27 */
  p_mem[index++] = 0x27;
  p_mem[index++] = 0x8d; /* STA $FE45 */ /* sysvia T1CH */
  p_mem[index++] = 0x45;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xad; /* LDA $FE4D */ /* sysvia IFR */
  p_mem[index++] = 0x4d;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0x29; /* AND #$40 */ /* TIMER1 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa9; /* LDA #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x85; /* STA $00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xa9; /* LDA #$C0 */ /* set, TIMER1 */
  p_mem[index++] = 0xc0;
  p_mem[index++] = 0x8d; /* STA $FE4E */ /* sysvia IER */
  p_mem[index++] = 0x4e;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0x58; /* CLI */
  p_mem[index++] = 0xa5; /* LDA $00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xf0; /* BEQ -4 */ /* Wait until an interrupt is serviced. */
  p_mem[index++] = 0xfc;
  p_mem[index++] = 0xad; /* LDA $FE4D */ /* sysvia IFR */
  p_mem[index++] = 0x4d;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0x29; /* AND #$40 */ /* TIMER1 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0xd0; /* BNE (should be ZF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa9; /* LDA #$27 */
  p_mem[index++] = 0x27;
  p_mem[index++] = 0x8d; /* STA $FE45 */ /* sysvia T1CH */ /* Clears TIMER1. */
  p_mem[index++] = 0x45;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xad; /* LDA $FE4D */ /* sysvia IFR */
  p_mem[index++] = 0x4d;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0x29; /* AND #$40 */ /* TIMER1 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  /* Loop again, starting here, with interrupts disabled.
   * Interrupt enable occurs briefly and within the block. Interrupt must still
   * fire :)
   */
  p_mem[index++] = 0x58; /* CLI */
  p_mem[index++] = 0x78; /* SEI */
  p_mem[index++] = 0xa5; /* LDA $00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xc9; /* CMP #$02 */
  p_mem[index++] = 0x02;
  p_mem[index++] = 0xd0; /* BNE -8 */
  p_mem[index++] = 0xf8;
  p_mem[index++] = 0x58; /* CLI */
  p_mem[index++] = 0xad; /* LDA $FE44 */ /* sysvia T1CL */ /* Clears TIMER1. */
  p_mem[index++] = 0x44;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xad; /* LDA $FE4D */ /* sysvia IFR */
  p_mem[index++] = 0x4d;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0x29; /* AND #$40 */ /* TIMER1 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa9; /* LDA #$40 */ /* clear, TIMER1 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0x8d; /* STA $FE4E */ /* sysvia IER */
  p_mem[index++] = 0x4e;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0x4c; /* JMP $C800 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xc8;

  /* Test the firing of a timer interrupt -- to be contrarian, let's now test
   * TIMER2 on the user VIA, using alternative registers.
   */
  index = set_new_index(index, 0x800);
  p_mem[index++] = 0x78; /* SEI */
  p_mem[index++] = 0xa9; /* LDA #$0e */
  p_mem[index++] = 0x0e;
  p_mem[index++] = 0x8d; /* STA $FE78 */ /* uservia T2CL */
  p_mem[index++] = 0x78;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xa9; /* LDA #$27 */
  p_mem[index++] = 0x27;
  p_mem[index++] = 0x8d; /* STA $FE79 */ /* uservia T2CH */
  p_mem[index++] = 0x79;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xad; /* LDA $FE7D */ /* uservia IFR */
  p_mem[index++] = 0x7d;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0x29; /* AND #$20 */ /* TIMER2 */
  p_mem[index++] = 0x20;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa9; /* LDA #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x85; /* STA $00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xa9; /* LDA #$A0 */ /* set, TIMER2 */
  p_mem[index++] = 0xa0;
  p_mem[index++] = 0x8d; /* STA $FE7E */ /* uservia IER */
  p_mem[index++] = 0x7e;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0x58; /* CLI */
  p_mem[index++] = 0xa5; /* LDA $00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xf0; /* BEQ -4 */ /* Wait until an interrupt is serviced. */
  p_mem[index++] = 0xfc;
  p_mem[index++] = 0xad; /* LDA $FE7D */ /* uservia IFR */
  p_mem[index++] = 0x7d;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0x29; /* AND #$20 */ /* TIMER2 */
  p_mem[index++] = 0x20;
  p_mem[index++] = 0xd0; /* BNE (should be ZF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa9; /* LDA #$27 */
  p_mem[index++] = 0x27;
  p_mem[index++] = 0x8d; /* STA $FE79 */ /* uservia T2CH */ /* Clears TIMER2. */
  p_mem[index++] = 0x79;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xad; /* LDA $FE7D */ /* uservia IFR */
  p_mem[index++] = 0x7d;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0x29; /* AND #$20 */ /* TIMER2 */
  p_mem[index++] = 0x20;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa9; /* LDA #$20 */ /* clear, TIMER2 */
  p_mem[index++] = 0x20;
  p_mem[index++] = 0x8d; /* STA $FE7E */ /* uservia IER */
  p_mem[index++] = 0x7e;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0x4c; /* JMP $C840 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0xc8;

  /* Test dynamic operands for a branch instruction, and a no-operands
   * instruction.
    */
  index = set_new_index(index, 0x840);
  p_mem[index++] = 0x20; /* JSR $3080 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xa9; /* LDA #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x8d; /* STA $3083 */
  p_mem[index++] = 0x83;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x20; /* JSR $3080 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xe0; /* CPX #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa9; /* LDA #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x8d; /* STA $3083 */
  p_mem[index++] = 0x83;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x20; /* JSR $3080 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xe0; /* CPX #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa9; /* LDA #$e8 */ /* INX */
  p_mem[index++] = 0xe8;
  p_mem[index++] = 0x8d; /* STA $3084 */
  p_mem[index++] = 0x84;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x20; /* JSR $3080 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x4c; /* JMP $C880 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xc8;

  /* Tests for a bug where the NZ flags update was going missing if there
   * were a couple of STA instructions in a row.
   */
  index = set_new_index(index, 0x880);
  p_mem[index++] = 0xa2; /* LDX #$FF */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xe8; /* INX */
  /* ZF is now 1. This should clear ZF. */
  p_mem[index++] = 0xa9; /* LDA #$FF */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0x85; /* STA $00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x85; /* STA $00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xd0; /* BNE (should be ZF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C8C0 */
  p_mem[index++] = 0xc0;
  p_mem[index++] = 0xc8;

  /* Test that timers don't return the same value twice in a row. */
  index = set_new_index(index, 0x8c0);
  p_mem[index++] = 0xad; /* LDA $FE64 */
  p_mem[index++] = 0x64;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xcd; /* CMP $FE64 */
  p_mem[index++] = 0x64;
  p_mem[index++] = 0xfe;
  p_mem[index++] = 0xd0; /* BNE (should be ZF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C900 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xc9;

  /* Test that the carry flag optimizations don't break anything. */
  index = set_new_index(index, 0x900);
  p_mem[index++] = 0x18; /* CLC */
  p_mem[index++] = 0xa9; /* LDA #$FF */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0x69; /* ADC #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x69; /* ADC #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xc9; /* CMP #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C940 */
  p_mem[index++] = 0x40;
  p_mem[index++] = 0xc9;

  /* Tests for a bug where the NZ flags were updated from the wrong register. */
  index = set_new_index(index, 0x940);
  p_mem[index++] = 0x20; /* JSR $C965 */ /* Create block boundary at the RTS. */
  p_mem[index++] = 0x65;
  p_mem[index++] = 0xc9;
  p_mem[index++] = 0x20; /* JSR $C960 */
  p_mem[index++] = 0x60;
  p_mem[index++] = 0xc9;
  p_mem[index++] = 0x30; /* BMI (should be NF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C980 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0xc9;
  index = set_new_index(index, 0x960);
  p_mem[index++] = 0xa9; /* LDA #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xaa; /* TAX */
  p_mem[index++] = 0xa9; /* LDA #$FF */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0x60; /* RTS */

  /* Give the carry flag tracking logic a good workout. */
  index = set_new_index(index, 0x980);
  p_mem[index++] = 0x18; /* CLC */
  p_mem[index++] = 0xa9; /* LDA #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x4a; /* LSR */
  p_mem[index++] = 0x69; /* ADC #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x69; /* ADC #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xaa; /* TAX */
  p_mem[index++] = 0xe8; /* INX */
  p_mem[index++] = 0x2a; /* ROL */
  p_mem[index++] = 0xe9; /* SBC #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x6a; /* ROR */
  p_mem[index++] = 0xa8; /* TAY */
  p_mem[index++] = 0xe0; /* CPX #$04 */
  p_mem[index++] = 0x04;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xc0; /* CPY #$82 */
  p_mem[index++] = 0x82;
  p_mem[index++] = 0xf0; /* BEQ (should be ZF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $C9C0 */
  p_mem[index++] = 0xc0;
  p_mem[index++] = 0xc9;

  /* Give the overflow flag tracking logic a good workout. */
  index = set_new_index(index, 0x9c0);
  p_mem[index++] = 0x18; /* CLC */
  p_mem[index++] = 0xa9; /* LDA #$01 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0x69; /* ADC #$7f */ /* Sets OF=1 */
  p_mem[index++] = 0x7f;
  p_mem[index++] = 0xa9; /* LDA #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x4a; /* LSR */
  p_mem[index++] = 0x70; /* BVS (should be OF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xb8; /* CLV */
  p_mem[index++] = 0x50; /* BVC (should be OF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x38; /* SEC */
  p_mem[index++] = 0x69; /* ADC #$7f */ /* Sets OF=1 */
  p_mem[index++] = 0x7f;
  p_mem[index++] = 0x70; /* BVS (should be OF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xc9; /* CMP #$FF */ /* Should not affect OF. */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0x70; /* BVS (should be OF=1) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0xa9; /* LDA #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x18; /* CLC */
  p_mem[index++] = 0x69; /* ADC #$00 */ /* Sets OF=0 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xa9; /* LDA #$80 */
  p_mem[index++] = 0x80;
  p_mem[index++] = 0x6a; /* ROR */
  p_mem[index++] = 0x50; /* BVC (should be OF=0) */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xf2; /* FAIL */
  p_mem[index++] = 0x4c; /* JMP $CA00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xca;

  index = set_new_index(index, 0xa00);
  p_mem[index++] = 0x02; /* Done */

  /* Some program code that we copy to ROM at $f000 to RAM at $3000 */
  index = set_new_index(index, 0x3000);
  p_mem[index++] = 0xa2; /* LDX #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xe8; /* INX */
  p_mem[index++] = 0x60; /* RTS */

  /* This is close to one of the simplest self-modifying routines I found: the
   * Galaforce memory copy at first load.
   */
  index = set_new_index(index, 0x3010);
  p_mem[index++] = 0xa0; /* LDY #$04 */
  p_mem[index++] = 0x04;
  p_mem[index++] = 0xbd; /* LDA $1A00,X */ /* Jump target for both BNEs. */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x1a;
  p_mem[index++] = 0x9d; /* STA $0A00,X */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x0a;
  p_mem[index++] = 0xe8; /* INX */
  p_mem[index++] = 0xd0; /* BNE -9 */
  p_mem[index++] = 0xf7;
  p_mem[index++] = 0xee; /* INC $3014 */ /* Self-modifying. */
  p_mem[index++] = 0x14;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xee; /* INC $3017 */ /* Self-modifying. */
  p_mem[index++] = 0x17;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0x88; /* DEY */
  p_mem[index++] = 0xd0; /* BNE -18 */
  p_mem[index++] = 0xee;
  p_mem[index++] = 0x60; /* RTS */

  /* A block that self-modifies within itself. */
  index = set_new_index(index, 0x3030);
  p_mem[index++] = 0xa9; /* LDA #$ff */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0x8d; /* STA $3036 */
  p_mem[index++] = 0x36;
  p_mem[index++] = 0x30;
  p_mem[index++] = 0xa9; /* LDA #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x60; /* RTS */

  /* Another block for us to modify. */
  index = set_new_index(index, 0x3040);
  p_mem[index++] = 0xa9; /* LDA #$ff */
  p_mem[index++] = 0xff;
  p_mem[index++] = 0xa9; /* LDA #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x60; /* RTS */

  /* Yet another block for us to modify. */
  index = set_new_index(index, 0x3050);
  p_mem[index++] = 0xe8; /* INX */
  p_mem[index++] = 0xc8; /* INY */
  p_mem[index++] = 0x60; /* RTS */

  /* etc... */
  index = set_new_index(index, 0x3060);
  p_mem[index++] = 0xea; /* NOP */
  p_mem[index++] = 0x60; /* RTS */

  /* etc... */
  index = set_new_index(index, 0x3070);
  p_mem[index++] = 0xe8; /* INX */
  p_mem[index++] = 0xc8; /* INY */
  p_mem[index++] = 0x60; /* RTS */

  /* For branch dynamic operands. */
  index = set_new_index(index, 0x3080);
  p_mem[index++] = 0xa2; /* LDX #$00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0xf0; /* BEQ + 1 */
  p_mem[index++] = 0x01;
  p_mem[index++] = 0xe8; /* INX */
  p_mem[index++] = 0x60; /* RTS */

  /* Need this byte here for a specific test. */
  index = set_new_index(index, 0x3bff);
  p_mem[index++] = 0x7d;

  /* IRQ routine. */
  index = set_new_index(index, 0x3f00);
  p_mem[index++] = 0xe6; /* INC $00 */
  p_mem[index++] = 0x00;
  p_mem[index++] = 0x40; /* RTI */

  fd = open("test.rom", O_CREAT | O_WRONLY, 0600);
  if (fd < 0) {
    errx(1, "can't open output rom");
  }
  write_ret = write(fd, p_mem, k_rom_size);
  if (write_ret < 0) {
    errx(1, "can't write output rom");
  }
  if ((size_t) write_ret != k_rom_size) {
    errx(1, "can't write output rom");
  }
  close(fd);

  util_buffer_destroy(p_buf);
  free(p_mem);

  return 0;
}
