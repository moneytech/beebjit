#ifndef BEEBJIT_ASM_X64_JIT_H
#define BEEBJIT_ASM_X64_JIT_H

#include <stdint.h>

struct util_buffer;

void asm_x64_emit_jit_call_compile_trampoline(struct util_buffer* p_buf);
void asm_x64_emit_jit_check_countdown(struct util_buffer* p_buf,
                                      uint16_t addr,
                                      uint32_t count);
void asm_x64_emit_jit_call_debug(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_call_interp(struct util_buffer* p_buf, uint16_t addr);

void asm_x64_emit_jit_ADD_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_ADD_Y_SCRATCH(struct util_buffer* p_buf);
void asm_x64_emit_jit_CHECK_BCD(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_FLAGA(struct util_buffer* p_buf);
void asm_x64_emit_jit_FLAGX(struct util_buffer* p_buf);
void asm_x64_emit_jit_FLAGY(struct util_buffer* p_buf);
void asm_x64_emit_jit_INC_SCRATCH(struct util_buffer* p_buf);
void asm_x64_emit_jit_JMP_SCRATCH(struct util_buffer* p_buf);
void asm_x64_emit_jit_LOAD_CARRY(struct util_buffer* p_buf);
void asm_x64_emit_jit_LOAD_CARRY_INV(struct util_buffer* p_buf);
void asm_x64_emit_jit_LOAD_OVERFLOW(struct util_buffer* p_buf);
void asm_x64_emit_jit_MODE_ABX(struct util_buffer* p_buf, uint16_t value);
void asm_x64_emit_jit_MODE_ABY(struct util_buffer* p_buf, uint16_t value);
void asm_x64_emit_jit_MODE_IND(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_MODE_IND_SCRATCH(struct util_buffer* p_buf);
void asm_x64_emit_jit_MODE_ZPX(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_MODE_ZPY(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_PUSH_16(struct util_buffer* p_buf, uint16_t value);
void asm_x64_emit_jit_SAVE_CARRY(struct util_buffer* p_buf);
void asm_x64_emit_jit_SAVE_CARRY_INV(struct util_buffer* p_buf);
void asm_x64_emit_jit_SAVE_OVERFLOW(struct util_buffer* p_buf);
void asm_x64_emit_jit_STOA_IMM(struct util_buffer* p_buf,
                               uint16_t addr,
                               uint8_t value);
void asm_x64_emit_jit_SUB_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_WRITE_INV_ABS(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_WRITE_INV_SCRATCH(struct util_buffer* p_buf);

void asm_x64_emit_jit_ADC_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_ALR_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_AND_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_ASL_ACC(struct util_buffer* p_buf);
void asm_x64_emit_jit_BCC(struct util_buffer* p_buf, void* p_target);
void asm_x64_emit_jit_BCS(struct util_buffer* p_buf, void* p_target);
void asm_x64_emit_jit_BEQ(struct util_buffer* p_buf, void* p_target);
void asm_x64_emit_jit_BIT(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_BMI(struct util_buffer* p_buf, void* p_target);
void asm_x64_emit_jit_BNE(struct util_buffer* p_buf, void* p_target);
void asm_x64_emit_jit_BPL(struct util_buffer* p_buf, void* p_target);
void asm_x64_emit_jit_BVC(struct util_buffer* p_buf, void* p_target);
void asm_x64_emit_jit_BVS(struct util_buffer* p_buf, void* p_target);
void asm_x64_emit_jit_CMP_ABS(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_CMP_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_CPX_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_CPY_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_DEC_ABS(struct util_buffer* p_buf, uint16_t value);
void asm_x64_emit_jit_EOR_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_INC_ABS(struct util_buffer* p_buf, uint16_t value);
void asm_x64_emit_jit_INC_ZPG(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_JMP(struct util_buffer* p_buf, void* p_target);
void asm_x64_emit_jit_LDA_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_LDA_ABS(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_LDA_ABX(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_LDA_scratch(struct util_buffer* p_buf);
void asm_x64_emit_jit_LDX_ABS(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_LDX_ABY(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_LDX_scratch(struct util_buffer* p_buf);
void asm_x64_emit_jit_LDX_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_LDY_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_LSR_ACC(struct util_buffer* p_buf);
void asm_x64_emit_jit_ORA_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_ROL_ACC(struct util_buffer* p_buf);
void asm_x64_emit_jit_ROR_ABS(struct util_buffer* p_buf, uint16_t value);
void asm_x64_emit_jit_ROR_ACC(struct util_buffer* p_buf);
void asm_x64_emit_jit_SAX_ABS(struct util_buffer* p_buf, uint16_t value);
void asm_x64_emit_jit_SBC_IMM(struct util_buffer* p_buf, uint8_t value);
void asm_x64_emit_jit_SHY_ABX(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_SLO_ABS(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_STA_ABS(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_STA_ABX(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_STA_ABY(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_STA_scratch(struct util_buffer* p_buf);
void asm_x64_emit_jit_STX_ABS(struct util_buffer* p_buf, uint16_t addr);
void asm_x64_emit_jit_STY_ABS(struct util_buffer* p_buf, uint16_t addr);

/* Symbols pointing directly to ASM bytes. */
void asm_x64_jit_compile_trampoline();
void asm_x64_jit_call_interp();
void asm_x64_jit_countdown_expired();

void asm_x64_jit_call_compile_trampoline();
void asm_x64_jit_call_compile_trampoline_END();
void asm_x64_jit_check_countdown();
void asm_x64_jit_check_countdown_pc_patch();
void asm_x64_jit_check_countdown_count_patch();
void asm_x64_jit_check_countdown_jump_patch();
void asm_x64_jit_check_countdown_END();
void asm_x64_jit_set_pc_and_call();
void asm_x64_jit_set_pc_and_call_pc_patch();
void asm_x64_jit_set_pc_and_call_call_patch();
void asm_x64_jit_set_pc_and_call_END();

void asm_x64_jit_ADD_IMM();
void asm_x64_jit_ADD_IMM_END();
void asm_x64_jit_ADD_Y_SCRATCH();
void asm_x64_jit_ADD_Y_SCRATCH_END();
void asm_x64_jit_CHECK_BCD();
void asm_x64_jit_CHECK_BCD_pc_patch();
void asm_x64_jit_CHECK_BCD_jump_patch();
void asm_x64_jit_CHECK_BCD_END();
void asm_x64_jit_FLAGA();
void asm_x64_jit_FLAGA_END();
void asm_x64_jit_FLAGX();
void asm_x64_jit_FLAGX_END();
void asm_x64_jit_FLAGY();
void asm_x64_jit_FLAGY_END();
void asm_x64_jit_INC_SCRATCH();
void asm_x64_jit_INC_SCRATCH_END();
void asm_x64_jit_JMP_SCRATCH();
void asm_x64_jit_JMP_SCRATCH_END();
void asm_x64_jit_LOAD_CARRY();
void asm_x64_jit_LOAD_CARRY_END();
void asm_x64_jit_LOAD_CARRY_INV();
void asm_x64_jit_LOAD_CARRY_INV_END();
void asm_x64_jit_LOAD_OVERFLOW();
void asm_x64_jit_LOAD_OVERFLOW_END();
void asm_x64_jit_MODE_ABX();
void asm_x64_jit_MODE_ABX_lea_patch();
void asm_x64_jit_MODE_ABX_END();
void asm_x64_jit_MODE_ABY();
void asm_x64_jit_MODE_ABY_lea_patch();
void asm_x64_jit_MODE_ABY_END();
void asm_x64_jit_MODE_IND();
void asm_x64_jit_MODE_IND_END();
void asm_x64_jit_MODE_IND_SCRATCH();
void asm_x64_jit_MODE_IND_SCRATCH_jump_patch();
void asm_x64_jit_MODE_IND_SCRATCH_END();
void asm_x64_jit_MODE_ZPX();
void asm_x64_jit_MODE_ZPX_lea_patch();
void asm_x64_jit_MODE_ZPX_END();
void asm_x64_jit_MODE_ZPX_8bit();
void asm_x64_jit_MODE_ZPX_8bit_lea_patch();
void asm_x64_jit_MODE_ZPX_8bit_END();
void asm_x64_jit_MODE_ZPY();
void asm_x64_jit_MODE_ZPY_lea_patch();
void asm_x64_jit_MODE_ZPY_END();
void asm_x64_jit_MODE_ZPY_8bit();
void asm_x64_jit_MODE_ZPY_8bit_lea_patch();
void asm_x64_jit_MODE_ZPY_8bit_END();
void asm_x64_jit_LOAD_OVERFLOW_END();
void asm_x64_jit_PUSH_16();
void asm_x64_jit_PUSH_16_byte1_patch();
void asm_x64_jit_PUSH_16_byte2_patch();
void asm_x64_jit_PUSH_16_END();
void asm_x64_jit_SAVE_CARRY();
void asm_x64_jit_SAVE_CARRY_END();
void asm_x64_jit_SAVE_CARRY_INV();
void asm_x64_jit_SAVE_CARRY_INV_END();
void asm_x64_jit_SAVE_OVERFLOW();
void asm_x64_jit_SAVE_OVERFLOW_END();
void asm_x64_jit_STOA_IMM();
void asm_x64_jit_STOA_IMM_END();
void asm_x64_jit_SUB_IMM();
void asm_x64_jit_SUB_IMM_END();
void asm_x64_jit_WRITE_INV_ABS();
void asm_x64_jit_WRITE_INV_ABS_offset_patch();
void asm_x64_jit_WRITE_INV_ABS_END();
void asm_x64_jit_WRITE_INV_SCRATCH();
void asm_x64_jit_WRITE_INV_SCRATCH_END();

void asm_x64_jit_ADC_IMM();
void asm_x64_jit_ADC_IMM_END();
void asm_x64_jit_ALR_IMM();
void asm_x64_jit_ALR_IMM_patch_byte();
void asm_x64_jit_ALR_IMM_END();
void asm_x64_jit_AND_IMM();
void asm_x64_jit_AND_IMM_END();
void asm_x64_jit_ASL_ACC();
void asm_x64_jit_ASL_ACC_END();
void asm_x64_jit_BCC();
void asm_x64_jit_BCC_END();
void asm_x64_jit_BCC_8bit();
void asm_x64_jit_BCC_8bit_END();
void asm_x64_jit_BCS();
void asm_x64_jit_BCS_END();
void asm_x64_jit_BCS_8bit();
void asm_x64_jit_BCS_8bit_END();
void asm_x64_jit_BEQ();
void asm_x64_jit_BEQ_END();
void asm_x64_jit_BEQ_8bit();
void asm_x64_jit_BEQ_8bit_END();
void asm_x64_jit_BIT();
void asm_x64_jit_BIT_mov_patch();
void asm_x64_jit_BIT_END();
void asm_x64_jit_BMI();
void asm_x64_jit_BMI_END();
void asm_x64_jit_BMI_8bit();
void asm_x64_jit_BMI_8bit_END();
void asm_x64_jit_BNE();
void asm_x64_jit_BNE_END();
void asm_x64_jit_BNE_8bit();
void asm_x64_jit_BNE_8bit_END();
void asm_x64_jit_BPL();
void asm_x64_jit_BPL_END();
void asm_x64_jit_BPL_8bit();
void asm_x64_jit_BPL_8bit_END();
void asm_x64_jit_BVC();
void asm_x64_jit_BVC_END();
void asm_x64_jit_BVC_8bit();
void asm_x64_jit_BVC_8bit_END();
void asm_x64_jit_BVS();
void asm_x64_jit_BVS_END();
void asm_x64_jit_BVS_8bit();
void asm_x64_jit_BVS_8bit_END();
void asm_x64_jit_CMP_ABS();
void asm_x64_jit_CMP_ABS_END();
void asm_x64_jit_CMP_IMM();
void asm_x64_jit_CMP_IMM_END();
void asm_x64_jit_CPX_IMM();
void asm_x64_jit_CPX_IMM_END();
void asm_x64_jit_CPY_IMM();
void asm_x64_jit_CPY_IMM_END();
void asm_x64_jit_DEC_ABS();
void asm_x64_jit_DEC_ABS_mov1_patch();
void asm_x64_jit_DEC_ABS_mov2_patch();
void asm_x64_jit_DEC_ABS_END();
void asm_x64_jit_EOR_IMM();
void asm_x64_jit_EOR_IMM_END();
void asm_x64_jit_INC_ABS();
void asm_x64_jit_INC_ABS_mov1_patch();
void asm_x64_jit_INC_ABS_mov2_patch();
void asm_x64_jit_INC_ABS_END();
void asm_x64_jit_INC_ZPG();
void asm_x64_jit_INC_ZPG_END();
void asm_x64_jit_JMP();
void asm_x64_jit_JMP_END();
void asm_x64_jit_JMP_8bit();
void asm_x64_jit_JMP_8bit_END();
void asm_x64_jit_LDA_IMM();
void asm_x64_jit_LDA_IMM_END();
void asm_x64_jit_LDA_ABS();
void asm_x64_jit_LDA_ABS_END();
void asm_x64_jit_LDA_ABX();
void asm_x64_jit_LDA_ABX_END();
void asm_x64_jit_LDA_scratch();
void asm_x64_jit_LDA_scratch_END();
void asm_x64_jit_LDX_ABS();
void asm_x64_jit_LDX_ABS_END();
void asm_x64_jit_LDX_ABY();
void asm_x64_jit_LDX_ABY_END();
void asm_x64_jit_LDX_IMM();
void asm_x64_jit_LDX_IMM_END();
void asm_x64_jit_LDX_scratch();
void asm_x64_jit_LDX_scratch_END();
void asm_x64_jit_LDY_IMM();
void asm_x64_jit_LDY_IMM_END();
void asm_x64_jit_LSR_ACC();
void asm_x64_jit_LSR_ACC_END();
void asm_x64_jit_ORA_IMM();
void asm_x64_jit_ORA_IMM_END();
void asm_x64_jit_ROL_ACC();
void asm_x64_jit_ROL_ACC_END();
void asm_x64_jit_ROR_ABS();
void asm_x64_jit_ROR_ABS_mov1_patch();
void asm_x64_jit_ROR_ABS_mov2_patch();
void asm_x64_jit_ROR_ABS_END();
void asm_x64_jit_ROR_ACC();
void asm_x64_jit_ROR_ACC_END();
void asm_x64_jit_SAX_ABS();
void asm_x64_jit_SAX_ABS_END();
void asm_x64_jit_SBC_IMM();
void asm_x64_jit_SBC_IMM_END();
void asm_x64_jit_SHY_ABX();
void asm_x64_jit_SHY_ABX_byte_patch();
void asm_x64_jit_SHY_ABX_mov_patch();
void asm_x64_jit_SHY_ABX_END();
void asm_x64_jit_SLO_ABS();
void asm_x64_jit_SLO_ABS_mov1_patch();
void asm_x64_jit_SLO_ABS_mov2_patch();
void asm_x64_jit_SLO_ABS_END();
void asm_x64_jit_STA_ABS();
void asm_x64_jit_STA_ABS_END();
void asm_x64_jit_STA_ABX();
void asm_x64_jit_STA_ABX_END();
void asm_x64_jit_STA_ABY();
void asm_x64_jit_STA_ABY_END();
void asm_x64_jit_STA_scratch();
void asm_x64_jit_STA_scratch_END();
void asm_x64_jit_STX_ABS();
void asm_x64_jit_STX_ABS_END();
void asm_x64_jit_STY_ABS();
void asm_x64_jit_STY_ABS_END();

#endif /* BEEBJIT_ASM_X64_JIT_H */
