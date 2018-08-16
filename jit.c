#include "jit.h"

#include "bbc.h"
#include "debug.h"
#include "opdefs.h"
#include "util.h"

#include <assert.h>
#include <err.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  k_addr_space_size = 0x10000,
};
static const size_t k_guard_size = 4096;
static const int k_jit_bytes_per_byte = 256;
static const int k_jit_bytes_shift = 8;
static void* k_jit_addr = (void*) 0x20000000;
static void* k_utils_addr = (void*) 0x80000000;
static const size_t k_utils_size = 4096;
static const size_t k_utils_debug_offset = 0;
static const size_t k_utils_regs_offset = 0x100;
static const size_t k_utils_jit_offset = 0x200;

static const int k_offset_util_debug = 24;
static const int k_offset_util_regs = 32;
static const int k_offset_util_jit = 40;
static const int k_offset_debug = 48;
static const int k_offset_debug_callback = 56;
static const int k_offset_jit_callback = 64;
static const int k_offset_bbc = 72;
static const int k_offset_read_callback = 80;
static const int k_offset_write_callback = 88;
static const int k_offset_interrupt = 96;
static const int k_offset_reg_a = 104;
static const int k_offset_reg_x = 105;
static const int k_offset_reg_y = 106;
static const int k_offset_reg_s = 107;
static const int k_offset_reg_flags = 108;
static const int k_offset_reg_pc = 110;
static const int k_offset_jit_ptrs = 116;

static const unsigned int k_jit_flag_debug = 1;
static const unsigned int k_jit_flag_merge_ops = 2;
static const unsigned int k_jit_flag_self_modifying = 4;

enum {
  k_a = 1,
  k_x = 2,
  k_y = 3,
  k_intel = 4,
  k_6502 = 5,
};

/* TODO: this is stale. */
/* k_a: PLA, TXA, TYA, LDA */
/* k_x: LDX, TAX, TSX, DEX, INX */
/* k_y: DEY, LDY, TAY, INY */
static const unsigned char g_nz_flag_results[58] = {
  0, 0, 0, k_6502, k_6502, 0, 0, 0,
  0, k_6502, k_6502, k_6502, k_6502, 0, 0, k_6502,
  k_6502, k_6502, 0, 0, 0, 0, 0, k_6502,
  k_a, k_6502, 0, 0, 0, 0, 0, k_y,
  k_a, 0, k_a, 0, k_y, k_a, k_x, k_y,
  k_x, 0, 0, k_x, k_6502, k_6502, k_6502, k_6502,
  k_y, k_x, 0, 0, k_6502, k_x, 0, k_6502,
  0, 0,
};

static const unsigned char g_nz_flags_needed[58] = {
  0, 0, 1, 0, 0, 1, 1, 0,
  1, 0, 0, 0, 0, 1, 0, 0,
  0, 0, 0, 1, 1, 1, 1, 0,
  0, 0, 1, 0, 0, 0, 0, 0,
  0, 1, 0, 0, 0, 0, 0, 0,
  0, 1, 0, 0, 0, 0, 0, 0,
  0, 0, 1, 0, 0, 0, 0, 0,
  1, 0,
};

struct jit_struct {
  unsigned char* p_mem;        /* 0   */
  unsigned char* p_jit_base;   /* 8   */
  unsigned char* p_utils_base; /* 16  */
  unsigned char* p_util_debug; /* 24  */
  unsigned char* p_util_regs;  /* 32  */
  unsigned char* p_util_jit;   /* 40  */
  void* p_debug;               /* 48  */
  void* p_debug_callback;      /* 56  */
  void* p_jit_callback;        /* 64  */
  struct bbc_struct* p_bbc;    /* 72  */
  void* p_read_callback;       /* 80  */
  void* p_write_callback;      /* 88  */
  uint64_t interrupt;          /* 96  */
  unsigned char reg_a;         /* 104 */
  unsigned char reg_x;         /* 105 */
  unsigned char reg_y;         /* 106 */
  unsigned char reg_s;         /* 107 */
  unsigned char reg_flags;     /* 108 */
  unsigned char pad;
  uint16_t reg_pc;             /* 110 */
  unsigned int jit_flags;      /* 112 */
  unsigned int jit_ptrs[k_addr_space_size]; /* 116 */
};

static size_t
jit_emit_int(unsigned char* p_jit_buf, size_t index, ssize_t offset) {
  assert(offset >= INT_MIN);
  assert(offset <= INT_MAX);
  p_jit_buf[index++] = offset & 0xff;
  offset >>= 8;
  p_jit_buf[index++] = offset & 0xff;
  offset >>= 8;
  p_jit_buf[index++] = offset & 0xff;
  offset >>= 8;
  p_jit_buf[index++] = offset & 0xff;

  return index;
}

static size_t
jit_emit_intel_to_6502_carry(unsigned char* p_jit, size_t index) {
  /* setb r14b */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x92;
  p_jit[index++] = 0xc6;

  return index;
}

static size_t
jit_emit_intel_to_6502_sub_carry(unsigned char* p_jit, size_t index) {
  /* setae r14b */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x93;
  p_jit[index++] = 0xc6;

  return index;
}

static size_t
jit_emit_intel_to_6502_overflow(unsigned char* p_jit, size_t index) {
  /* seto r12b */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x90;
  p_jit[index++] = 0xc4;

  return index;
}

static size_t
jit_emit_carry_to_6502_overflow(unsigned char* p_jit, size_t index) {
  /* setb r12b */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x92;
  p_jit[index++] = 0xc4;

  return index;
}

static size_t
jit_emit_do_zn_flags(unsigned char* p_jit, size_t index, int reg) {
  assert(reg >= 0 && reg <= 2);
  if (reg == 0) {
    /* test al, al */
    p_jit[index++] = 0x84;
    p_jit[index++] = 0xc0;
  } else if (reg == 1) {
    /* test bl, bl */
    p_jit[index++] = 0x84;
    p_jit[index++] = 0xdb;
  } else if (reg == 2) {
    /* test cl, cl */
    p_jit[index++] = 0x84;
    p_jit[index++] = 0xc9;
  }

  return index;
}

static size_t
jit_emit_intel_to_6502_co(unsigned char* p_jit, size_t index) {
  index = jit_emit_intel_to_6502_carry(p_jit, index);
  index = jit_emit_intel_to_6502_overflow(p_jit, index);

  return index;
}

static size_t
jit_emit_intel_to_6502_sub_co(unsigned char* p_jit, size_t index) {
  index = jit_emit_intel_to_6502_sub_carry(p_jit, index);
  index = jit_emit_intel_to_6502_overflow(p_jit, index);

  return index;
}

static size_t
jit_emit_6502_carry_to_intel(unsigned char* p_jit, size_t index) {
  /* bt r14, 0 */
  p_jit[index++] = 0x49;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xba;
  p_jit[index++] = 0xe6;
  p_jit[index++] = 0x00;

  return index;
}

static size_t
jit_emit_set_carry(unsigned char* p_jit, size_t index, unsigned char val) {
  /* mov r14b, val */
  p_jit[index++] = 0x41;
  p_jit[index++] = 0xb6;
  p_jit[index++] = val;

  return index;
}

static size_t
jit_emit_test_carry(unsigned char* p_jit, size_t index) {
  return jit_emit_6502_carry_to_intel(p_jit, index);
}

static size_t
jit_emit_test_overflow(unsigned char* p_jit, size_t index) {
  /* bt r12, 0 */
  p_jit[index++] = 0x49;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xba;
  p_jit[index++] = 0xe4;
  p_jit[index++] = 0x00;

  return index;
}

static size_t
jit_emit_abs_x_to_scratch(unsigned char* p_jit,
                          size_t index,
                          unsigned char operand1,
                          unsigned char operand2) {
  /* Empty -- optimized for now but wrap-around will cause a fault. */
  return index;
}

static size_t
jit_emit_abs_y_to_scratch(unsigned char* p_jit,
                          size_t index,
                          unsigned char operand1,
                          unsigned char operand2) {
  /* Empty -- optimized for now but wrap-around will cause a fault. */
  return index;
}

static size_t
jit_emit_ind_y_to_scratch(struct jit_struct* p_jit,
                          unsigned char* p_jit_buf,
                          size_t index,
                          unsigned char operand1) {
  if (operand1 == 0xff) {
    /* movzx edx, BYTE PTR [p_mem + 0xff] */
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xb6;
    p_jit_buf[index++] = 0x14;
    p_jit_buf[index++] = 0x25;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem + 0xff);
    /* mov dh, BYTE PTR [p_mem] */
    p_jit_buf[index++] = 0x8a;
    p_jit_buf[index++] = 0x34;
    p_jit_buf[index++] = 0x25;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
  } else {
    /* movzx edx, WORD PTR [p_mem + op1] */
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xb7;
    p_jit_buf[index++] = 0x14;
    p_jit_buf[index++] = 0x25;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem + operand1);
  }

  return index;
}

static size_t
jit_emit_ind_x_to_scratch(struct jit_struct* p_jit,
                          unsigned char* p_jit_buf,
                          size_t index,
                          unsigned char operand1) {
  unsigned char operand1_inc = operand1 + 1;
  /* NOTE: zero page wrap is very uncommon so we could do fault-based fixup
   * instead.
   */
  /* mov rdi, rbx */
  p_jit_buf[index++] = 0x48;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0xdf;
  /* lea edx, [rbx + operand1] */
  p_jit_buf[index++] = 0x8d;
  p_jit_buf[index++] = 0x93;
  index = jit_emit_int(p_jit_buf, index, operand1);
  /* lea r9, [rbx + operand1 + 1] */
  p_jit_buf[index++] = 0x4c;
  p_jit_buf[index++] = 0x8d;
  p_jit_buf[index++] = 0x8b;
  index = jit_emit_int(p_jit_buf, index, operand1_inc);
  /* mov dil, dl */
  p_jit_buf[index++] = 0x40;
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0xd7;
  /* movzx edx, BYTE PTR [rdi] */
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xb6;
  p_jit_buf[index++] = 0x17;
  /* mov dil, r9b */
  p_jit_buf[index++] = 0x44;
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0xcf;
  /* mov dh, BYTE PTR [rdi] */
  p_jit_buf[index++] = 0x8a;
  p_jit_buf[index++] = 0x37;

  return index;
}

static size_t
jit_emit_zp_x_to_scratch(unsigned char* p_jit,
                         size_t index,
                         unsigned char operand1) {
  /* NOTE: zero page wrap is very uncommon so we could do fault-based fixup
   * instead.
   */
  /* lea edi, [rbx + operand1] */
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0xbb;
  index = jit_emit_int(p_jit, index, operand1);
  /* movzx edx, dil */
  p_jit[index++] = 0x40;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0xd7;

  return index;
}

static size_t
jit_emit_zp_y_to_scratch(unsigned char* p_jit,
                         size_t index,
                         unsigned char operand1) {
  /* NOTE: zero page wrap is very uncommon so we could do fault-based fixup
   * instead.
   */
  /* lea edi, [rcx + operand1] */
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0xb9;
  index = jit_emit_int(p_jit, index, operand1);
  /* movzx edx, dil */
  p_jit[index++] = 0x40;
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0xd7;

  return index;
}

static size_t
jit_emit_scratch_bit_test(unsigned char* p_jit,
                          size_t index,
                          unsigned char bit) {
  /* bt edx, bit */
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xba;
  p_jit[index++] = 0xe2;
  p_jit[index++] = bit;

  return index;
}

static size_t
jit_emit_jmp_scratch(unsigned char* p_jit, size_t index) {
  /* jmp rdx */
  p_jit[index++] = 0xff;
  p_jit[index++] = 0xe2;

  return index;
}

static size_t
jit_emit_jmp_6502_addr(struct jit_struct* p_jit,
                       struct util_buffer* p_buf,
                       uint16_t new_addr_6502,
                       unsigned char opcode1,
                       unsigned char opcode2) {
  unsigned char* p_src_addr = util_buffer_get_base_address(p_buf) +
                              util_buffer_get_pos(p_buf);
  unsigned char* p_dst_addr = p_jit->p_jit_base +
                              (new_addr_6502 * k_jit_bytes_per_byte);
  ssize_t offset = p_dst_addr - p_src_addr;
  unsigned char* p_jit_buf = util_buffer_get_ptr(p_buf);
  size_t index = util_buffer_get_pos(p_buf);
  /* TODO: emit short opcode sequence if jump is in range. */
  /* Intel opcode length (5 or 6) counts against jump delta. */
  offset -= 5;
  p_jit_buf[index++] = opcode1;
  if (opcode2 != 0) {
    p_jit_buf[index++] = opcode2;
    offset--;
  }
  index = jit_emit_int(p_jit_buf, index, offset);

  return index;
}

static size_t
jit_emit_jit_bytes_shift_scratch_left(unsigned char* p_jit, size_t index) {
  /* NOTE: uses BMI2 shlx instruction to avoid modifying flags. */
  /* mov dil, k_jit_bytes_shift */
  p_jit[index++] = 0x40;
  p_jit[index++] = 0xb7;
  p_jit[index++] = k_jit_bytes_shift;
  /* shlx edx, edx, edi k_jit_bytes_shift */ /* BMI2 */
  p_jit[index++] = 0xc4;
  p_jit[index++] = 0xe2;
  p_jit[index++] = 0x41;
  p_jit[index++] = 0xf7;
  p_jit[index++] = 0xd2;

  return index;
}

static size_t
jit_emit_stack_inc(unsigned char* p_jit, size_t index) {
  /* lea edi, [rsi + 1] */
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x7e;
  p_jit[index++] = 0x01;
  /* mov sil, dil */
  p_jit[index++] = 0x40;
  p_jit[index++] = 0x88;
  p_jit[index++] = 0xfe;

  return index;
}

static size_t
jit_emit_stack_dec(unsigned char* p_jit, size_t index) {
  /* lea edi, [rsi - 1] */
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x7e;
  p_jit[index++] = 0xff;
  /* mov sil, dil */
  p_jit[index++] = 0x40;
  p_jit[index++] = 0x88;
  p_jit[index++] = 0xfe;

  return index;
}

static size_t
jit_emit_pull_to_a(unsigned char* p_jit, size_t index) {
  index = jit_emit_stack_inc(p_jit, index);
  /* mov al, [rsi] */
  p_jit[index++] = 0x8a;
  p_jit[index++] = 0x06;

  return index;
}

static size_t
jit_emit_pull_to_scratch(unsigned char* p_jit, size_t index) {
  index = jit_emit_stack_inc(p_jit, index);
  /* mov dl, [rsi] */
  p_jit[index++] = 0x8a;
  p_jit[index++] = 0x16;

  return index;
}

static size_t
jit_emit_pull_to_scratch_word(unsigned char* p_jit, size_t index) {
  index = jit_emit_stack_inc(p_jit, index);
  /* movzx edx, BYTE PTR [rsi] */
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0x16;
  index = jit_emit_stack_inc(p_jit, index);
  /* mov dh, BYTE PTR [rsi] */
  p_jit[index++] = 0x8a;
  p_jit[index++] = 0x36;

  return index;
}

static size_t
jit_emit_push_from_a(unsigned char* p_jit, size_t index) {
  /* mov [rsi], al */
  p_jit[index++] = 0x88;
  p_jit[index++] = 0x06;
  index = jit_emit_stack_dec(p_jit, index);

  return index;
}

static size_t
jit_emit_push_constant(unsigned char* p_jit_buf,
                       size_t index,
                       unsigned char val) {
  /* mov BYTE PTR [rsi], val */
  p_jit_buf[index++] = 0xc6;
  p_jit_buf[index++] = 0x06;
  p_jit_buf[index++] = val;
  index = jit_emit_stack_dec(p_jit_buf, index);

  return index;
}

static size_t
jit_emit_push_from_scratch(unsigned char* p_jit, size_t index) {
  /* mov [rsi], dl */
  p_jit[index++] = 0x88;
  p_jit[index++] = 0x16;
  index = jit_emit_stack_dec(p_jit, index);

  return index;
}

static size_t
jit_emit_push_addr(unsigned char* p_jit_buf, size_t index, uint16_t addr_6502) {
  index = jit_emit_push_constant(p_jit_buf, index, (addr_6502 >> 8));
  index = jit_emit_push_constant(p_jit_buf, index, (addr_6502 & 0xff));

  return index;
}

static size_t
jit_emit_flags_to_scratch(unsigned char* p_jit, size_t index, int is_brk) {
  unsigned char brk_and_set_bit = 0x20;
  if (is_brk) {
    brk_and_set_bit += 0x10;
  }

  /* lahf */
  p_jit[index++] = 0x9f;

  /* r13 is IF and DF */
  /* r14 is CF */
  /* lea rdx, [r13 + r14 + brk_and_set_bit] */
  p_jit[index++] = 0x4b;
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x54;
  p_jit[index++] = 0x35;
  p_jit[index++] = brk_and_set_bit;

  /* r12 is OF */
  /* mov rdi, r12 */
  p_jit[index++] = 0x4c;
  p_jit[index++] = 0x89;
  p_jit[index++] = 0xe7;
  /* shl edi, 6 */
  p_jit[index++] = 0xc1;
  p_jit[index++] = 0xe7;
  p_jit[index++] = 0x06;
  /* lea edx, [rdx + rdi] */
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x14;
  p_jit[index++] = 0x3a;

  /* Intel flags bit 6 is ZF, 6502 flags bit 1 */
  /* movzx edi, ah */
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0xfc;
  /* and edi, 0x40 */
  p_jit[index++] = 0x83;
  p_jit[index++] = 0xe7;
  p_jit[index++] = 0x40;
  /* shr edi, 5 */
  p_jit[index++] = 0xc1;
  p_jit[index++] = 0xef;
  p_jit[index++] = 0x05;
  /* lea edx, [rdx + rdi] */
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x14;
  p_jit[index++] = 0x3a;

  /* Intel flags bit 7 is SF, 6502 flags bit 7 */
  /* movzx edi, ah */
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0xb6;
  p_jit[index++] = 0xfc;
  /* and edi, 0x80 */
  p_jit[index++] = 0x83;
  p_jit[index++] = 0xe7;
  p_jit[index++] = 0x80;
  /* lea edx, [rdx + rdi] */
  p_jit[index++] = 0x8d;
  p_jit[index++] = 0x14;
  p_jit[index++] = 0x3a;

  /* sahf */
  p_jit[index++] = 0x9e;

  return index;
}

static size_t
jit_emit_set_flags(unsigned char* p_jit_buf, size_t index) {
  index = jit_emit_scratch_bit_test(p_jit_buf, index, 0);
  index = jit_emit_intel_to_6502_carry(p_jit_buf, index);
  index = jit_emit_scratch_bit_test(p_jit_buf, index, 6);
  index = jit_emit_carry_to_6502_overflow(p_jit_buf, index);
  /* I and D flags */
  /* mov r13b, dl */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0xd5;
  /* and r13b, 0xc */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x80;
  p_jit_buf[index++] = 0xe5;
  p_jit_buf[index++] = 0x0c;
  /* ZF */
  /* mov ah, dl */
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0xd4;
  /* and ah, 2 */
  p_jit_buf[index++] = 0x80;
  p_jit_buf[index++] = 0xe4;
  p_jit_buf[index++] = 0x02;
  /* shl ah, 5 */
  p_jit_buf[index++] = 0xc0;
  p_jit_buf[index++] = 0xe4;
  p_jit_buf[index++] = 0x05;
  /* NF */
  /* and dl, 0x80 */
  p_jit_buf[index++] = 0x80;
  p_jit_buf[index++] = 0xe2;
  p_jit_buf[index++] = 0x80;
  /* or ah, dl */
  p_jit_buf[index++] = 0x08;
  p_jit_buf[index++] = 0xd4;

  /* sahf */
  p_jit_buf[index++] = 0x9e;

  return index;
}

static size_t
jit_emit_jmp_from_6502_scratch(struct jit_struct* p_jit,
                               unsigned char* p_jit_buf,
                               size_t index) {
  index = jit_emit_jit_bytes_shift_scratch_left(p_jit_buf, index);
  /* lea edx, [rdx + p_jit_base] */
  p_jit_buf[index++] = 0x8d;
  p_jit_buf[index++] = 0x92;
  index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_jit_base);
  index = jit_emit_jmp_scratch(p_jit_buf, index);

  return index;
}

static size_t
jit_emit_jmp_indirect(struct jit_struct* p_jit,
                      unsigned char* p_jit_buf,
                      size_t index,
                      uint16_t addr_6502) {
  assert((addr_6502 & 0xff) != 0xff);
  /* movzx edx, WORD PTR [p_mem + addr] */
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xb7;
  p_jit_buf[index++] = 0x14;
  p_jit_buf[index++] = 0x25;
  index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem + addr_6502);
  index = jit_emit_jmp_from_6502_scratch(p_jit, p_jit_buf, index);

  return index;
}

static size_t
jit_emit_undefined(unsigned char* p_jit,
                   size_t index,
                   unsigned char opcode,
                   size_t addr_6502) {
  /* ud2 */
  p_jit[index++] = 0x0f;
  p_jit[index++] = 0x0b;
  /* Copy of unimplemented 6502 opcode. */
  p_jit[index++] = opcode;
  /* Virtual address of opcode, big endian. */
  p_jit[index++] = addr_6502 >> 8;
  p_jit[index++] = addr_6502 & 0xff;

  return index;
}

static size_t
jit_emit_save_registers(unsigned char* p_jit, size_t index) {
  /* The flags we need to save are negative and zero, both covered by lahf. */
  /* lahf */
  p_jit[index++] = 0x9f;
  /* No need to push rdx or rdi because they are scratch registers. */
  /* push rax / rcx / rsi */
  p_jit[index++] = 0x50;
  p_jit[index++] = 0x51;
  p_jit[index++] = 0x56;

  return index;
}

static size_t
jit_emit_restore_registers(unsigned char* p_jit, size_t index) {
  /* pop rsi / rcx / rax */
  p_jit[index++] = 0x5e;
  p_jit[index++] = 0x59;
  p_jit[index++] = 0x58;
  /* sahf */
  p_jit[index++] = 0x9e;

  return index;
}

static void
jit_emit_debug_util(unsigned char* p_jit_buf) {
  size_t index = 0;
  index = jit_emit_save_registers(p_jit_buf, index);

  /* 6502 A */
  /* mov [r15 + k_offset_reg_a], al */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0x47;
  p_jit_buf[index++] = k_offset_reg_a;

  /* 6502 X */
  /* mov [r15 + k_offset_reg_x], bl */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0x5f;
  p_jit_buf[index++] = k_offset_reg_x;

  /* 6502 Y */
  /* mov [r15 + k_offset_reg_y], cl */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0x4f;
  p_jit_buf[index++] = k_offset_reg_y;

  /* 6502 S */
  /* mov [r15 + k_offset_reg_s], sil */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0x77;
  p_jit_buf[index++] = k_offset_reg_s;

  /* 6502 IP */
  /* Must come before flags because gathering flags trashes dx. */
  /* mov [r15 + k_offset_reg_pc], dx */
  p_jit_buf[index++] = 0x66;
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_reg_pc;

  /* 6502 flags */
  index = jit_emit_flags_to_scratch(p_jit_buf, index, 1);
  /* mov [r15 + k_offset_reg_flags], dl */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_reg_flags;

  /* param1 */
  /* mov rdi, [r15 + k_offset_debug] */
  p_jit_buf[index++] = 0x49;
  p_jit_buf[index++] = 0x8b;
  p_jit_buf[index++] = 0x7f;
  p_jit_buf[index++] = k_offset_debug;

  /* call [r15 + k_offset_debug_callback] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0xff;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_debug_callback;

  index = jit_emit_restore_registers(p_jit_buf, index);

  /* call [r15 + k_offset_util_regs] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0xff;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_util_regs;

  /* ret */
  p_jit_buf[index++] = 0xc3;
}

static void
jit_emit_regs_util(struct jit_struct* p_jit, unsigned char* p_jit_buf) {
  size_t index = 0;

  /* Set A. */
  /* mov al, [r15 + k_offset_reg_a] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x8a;
  p_jit_buf[index++] = 0x47;
  p_jit_buf[index++] = k_offset_reg_a;

  /* Set X. */
  /* mov bl, [r15 + k_offset_reg_x] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x8a;
  p_jit_buf[index++] = 0x5f;
  p_jit_buf[index++] = k_offset_reg_x;

  /* Set Y. */
  /* mov cl, [r15 + k_offset_reg_y] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x8a;
  p_jit_buf[index++] = 0x4f;
  p_jit_buf[index++] = k_offset_reg_y;

  /* Set S. */
  /* mov sil, [r15 + k_offset_reg_s] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x8a;
  p_jit_buf[index++] = 0x77;
  p_jit_buf[index++] = k_offset_reg_s;

  /* Set flags. */
  /* mov dl, [r15 + k_offset_reg_flags] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x8a;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_reg_flags;
  index = jit_emit_set_flags(p_jit_buf, index);

  /* Load PC into edx. */
  /* movzx edx, WORD PTR [r15 + k_offset_reg_pc] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xb7;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_reg_pc;

  /* ret */
  p_jit_buf[index++] = 0xc3;
}

static void
jit_emit_jit_util(struct jit_struct* p_jit, unsigned char* p_jit_buf) {
  size_t index = 0;

  /* Save calling rip. */
  /* mov rdx, [rsp] */
  p_jit_buf[index++] = 0x48;
  p_jit_buf[index++] = 0x8b;
  p_jit_buf[index++] = 0x14;
  p_jit_buf[index++] = 0x24;

  index = jit_emit_save_registers(p_jit_buf, index);

  /* param1: jit_struct pointer. */
  /* mov rdi, r15 */
  p_jit_buf[index++] = 0x4c;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0xff;

  /* param2: x64 rip that call'ed here. */
  /* mov rsi, rdx */
  p_jit_buf[index++] = 0x48;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0xd6;

  /* call [r15 + k_offset_jit_callback] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0xff;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_jit_callback;

  index = jit_emit_restore_registers(p_jit_buf, index);

  /* movzx edx, WORD PTR [r15 + k_offset_reg_pc] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xb7;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_reg_pc;

  /* We are jumping out of a call, so need to pop the return value. */
  /* pop rdi */
  p_jit_buf[index++] = 0x5f;

  index = jit_emit_jmp_from_6502_scratch(p_jit, p_jit_buf, index);
}

static void
jit_emit_debug_sequence(struct util_buffer* p_buf, uint16_t addr_6502) {
  /* mov dx, addr_6502 */
  util_buffer_add_2b_1w(p_buf, 0x66, 0xba, addr_6502);
  /* call [r15 + k_offset_util_debug] */
  util_buffer_add_4b(p_buf, 0x41, 0xff, 0x57, k_offset_util_debug);
}

static size_t
jit_check_special_read(struct jit_struct* p_jit,
                       uint16_t addr_6502,
                       unsigned char* p_jit_buf,
                       size_t index) {
  if (!bbc_is_special_read_addr(p_jit->p_bbc, addr_6502)) {
    return index;
  }
  index = jit_emit_save_registers(p_jit_buf, index);

  /* mov rdi, [r15 + k_offset_bbc] */
  p_jit_buf[index++] = 0x49;
  p_jit_buf[index++] = 0x8b;
  p_jit_buf[index++] = 0x7f;
  p_jit_buf[index++] = k_offset_bbc;
  /* mov si, addr_6502 */
  p_jit_buf[index++] = 0x66;
  p_jit_buf[index++] = 0xbe;
  p_jit_buf[index++] = addr_6502 & 0xff;
  p_jit_buf[index++] = addr_6502 >> 8;
  /* call [r15 + k_offset_read_callback] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0xff;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_read_callback;
  /* mov rdx, rax */
  p_jit_buf[index++] = 0x48;
  p_jit_buf[index++] = 0x89;
  p_jit_buf[index++] = 0xc2;

  /* mov [p_mem + addr_6502], dl */
  p_jit_buf[index++] = 0x88;
  p_jit_buf[index++] = 0x14;
  p_jit_buf[index++] = 0x25;
  index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem + addr_6502);

  index = jit_emit_restore_registers(p_jit_buf, index);

  return index;
}

static size_t
jit_check_special_write(struct jit_struct* p_jit,
                        uint16_t addr,
                        unsigned char* p_jit_buf,
                        size_t index) {
  if (!bbc_is_special_write_addr(p_jit->p_bbc, addr)) {
    return index;
  }
  index = jit_emit_save_registers(p_jit_buf, index);

  /* mov rdi, [r15 + k_offset_bbc] */
  p_jit_buf[index++] = 0x49;
  p_jit_buf[index++] = 0x8b;
  p_jit_buf[index++] = 0x7f;
  p_jit_buf[index++] = k_offset_bbc;
  /* mov si, addr */
  p_jit_buf[index++] = 0x66;
  p_jit_buf[index++] = 0xbe;
  p_jit_buf[index++] = addr & 0xff;
  p_jit_buf[index++] = addr >> 8;
  /* call [r15 + k_offset_write_callback] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0xff;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_write_callback;

  index = jit_emit_restore_registers(p_jit_buf, index);

  return index;
}

static size_t
jit_emit_calc_op(struct jit_struct* p_jit,
                 unsigned char* p_jit_buf,
                 size_t index,
                 unsigned char opmode,
                 unsigned char operand1,
                 unsigned char operand2,
                 unsigned char intel_op_base) {
  uint16_t addr_6502 = (operand2 << 8) | operand1;
  switch (opmode) {
  case k_imm:
    /* OP al, op1 */
    p_jit_buf[index++] = intel_op_base + 2;
    p_jit_buf[index++] = operand1;
    break;
  case k_zpg:
  case k_abs:
    index = jit_check_special_read(p_jit, addr_6502, p_jit_buf, index);
    /* OP al, [p_mem + addr] */
    p_jit_buf[index++] = intel_op_base;
    p_jit_buf[index++] = 0x04;
    p_jit_buf[index++] = 0x25;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem + addr_6502);
    break;
  case k_idy:
    /* OP al, [rdx + rcx] */
    p_jit_buf[index++] = intel_op_base;
    p_jit_buf[index++] = 0x04;
    p_jit_buf[index++] = 0x0a;
    break;
  case k_abx:
    /* OP al, [rbx + addr_6502] */
    p_jit_buf[index++] = intel_op_base;
    p_jit_buf[index++] = 0x83;
    index = jit_emit_int(p_jit_buf, index, addr_6502);
    break;
  case k_aby:
    /* OP al, [rcx + addr_6502] */
    p_jit_buf[index++] = intel_op_base;
    p_jit_buf[index++] = 0x81;
    index = jit_emit_int(p_jit_buf, index, addr_6502);
    break;
  case k_zpx:
  case k_idx:
    /* OP al, [rdx + p_mem] */
    p_jit_buf[index++] = intel_op_base;
    p_jit_buf[index++] = 0x82;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
    break;
  default:
    assert(0);
    break;
  }

  return index;
}

static size_t
jit_emit_shift_op(struct jit_struct* p_jit,
                  unsigned char* p_jit_buf,
                  size_t index,
                  unsigned char opmode,
                  unsigned char operand1,
                  unsigned char operand2,
                  unsigned char intel_op_base,
                  size_t n_count) {
  assert(n_count < 8);
  uint16_t addr_6502 = (operand2 << 8) | operand1;
  unsigned char first_byte = 0xd0;
  if (n_count > 1) {
    first_byte = 0xc0;
  }
  switch (opmode) {
  case k_nil:
    /* OP al, n */
    p_jit_buf[index++] = first_byte;
    p_jit_buf[index++] = intel_op_base;
    break;
  case k_zpg:
  case k_abs:
    /* OP BYTE PTR [p_mem + addr], n */
    p_jit_buf[index++] = first_byte;
    p_jit_buf[index++] = intel_op_base - 0xbc;
    p_jit_buf[index++] = 0x25;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem + addr_6502);
    break;
  case k_zpx:
    /* OP BYTE PTR [rdx + p_mem], n */
    p_jit_buf[index++] = first_byte;
    p_jit_buf[index++] = intel_op_base - 0x3e;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
    break;
  case k_abx:
    /* OP BYTE PTR [rbx + addr_6502], n */
    p_jit_buf[index++] = first_byte;
    p_jit_buf[index++] = intel_op_base - 0x3d;
    index = jit_emit_int(p_jit_buf, index, addr_6502);
    break;
  default:
    assert(0);
    break;
  }

  if (n_count > 1) {
    p_jit_buf[index++] = n_count;
  }

  return index;
}

static size_t
jit_emit_post_rotate(struct jit_struct* p_jit,
                     unsigned char* p_jit_buf,
                     size_t index,
                     unsigned char opmode,
                     unsigned char operand1,
                     unsigned char operand2) {
  uint16_t addr_6502 = (operand2 << 8) | operand1;
  index = jit_emit_intel_to_6502_carry(p_jit_buf, index);
  switch (opmode) {
  case k_nil:
    index = jit_emit_do_zn_flags(p_jit_buf, index, 0);
    break;
  case k_zpg:
  case k_abs:
    /* test BYTE PTR [p_mem + addr], 0xff */
    p_jit_buf[index++] = 0xf6;
    p_jit_buf[index++] = 0x04;
    p_jit_buf[index++] = 0x25;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem + addr_6502);
    p_jit_buf[index++] = 0xff;
    break;
  case k_zpx:
    /* test BYTE PTR [rdx + p_mem], 0xff */
    p_jit_buf[index++] = 0xf6;
    p_jit_buf[index++] = 0x82;
    index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
    p_jit_buf[index++] = 0xff;
    break;
  case k_abx:
    /* test BYTE PTR [rbx + addr_6502], 0xff */
    p_jit_buf[index++] = 0xf6;
    p_jit_buf[index++] = 0x83;
    index = jit_emit_int(p_jit_buf, index, addr_6502);
    p_jit_buf[index++] = 0xff;
    break;
  default:
    assert(0);
    break;
  }

  return index;
}

static size_t
jit_emit_sei(unsigned char* p_jit_buf, size_t index) {
  /* bts r13, 2 */
  p_jit_buf[index++] = 0x49;
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xba;
  p_jit_buf[index++] = 0xed;
  p_jit_buf[index++] = 0x02;

  return index;
}

static size_t
jit_emit_do_interrupt(struct jit_struct* p_jit,
                      unsigned char* p_jit_buf,
                      size_t index,
                      uint16_t addr_6502,
                      int is_brk) {
  uint16_t vector = k_bbc_vector_irq;
  index = jit_emit_push_addr(p_jit_buf, index, addr_6502);
  index = jit_emit_flags_to_scratch(p_jit_buf, index, is_brk);
  index = jit_emit_push_from_scratch(p_jit_buf, index);
  index = jit_emit_sei(p_jit_buf, index);
  index = jit_emit_jmp_indirect(p_jit, p_jit_buf, index, vector);

  return index;
}

static size_t
jit_emit_check_interrupt(struct jit_struct* p_jit,
                         unsigned char* p_jit_buf,
                         size_t index,
                         uint16_t addr_6502,
                         int check_flag) {
  size_t index_jmp1 = 0;
  size_t index_jmp2 = 0;
  /* bt DWORD PTR [r15 + k_offset_interrupt], 0 */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0x0f;
  p_jit_buf[index++] = 0xba;
  p_jit_buf[index++] = 0x67;
  p_jit_buf[index++] = k_offset_interrupt;
  p_jit_buf[index++] = 0x00;
  /* jae / jnc ... */
  p_jit_buf[index++] = 0x73;
  p_jit_buf[index++] = 0xfe;
  index_jmp1 = index;

  if (check_flag) {
    /* bt r13, 2 */
    p_jit_buf[index++] = 0x49;
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xba;
    p_jit_buf[index++] = 0xe5;
    p_jit_buf[index++] = 0x02;
    /* jb ... */
    p_jit_buf[index++] = 0x72;
    p_jit_buf[index++] = 0xfe;
    index_jmp2 = index;
  }

  index = jit_emit_do_interrupt(p_jit, p_jit_buf, index, addr_6502, 0);

  if (index_jmp1) {
    p_jit_buf[index_jmp1 - 1] = index - index_jmp1;
  }
  if (index_jmp2) {
    p_jit_buf[index_jmp2 - 1] = index - index_jmp2;
  }

  return index;
}

void
jit_set_interrupt(struct jit_struct* p_jit, int interrupt) {
  p_jit->interrupt = interrupt;
}

static unsigned char
jit_get_opcode(struct jit_struct* p_jit, uint16_t addr_6502) {
  unsigned char* p_mem = p_jit->p_mem;
  return p_mem[addr_6502];
}

static size_t
jit_emit_do_jit(unsigned char* p_jit_buf, size_t index) {
  /* call [r15 + k_offset_util_jit] */
  p_jit_buf[index++] = 0x41;
  p_jit_buf[index++] = 0xff;
  p_jit_buf[index++] = 0x57;
  p_jit_buf[index++] = k_offset_util_jit;

  return index;
}

static size_t
jit_single(struct jit_struct* p_jit,
           struct util_buffer* p_buf,
           uint16_t addr_6502) {
  unsigned char* p_mem = p_jit->p_mem;
  unsigned char* p_jit_buf = util_buffer_get_ptr(p_buf);

  uint16_t addr_6502_plus_1 = addr_6502 + 1;
  uint16_t addr_6502_plus_2 = addr_6502 + 2;

  unsigned char opcode = jit_get_opcode(p_jit, addr_6502);
  unsigned char operand1 = p_mem[addr_6502_plus_1];
  unsigned char operand2 = p_mem[addr_6502_plus_2];
  uint16_t addr_6502_relative_jump = (int) addr_6502 + 2 + (char) operand1;

  unsigned char opmode = g_opmodes[opcode];
  unsigned char optype = g_optypes[opcode];
  unsigned char opmem = g_opmem[optype];
  unsigned char oplen = g_opmodelens[opmode];
  uint16_t opcode_addr_6502;
  size_t index = util_buffer_get_pos(p_buf);
  size_t num_6502_bytes = 0;
  size_t n_count = 1;

  if (p_jit->jit_flags & k_jit_flag_debug) {
    jit_emit_debug_sequence(p_buf, addr_6502);
    index = util_buffer_get_pos(p_buf);
  }

  switch (opmode) {
  case k_zpx:
    index = jit_emit_zp_x_to_scratch(p_jit_buf, index, operand1);
    break;
  case k_zpy:
    index = jit_emit_zp_y_to_scratch(p_jit_buf, index, operand1);
    break;
  case k_abx:
    index = jit_emit_abs_x_to_scratch(p_jit_buf, index, operand1, operand2);
    break;
  case k_aby:
    index = jit_emit_abs_y_to_scratch(p_jit_buf, index, operand1, operand2);
    break;
  case k_idy:
    index = jit_emit_ind_y_to_scratch(p_jit, p_jit_buf, index, operand1);
    break;
  case k_idx:
    index = jit_emit_ind_x_to_scratch(p_jit, p_jit_buf, index, operand1);
    break;
  default:
    break;
  }

  num_6502_bytes += oplen;

  if (oplen < 3) { 
    /* Clear operand2 if we're not using it. This enables us to re-use the
     * same x64 opcode generation code for both k_zpg and k_abs.
     */
    operand2 = 0;
  }
  opcode_addr_6502 = (operand2 << 8) | operand1;

  /* Handle merging repeated shift / rotate instructions. */
  if (p_jit->jit_flags & k_jit_flag_merge_ops) {
    if (optype == k_lsr ||
        optype == k_asl ||
        optype == k_rol ||
        optype == k_ror) {
      if (opmode == k_nil) {
        uint16_t next_addr_6502 = addr_6502 + 1;
        while (n_count < 7 && p_mem[next_addr_6502] == opcode) {
          n_count++;
          next_addr_6502++;
          num_6502_bytes++;
        }
      }
    }
  }

  switch (optype) {
  case k_kil:
    switch (opcode) {
    case 0x02:
      /* Illegal opcode. Hangs a standard 6502. */
      /* Bounce out of JIT. */
      /* ret */
      p_jit_buf[index++] = 0xc3;
      break;
    case 0x12:
      /* Illegal opcode. Hangs a standard 6502. */
      /* Generate a debug trap and continue. */
      /* int 3 */
      p_jit_buf[index++] = 0xcc;
      break;
    case 0xf2:
      /* Illegal opcode. Hangs a standard 6502. */
      /* Generate a SEGV. */
      /* xor rdx, rdx */
      p_jit_buf[index++] = 0x31;
      p_jit_buf[index++] = 0xd2;
      index = jit_emit_jmp_scratch(p_jit_buf, index);
      break;
    default:
      index = jit_emit_undefined(p_jit_buf, index, opcode, addr_6502);
      break;
    }
    break;
  case k_brk:
    /* BRK */
    index = jit_emit_do_interrupt(p_jit, p_jit_buf, index, addr_6502 + 2, 1);
    break;
  case k_ora:
    /* ORA */
    index = jit_emit_calc_op(p_jit,
                             p_jit_buf,
                             index,
                             opmode,
                             operand1,
                             operand2,
                             0x0a);
    break;
  case k_asl:
    /* ASL */
    index = jit_emit_shift_op(p_jit,
                              p_jit_buf,
                              index,
                              opmode,
                              operand1,
                              operand2,
                              0xe0,
                              n_count);
    index = jit_emit_intel_to_6502_carry(p_jit_buf, index);
    break;
  case k_php:
    /* PHP */
    index = jit_emit_flags_to_scratch(p_jit_buf, index, 1);
    index = jit_emit_push_from_scratch(p_jit_buf, index);
    break;
  case k_bpl:
    /* BPL */
    /* jns */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x89);
    break;
  case k_clc:
    /* CLC */
    index = jit_emit_set_carry(p_jit_buf, index, 0);
    break;
  case k_jsr:
    /* JSR */
    index = jit_emit_push_addr(p_jit_buf, index, addr_6502 + 2);
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit, p_buf, opcode_addr_6502, 0xe9, 0);
    break;
  case k_bit:
    /* BIT */
    /* Only has zp and abs. */
    assert(opmode == k_zpg || opmode == k_abs);
    index = jit_check_special_read(p_jit, opcode_addr_6502, p_jit_buf, index);
    /* mov ah, [p_mem + addr] */
    p_jit_buf[index++] = 0x8a;
    p_jit_buf[index++] = 0x24;
    p_jit_buf[index++] = 0x25;
    index = jit_emit_int(p_jit_buf,
                         index,
                         (size_t) p_jit->p_mem + opcode_addr_6502);

    /* Bit 14 of eax is bit 6 of ah, where we get the OF from. */
    /* bt eax, 14 */
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xba;
    p_jit_buf[index++] = 0xe0;
    p_jit_buf[index++] = 14;
    index = jit_emit_carry_to_6502_overflow(p_jit_buf, index);

    /* Set ZF. */
    /* test ah, al */
    p_jit_buf[index++] = 0x84;
    p_jit_buf[index++] = 0xc4;
    /* sete dl */
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0x94;
    p_jit_buf[index++] = 0xc2;
    /* x64 ZF is bit 6 in flags. */
    /* shl dl, 6 */
    p_jit_buf[index++] = 0xc0;
    p_jit_buf[index++] = 0xe2;
    p_jit_buf[index++] = 6;

    /* Set NF. With a trick: NF is bit 7 in both x64 and 6502 flags. */
    /* and ah, 0x80 */
    p_jit_buf[index++] = 0x80;
    p_jit_buf[index++] = 0xe4;
    p_jit_buf[index++] = 0x80;

    /* Put new flags into Intel flags. */
    /* or ah, dl */
    p_jit_buf[index++] = 0x08;
    p_jit_buf[index++] = 0xd4;
    /* sahf */
    p_jit_buf[index++] = 0x9e;
    break;
  case k_and:
    /* AND */
    index = jit_emit_calc_op(p_jit,
                             p_jit_buf,
                             index,
                             opmode,
                             operand1,
                             operand2,
                             0x22);
    break;
  case k_rol:
    /* ROL */
    index = jit_emit_6502_carry_to_intel(p_jit_buf, index);
    index = jit_emit_shift_op(p_jit,
                              p_jit_buf,
                              index,
                              opmode,
                              operand1,
                              operand2,
                              0xd0,
                              n_count);
    index = jit_emit_post_rotate(p_jit,
                                 p_jit_buf,
                                 index,
                                 opmode,
                                 operand1,
                                 operand2);
    break;
  case k_plp:
    /* PLP */
    index = jit_emit_pull_to_scratch(p_jit_buf, index);
    index = jit_emit_set_flags(p_jit_buf, index);
    index = jit_emit_check_interrupt(p_jit, p_jit_buf, index, addr_6502 + 1, 1);
    break;
  case k_bmi:
    /* BMI */
    /* js */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x88);
    break;
  case k_sec:
    /* SEC */
    index = jit_emit_set_carry(p_jit_buf, index, 1);
    break;
  case k_eor:
    /* EOR */
    index = jit_emit_calc_op(p_jit,
                             p_jit_buf,
                             index,
                             opmode,
                             operand1,
                             operand2,
                             0x32);
    break;
  case k_lsr:
    /* LSR */
    index = jit_emit_shift_op(p_jit,
                              p_jit_buf,
                              index,
                              opmode,
                              operand1,
                              operand2,
                              0xe8,
                              n_count);
    index = jit_emit_intel_to_6502_carry(p_jit_buf, index);
    break;
  case k_pha:
    /* PHA */
    index = jit_emit_push_from_a(p_jit_buf, index);
    break;
  case k_jmp:
    /* JMP */
    if (opmode == k_abs) {
      index = jit_emit_jmp_6502_addr(p_jit,
                                     p_buf,
                                     opcode_addr_6502,
                                     0xe9,
                                     0);
    } else {
      index = jit_emit_jmp_indirect(p_jit,
                                    p_jit_buf,
                                    index,
                                    opcode_addr_6502);
    }
    break;
  case k_bvc:
    /* BVC */
    index = jit_emit_test_overflow(p_jit_buf, index);
    /* jae / jnc */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x83);
    break;
  case k_cli:
    /* CLI */
    /* btr r13, 2 */
    p_jit_buf[index++] = 0x49;
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xba;
    p_jit_buf[index++] = 0xf5;
    p_jit_buf[index++] = 0x02;
    index = jit_emit_check_interrupt(p_jit, p_jit_buf, index, addr_6502 + 1, 0);
    break;
  case k_rti:
    index = jit_emit_pull_to_scratch(p_jit_buf, index);
    index = jit_emit_set_flags(p_jit_buf, index);
    index = jit_emit_pull_to_scratch_word(p_jit_buf, index);
    index = jit_emit_jmp_from_6502_scratch(p_jit, p_jit_buf, index);
    break;
  case k_rts:
    /* RTS */
    index = jit_emit_pull_to_scratch_word(p_jit_buf, index);
    /* lea dx, [rdx + 1] */
    p_jit_buf[index++] = 0x66;
    p_jit_buf[index++] = 0x8d;
    p_jit_buf[index++] = 0x52;
    p_jit_buf[index++] = 0x01;
    index = jit_emit_jmp_from_6502_scratch(p_jit, p_jit_buf, index);
    break;
  case k_adc:
    /* ADC */
    index = jit_emit_6502_carry_to_intel(p_jit_buf, index);
    index = jit_emit_calc_op(p_jit,
                             p_jit_buf,
                             index,
                             opmode,
                             operand1,
                             operand2,
                             0x12);
    index = jit_emit_intel_to_6502_co(p_jit_buf, index);
    break;
  case k_ror:
    /* ROR */
    index = jit_emit_6502_carry_to_intel(p_jit_buf, index);
    index = jit_emit_shift_op(p_jit,
                              p_jit_buf,
                              index,
                              opmode,
                              operand1,
                              operand2,
                              0xd8,
                              n_count);
    index = jit_emit_post_rotate(p_jit,
                                 p_jit_buf,
                                 index,
                                 opmode,
                                 operand1,
                                 operand2);
    break;
  case k_pla:
    /* PLA */
    index = jit_emit_pull_to_a(p_jit_buf, index);
    break;
  case k_bvs:
    /* BVS */
    index = jit_emit_test_overflow(p_jit_buf, index);
    /* jb / jc */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x82);
    break;
  case k_sei:
    /* SEI */
    index = jit_emit_sei(p_jit_buf, index);
    break;
  case k_sta:
    /* STA */
    switch (opmode) {
    case k_zpg:
    case k_abs:
      /* mov [p_mem + addr], al */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x04;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      index = jit_check_special_write(p_jit,
                                      opcode_addr_6502,
                                      p_jit_buf,
                                      index);
      break;
    case k_idy:
      /* mov [rdx + rcx], al */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x04;
      p_jit_buf[index++] = 0x0a;
      break;
    case k_abx:
      /* mov [rbx + addr_6502], al */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x83;
      index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
      break;
    case k_aby:
      /* mov [rcx + addr_6502], al */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x81;
      index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
      break;
    case k_zpx:
    case k_idx:
      /* mov [rdx + p_mem], al */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x82;
      index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_sty:
    /* STY */
    switch (opmode) {
    case k_zpg:
    case k_abs:
      /* mov [p_mem + addr], cl */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x0c;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      index = jit_check_special_write(p_jit,
                                      opcode_addr_6502,
                                      p_jit_buf,
                                      index);
      break;
    case k_zpx:
      /* mov [rdx + p_mem], cl */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x8a;
      index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_stx:
    /* STX */
    switch (opmode) {
    case k_zpg:
    case k_abs:
      /* mov [p_mem + addr], bl */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x1c;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      index = jit_check_special_write(p_jit,
                                      opcode_addr_6502,
                                      p_jit_buf,
                                      index);
      break;
    case k_zpy:
      /* mov [rdx + p_mem], bl */
      p_jit_buf[index++] = 0x88;
      p_jit_buf[index++] = 0x9a;
      index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_dey:
    /* DEY */
    /* dec cl */
    p_jit_buf[index++] = 0xfe;
    p_jit_buf[index++] = 0xc9;
    break;
  case k_txa:
    /* TXA */
    /* mov al, bl */
    p_jit_buf[index++] = 0x88;
    p_jit_buf[index++] = 0xd8;
    break;
  case k_bcc:
    /* BCC */
    index = jit_emit_test_carry(p_jit_buf, index);
    /* jae / jnc */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x83);
    break;
  case k_tya:
    /* TYA */
    /* mov al, cl */
    p_jit_buf[index++] = 0x88;
    p_jit_buf[index++] = 0xc8;
    break;
  case k_txs:
    /* TXS */
    /* mov sil, bl */
    p_jit_buf[index++] = 0x40;
    p_jit_buf[index++] = 0x88;
    p_jit_buf[index++] = 0xde;
    break;
  case k_ldy:
    /* LDY */
    switch (opmode) {
    case k_imm:
      /* mov cl, op1 */
      p_jit_buf[index++] = 0xb1;
      p_jit_buf[index++] = operand1;
      break;
    case k_zpg:
    case k_abs:
      index = jit_check_special_read(p_jit,
                                     opcode_addr_6502,
                                     p_jit_buf,
                                     index);
      /* mov cl, [p_mem + addr] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x0c;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    case k_zpx:
      /* mov cl, [rdx + p_mem] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x8a;
      index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
      break;
    case k_abx:
      /* mov cl, [rbx + addr_6502] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x8b;
      index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_ldx:
    /* LDX */
    switch (opmode) {
    case k_imm:
      /* mov bl, op1 */
      p_jit_buf[index++] = 0xb3;
      p_jit_buf[index++] = operand1;
      break;
    case k_zpg:
    case k_abs:
      index = jit_check_special_read(p_jit,
                                     opcode_addr_6502,
                                     p_jit_buf,
                                     index);
      /* mov bl, [p_mem + addr] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x1c;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    case k_zpy:
      /* mov bl, [rdx + p_mem] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x9a;
      index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
      break;
    case k_aby:
      /* mov bl, [rcx + addr_6502] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x9b;
      index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_lda:
    /* LDA */
    switch (opmode) {
    case k_imm:
      /* mov al, op1 */
      p_jit_buf[index++] = 0xb0;
      p_jit_buf[index++] = operand1;
      break;
    case k_zpg:
    case k_abs:
      index = jit_check_special_read(p_jit,
                                     opcode_addr_6502,
                                     p_jit_buf,
                                     index);
      /* mov al, [p_mem + addr] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x04;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    case k_idy:
      /* mov al, [rdx + rcx] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x04;
      p_jit_buf[index++] = 0x0a;
      break;
    case k_abx:
      /* mov al, [rbx + addr_6502] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x83;
      index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
      break;
    case k_aby:
      /* mov al, [rcx + addr_6502] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x81;
      index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
      break;
    case k_zpx:
    case k_idx:
      /* mov al, [rdx + p_mem] */
      p_jit_buf[index++] = 0x8a;
      p_jit_buf[index++] = 0x82;
      index = jit_emit_int(p_jit_buf, index, (size_t) p_jit->p_mem);
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_tay:
    /* TAY */
    /* mov cl, al */
    p_jit_buf[index++] = 0x88;
    p_jit_buf[index++] = 0xc1;
    break;
  case k_tax:
    /* TAX */
    /* mov bl, al */
    p_jit_buf[index++] = 0x88;
    p_jit_buf[index++] = 0xc3;
    break;
  case k_bcs:
    /* BCS */
    index = jit_emit_test_carry(p_jit_buf, index);
    /* jb / jc */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x82);
    break;
  case k_clv:
    /* CLV */
    /* mov r12b, 0 */
    p_jit_buf[index++] = 0x41;
    p_jit_buf[index++] = 0xb4;
    p_jit_buf[index++] = 0x00;
    break;
  case k_tsx:
    /* TSX */
    /* mov bl, sil */
    p_jit_buf[index++] = 0x40;
    p_jit_buf[index++] = 0x88;
    p_jit_buf[index++] = 0xf3;
    break;
  case k_cpy:
    /* CPY */
    switch (opmode) {
    case k_imm:
      /* cmp cl, op1 */
      p_jit_buf[index++] = 0x80;
      p_jit_buf[index++] = 0xf9;
      p_jit_buf[index++] = operand1;
      break;
    case k_zpg:
    case k_abs:
      /* cmp cl, [p_mem + addr] */
      p_jit_buf[index++] = 0x3a;
      p_jit_buf[index++] = 0x0c;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    default:
      assert(0);
      break;
    }
    index = jit_emit_intel_to_6502_sub_carry(p_jit_buf, index);
    break;
  case k_cmp:
    /* CMP */
    index = jit_emit_calc_op(p_jit,
                             p_jit_buf,
                             index,
                             opmode,
                             operand1,
                             operand2,
                             0x3a);
    index = jit_emit_intel_to_6502_sub_carry(p_jit_buf, index);
    break;
  case k_dec:
    /* DEC */
    switch (opmode) {
    case k_zpg:
    case k_abs:
      /* dec BYTE PTR [p_mem + addr] */
      p_jit_buf[index++] = 0xfe;
      p_jit_buf[index++] = 0x0c;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    case k_zpx: 
      /* dec BYTE PTR [rdx + p_mem] */
      p_jit_buf[index++] = 0xfe;
      p_jit_buf[index++] = 0x8a;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    case k_abx:
      /* dec BYTE PTR [rbx + addr_6502] */
      p_jit_buf[index++] = 0xfe;
      p_jit_buf[index++] = 0x8b;
      index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_iny:
    /* INY */
    /* inc cl */
    p_jit_buf[index++] = 0xfe;
    p_jit_buf[index++] = 0xc1;
    break;
  case k_dex:
    /* DEX */
    /* dec bl */
    p_jit_buf[index++] = 0xfe;
    p_jit_buf[index++] = 0xcb;
    break;
  case k_bne:
    /* BNE */
    /* jne */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x85);
    break;
  case k_cld:
    /* CLD */
    /* btr r13, 3 */
    p_jit_buf[index++] = 0x49;
    p_jit_buf[index++] = 0x0f;
    p_jit_buf[index++] = 0xba;
    p_jit_buf[index++] = 0xf5;
    p_jit_buf[index++] = 0x03;
    break;
  case k_cpx:
    /* CPX */
    switch (opmode) {
    case k_imm:
      /* cmp bl, op1 */
      p_jit_buf[index++] = 0x80;
      p_jit_buf[index++] = 0xfb;
      p_jit_buf[index++] = operand1;
      break;
    case k_zpg:
    case k_abs:
      /* cmp bl, [p_mem + addr] */
      p_jit_buf[index++] = 0x3a;
      p_jit_buf[index++] = 0x1c;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    default:
      assert(0);
      break;
    }
    index = jit_emit_intel_to_6502_sub_carry(p_jit_buf, index);
    break;
  case k_inc:
    /* INC */
    switch (opmode) {
    case k_zpg:
    case k_abs:
      /* inc BYTE PTR [p_mem + addr] */
      p_jit_buf[index++] = 0xfe;
      p_jit_buf[index++] = 0x04;
      p_jit_buf[index++] = 0x25;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    case k_zpx: 
      /* inc BYTE PTR [rdx + p_mem] */
      p_jit_buf[index++] = 0xfe;
      p_jit_buf[index++] = 0x82;
      index = jit_emit_int(p_jit_buf,
                           index,
                           (size_t) p_jit->p_mem + opcode_addr_6502);
      break;
    case k_abx:
      /* inc BYTE PTR [rbx + addr_6502] */
      p_jit_buf[index++] = 0xfe;
      p_jit_buf[index++] = 0x83;
      index = jit_emit_int(p_jit_buf, index, opcode_addr_6502);
      break;
    default:
      assert(0);
      break;
    }
    break;
  case k_inx:
    /* INX */
    /* inc bl */
    p_jit_buf[index++] = 0xfe;
    p_jit_buf[index++] = 0xc3;
    break;
  case k_sbc:
    /* SBC */
    index = jit_emit_6502_carry_to_intel(p_jit_buf, index);
    /* cmc */
    p_jit_buf[index++] = 0xf5;
    index = jit_emit_calc_op(p_jit,
                             p_jit_buf,
                             index,
                             opmode,
                             operand1,
                             operand2,
                             0x1a);
    index = jit_emit_intel_to_6502_sub_co(p_jit_buf, index);
    break;
  case k_nop:
    /* NOP */
    break;
  case k_beq:
    /* BEQ */
    /* je */
    util_buffer_set_pos(p_buf, index);
    index = jit_emit_jmp_6502_addr(p_jit,
                                   p_buf,
                                   addr_6502_relative_jump,
                                   0x0f,
                                   0x84);
    break;
  default:
    index = jit_emit_undefined(p_jit_buf, index, opcode, addr_6502);
    break;
  }

  /* Writes to memory invalidate the JIT there. */
  if ((p_jit->jit_flags & k_jit_flag_self_modifying) &&
      (opmem == k_write || opmem == k_rw)) {
    if (opmode == k_abs) {
      /* mov edx, DWORD PTR [r15 + k_offset_jit_ptrs + (addr * 4) */
      p_jit_buf[index++] = 0x41;
      p_jit_buf[index++] = 0x8b;
      p_jit_buf[index++] = 0x97;
      index = jit_emit_int(p_jit_buf,
                           index,
                           k_offset_jit_ptrs +
                               (opcode_addr_6502 * sizeof(unsigned int)));
      /* mov DWORD PTR [rdx], 0x??57ff41 */
      p_jit_buf[index++] = 0xc7;
      p_jit_buf[index++] = 0x02;
      index = jit_emit_do_jit(p_jit_buf, index);
    }
  }

  util_buffer_set_pos(p_buf, index);

  return num_6502_bytes;
}

static void
jit_at_addr(struct jit_struct* p_jit,
            struct util_buffer* p_buf,
            uint16_t addr_6502) {
  unsigned char* p_old_jit_addr;
  size_t num_6502_bytes;

  size_t total_num_ops = 0;
  size_t total_6502_bytes = 0;
  uint16_t start_addr_6502 = addr_6502;
  int curr_nz_flags = -1;
  int jumps_always = 0;
  unsigned char single_jit_buf[k_jit_bytes_per_byte];

  unsigned char* p_jit_buf = util_buffer_get_ptr(p_buf);
  struct util_buffer* p_single_buf = util_buffer_create();

  /* This opcode may be compiled into part of a previous basic block, so make
   * sure to replace it with a jump to the new basic block we're just
   * starting.
   */
  p_old_jit_addr = (unsigned char*) (size_t) p_jit->jit_ptrs[addr_6502];
  assert(p_old_jit_addr <= p_jit_buf);
  util_buffer_setup(p_single_buf, p_old_jit_addr, 5);
  util_buffer_set_base_address(p_single_buf, p_old_jit_addr);
  (void) jit_emit_jmp_6502_addr(p_jit,
                                p_single_buf,
                                addr_6502,
                                0xe9,
                                0);

  do {
    unsigned char opcode_6502;
    unsigned char optype;
    size_t intel_opcodes_len;
    size_t buf_left;
    unsigned char new_nz_flags;
    int nz_lazy_loaded = 0;
    unsigned char* p_dst = p_jit_buf + util_buffer_get_pos(p_buf);
    assert((size_t) p_dst < 0xffffffff);

    util_buffer_setup(p_single_buf, single_jit_buf, k_jit_bytes_per_byte);
    util_buffer_set_base_address(p_single_buf, p_dst);

    opcode_6502 = jit_get_opcode(p_jit, addr_6502);
    optype = g_optypes[opcode_6502];

    /* See if we need to lazy load the 6502 NZ flags. */
    if (g_nz_flags_needed[optype] && curr_nz_flags != -1) {
      size_t index = jit_emit_do_zn_flags(single_jit_buf, 0, curr_nz_flags - 1);
      util_buffer_set_pos(p_single_buf, index);
      nz_lazy_loaded = 1;
    }

    num_6502_bytes = jit_single(p_jit, p_single_buf, addr_6502);

    intel_opcodes_len = util_buffer_get_pos(p_single_buf);
    buf_left = util_buffer_remaining(p_buf);
    /* TODO: don't hardcode a guess at flag lazy load + jmp length. */
    if (buf_left < intel_opcodes_len + 2 + 4 + 4 + 5) {
      break;
    }

    if (g_opbranch[optype] == k_bra_y) {
      jumps_always = 1;
    }

    total_6502_bytes += num_6502_bytes;
    total_num_ops++;

    /* Store where the Intel code is for each 6502 opcode, so we can invalidate
     * Intel JIT on 6502 writes.
     */
    while (num_6502_bytes > 0) {
      p_jit->jit_ptrs[addr_6502] = (unsigned int) (size_t) p_dst;
      addr_6502++;
      num_6502_bytes--;
    }

    util_buffer_append(p_buf, p_single_buf);

    new_nz_flags = g_nz_flag_results[optype];
    if (new_nz_flags == k_a || new_nz_flags == k_x || new_nz_flags == k_y) {
      curr_nz_flags = new_nz_flags;
    } else if (new_nz_flags == k_6502 || nz_lazy_loaded) {
      curr_nz_flags = -1;
    }

    if (jumps_always) {
      break;
    }
  } while (1);

  assert(total_num_ops > 0);
/*printf("addr %x - %x, total_num_ops: %zu\n",
       start_addr_6502,
       addr_6502 - 1,
       total_num_ops);
fflush(stdout);*/

  util_buffer_destroy(p_single_buf);

  if (jumps_always) {
    return;
  }

  /* See if we need to lazy load the 6502 NZ flags. */
  if (curr_nz_flags != -1) {
    size_t index = util_buffer_get_pos(p_buf);
    index = jit_emit_do_zn_flags(p_jit_buf, index, curr_nz_flags - 1);
    util_buffer_set_pos(p_buf, index);
  }

  (void) jit_emit_jmp_6502_addr(p_jit,
                                p_buf,
                                start_addr_6502 + total_6502_bytes,
                                0xe9,
                                0);
}

static void
jit_callback(struct jit_struct* p_jit, unsigned char* p_jit_addr) {
  struct util_buffer* p_buf;
  size_t block_addr_6502;
  unsigned char* p_jit_buf;

  /* Calculate the 6502 start address of the basic block we're in. */
  assert(p_jit_addr >= p_jit->p_jit_base);
  block_addr_6502 = p_jit_addr - p_jit->p_jit_base;
  block_addr_6502 >>= k_jit_bytes_shift;
  assert(block_addr_6502 <= 0xffff);

  /* Executing within the zero page and stack page is trapped.
   * By default, for performance, writes to these pages do not invalidate
   * related JIT code.
   * Upon hitting this trap, we could re-JIT everything in a new mode that does
   * invalidate JIT upon writes to these addresses. This is unimplemented.
   */
  assert(block_addr_6502 >= 0x200);

  p_jit_buf = p_jit->p_jit_base + (block_addr_6502 << k_jit_bytes_shift);

  /* For now, our expectations are that we should only hit JIT at the start of
   * a basic block.
   */
  /* -4 because of the jmp [r15 + offset] opcode size. */
  assert(p_jit_buf == (p_jit_addr - 4));

  p_buf = util_buffer_create();
  util_buffer_setup(p_buf, p_jit_buf, k_jit_bytes_per_byte);
  util_buffer_set_base_address(p_buf, p_jit_buf);

  jit_at_addr(p_jit, p_buf, block_addr_6502);

  util_buffer_destroy(p_buf);

  p_jit->reg_pc = block_addr_6502;
}

void
jit_enter(struct jit_struct* p_jit) {
  unsigned char* p_mem = p_jit->p_mem;

  /* The memory must be aligned to at least 0x100 so that our register access
   * tricks work.
   */
  assert(((size_t) p_mem & 0xff) == 0);

  asm volatile (
    /* al is 6502 A. */
    /* We also use ah for lahf / sahf. */
    "xor %%eax, %%eax;"
    /* bl is 6502 X */
    /* rbx is a real x64 pointer to the 6502 RAM. */
    "mov %0, %%rbx;"
    /* cl is 6502 Y. */
    /* rcx is a real x64 pointer to the 6502 RAM. */
    "mov %0, %%rcx;"
    /* rdx, rdi are scratch. */
    "xor %%edx, %%edx;"
    "xor %%edi, %%edi;"
    /* r12 is overflow flag. */
    "xor %%r12, %%r12;"
    /* r13 is the rest of the 6502 flags or'ed together. */
    /* Bit 2 is interrupt disable. */
    /* Bit 3 is decimal mode. */
    "xor %%r13, %%r13;"
    /* r14 is carry flag. */
    "xor %%r14, %%r14;"
    /* sil is 6502 S. */
    /* rsi is a real x64 pointer to the 6502 RAM. */
    "lea 0x100(%%rbx), %%rsi;"
    /* x64 flags is used for zero and negative flags. */
    /* Clear them. ah is already 0 here from above. */
    "sahf;"
    /* Pass a pointer to the jit_struct in r15. */
    "mov %1, %%r15;"
    /* Call regs_util -- offset must match struct jit_struct layout. */
    "call *32(%%r15);"
    /* Constants here must match. */
    "mov $8, %%edi;"
    "shlx %%edi, %%edx, %%edx;"
    "lea 0x20000000(%%edx), %%edx;"
    "call *%%rdx;"
    :
    : "g" (p_mem), "g" (p_jit)
    : "rax", "rbx", "rcx", "rdx", "rdi", "rsi",
      "r9", "r12", "r13", "r14", "r15"
  );
}

struct jit_struct*
jit_create(unsigned char* p_mem,
           void* p_debug_callback,
           struct debug_struct* p_debug,
           struct bbc_struct* p_bbc,
           void* p_read_callback,
           void* p_write_callback) {
  unsigned char* p_jit_base;
  unsigned char* p_utils_base;
  unsigned char* p_util_debug;
  unsigned char* p_util_regs;
  unsigned char* p_util_jit;
  struct jit_struct* p_jit = malloc(sizeof(struct jit_struct));
  if (p_jit == NULL) {
    errx(1, "cannot allocate jit_struct");
  }
  memset(p_jit, '\0', sizeof(struct jit_struct));

  p_jit_base = util_get_guarded_mapping(
      k_jit_addr,
      k_addr_space_size * k_jit_bytes_per_byte,
      1);

  p_utils_base = util_get_guarded_mapping(k_utils_addr, k_utils_size, 1);
  p_util_debug = p_utils_base + k_utils_debug_offset;
  p_util_regs = p_utils_base + k_utils_regs_offset;
  p_util_jit = p_utils_base + k_utils_jit_offset;

  p_jit->p_mem = p_mem;
  p_jit->p_jit_base = p_jit_base;
  p_jit->p_utils_base = p_utils_base;
  p_jit->p_util_debug = p_util_debug;
  p_jit->p_util_regs = p_util_regs;
  p_jit->p_util_jit = p_util_jit;
  p_jit->p_debug = p_debug;
  p_jit->p_debug_callback = p_debug_callback;
  p_jit->p_jit_callback = jit_callback;
  p_jit->p_bbc = p_bbc;
  p_jit->p_read_callback = p_read_callback;
  p_jit->p_write_callback = p_write_callback;
  p_jit->jit_flags = k_jit_flag_merge_ops | k_jit_flag_self_modifying;

  /* int3 */
  memset(p_jit_base, '\xcc', k_addr_space_size * k_jit_bytes_per_byte);
  size_t num_bytes = 0;
  while (num_bytes < k_addr_space_size) {
    (void) jit_emit_do_jit(p_jit_base, 0);

    p_jit->jit_ptrs[num_bytes] = (unsigned int) (size_t) p_jit_base;

    p_jit_base += k_jit_bytes_per_byte;
    num_bytes++;
  }

  jit_emit_debug_util(p_util_debug);
  jit_emit_regs_util(p_jit, p_util_regs);
  jit_emit_jit_util(p_jit, p_util_jit);

  return p_jit;
}

void
jit_set_debug(struct jit_struct* p_jit, int debug) {
  p_jit->jit_flags &= ~k_jit_flag_debug;
  if (debug) {
    p_jit->jit_flags |= k_jit_flag_debug;
  }
}

void
jit_get_registers(struct jit_struct* p_jit,
                  unsigned char* a,
                  unsigned char* x,
                  unsigned char* y,
                  unsigned char* s,
                  unsigned char* flags,
                  uint16_t* pc) {
  *a = p_jit->reg_a;
  *x = p_jit->reg_x;
  *y = p_jit->reg_y;
  *s = p_jit->reg_s;
  *flags = p_jit->reg_flags;
  *pc = p_jit->reg_pc;
}

void
jit_set_registers(struct jit_struct* p_jit,
                  unsigned char a,
                  unsigned char x,
                  unsigned char y,
                  unsigned char s,
                  unsigned char flags,
                  uint16_t pc) {
  p_jit->reg_a = a;
  p_jit->reg_x = x;
  p_jit->reg_y = y;
  p_jit->reg_s = s;
  p_jit->reg_flags = flags;
  p_jit->reg_pc = pc;
}

void
jit_destroy(struct jit_struct* p_jit) {
  util_free_guarded_mapping(p_jit->p_jit_base,
                            k_addr_space_size * k_jit_bytes_per_byte);
  util_free_guarded_mapping(p_jit->p_utils_base, k_utils_size);
  free(p_jit);
}
