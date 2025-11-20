#include <cstdint>
#include <vector>
#include <assert.h>

/**
 * ARM64 JIT Backend - Low-level instruction generation
 * 
 * This backend provides pure instruction encoding without any state management.
 * All functions are prefixed with gen_* and return the encoded instruction.
 */
class ARM64Backend {
public:
    // ===== Condition Codes =====
    enum class ConditionCode  {
        COND_EQ = 0x0,  // Equal
        COND_NE = 0x1,  // Not equal
        COND_LT = 0xB,  // Signed less than
        COND_GT = 0xC,  // Signed greater than
    };

    // ===== Move Instructions =====
    
    /**
     * MOVZ Xd, #imm16, LSL #shift
     * Move wide with zero, 64-bit
     */
    static uint32_t gen_movz_x(int reg, uint16_t imm16, int shift) {
        return (0b110 << 29) | (0b100101 << 23) | ((shift / 16) << 21) |
               ((uint32_t)imm16 << 5) | (reg & 0x1F);
    }
    
    /**
     * MOVK Xd, #imm16, LSL #shift
     * Move wide with keep, 64-bit
     */
    static uint32_t gen_movk_x(int reg, uint16_t imm16, int shift) {
        return (0b111 << 29) | (0b100101 << 23) | ((shift / 16) << 21) |
               ((uint32_t)imm16 << 5) | (reg & 0x1F);
    }
    
    /**
     * MOVZ Wd, #imm16, LSL #shift
     * Move wide with zero, 32-bit
     */
    static uint32_t gen_movz_w(int reg, uint16_t imm16, int shift) {
        return (0b010 << 29) | (0b100101 << 23) | ((shift / 16) << 21) |
               ((uint32_t)imm16 << 5) | (reg & 0x1F);
    }
    
    /**
     * MOVK Wd, #imm16, LSL #shift
     * Move wide with keep, 32-bit
     */
    static uint32_t gen_movk_w(int reg, uint16_t imm16, int shift) {
        return (0b011 << 29) | (0b100101 << 23) | ((shift / 16) << 21) |
               ((uint32_t)imm16 << 5) | (reg & 0x1F);
    }
    
    /**
     * MOV Xd, Xn (implemented as ORR Xd, XZR, Xn)
     * Move register, 64-bit
     */
    //emit(ARM64Backend::gen_mov_x(29, 31));             // mov x29, sp
    static uint32_t gen_mov_x(int rd, int rn) {
        return (0b1 << 31) | (0b0101010 << 24) | ((rn & 0x1F) << 16) |
               (31 << 5) | (rd & 0x1F);
    }
    
    // ===== Load/Store Instructions =====
    
    /**
     * LDR Xt, [Xn, #offset]
     * Load register, 64-bit, immediate offset
     * offset is in 8-byte units (0-32760)
     */
    static uint32_t gen_ldr_x_imm(int rt, int rn, int imm12) {
        return 0xF9400000 |
               ((imm12 & 0xFFF) << 10) |
               ((rn & 0x1F) << 5) |
               (rt & 0x1F);
    }

    /**
     * LDR Xt, [Xn, Xm]
     * Load register, 64-bit, register offset
     */
    static uint32_t gen_reg_mem(int rt, int rn, int rm, bool load = true, int size = 64) {
        // https://github.com/qemu/qemu/blob/master/tcg/aarch64/tcg-target.c.inc#L1199
        // zero extended offset addr, 32 bits
        enum {
            MO_8 = 0,
            MO_16 = 1,
            MO_32 = 2,
            MO_64 = 3,
            LDST_ST = 0,
            LDST_LD = 1,
            TCG_TYPE_I64 = 1,
            I3312_LDRB = 0x38000000 | LDST_LD << 22 | MO_8 << 30,
            I3312_LDRH = 0x38000000 | LDST_LD << 22 | MO_16 << 30,
            I3312_LDRW = 0x38000000 | LDST_LD << 22 | MO_32 << 30,
            I3312_LDRX = 0x38000000 | LDST_LD << 22 | MO_64 << 30,
            I3312_STRB = 0x38000000 | LDST_ST << 22 | MO_8 << 30,
            I3312_STRH = 0x38000000 | LDST_ST << 22 | MO_16 << 30,
            I3312_STRW = 0x38000000 | LDST_ST << 22 | MO_32 << 30,
            I3312_STRX = 0x38000000 | LDST_ST << 22 | MO_64 << 30,
            I3312_TO_I3310 = 0x00200800,
        };
        
        uint32_t x = I3312_TO_I3310 | 0x4000;
        switch (size)
        {
            case 64: x |= load ? I3312_LDRX : I3312_STRX; break;
            case 32: x |= load ? I3312_LDRW : I3312_STRW; break;
            case 16: x |= load ? I3312_LDRH : I3312_STRH; break;
            case 8: x |= load ? I3312_LDRB : I3312_STRB; break;
            default:
                assert(0);
        }

        x |= ((rm & 0x1F) << 16);  // Rm = offset register
        x |= ((rn & 0x1F) << 5);   // Rn = base register
        x |= (rt & 0x1F);         // Rt = destination

        //printf("gen_ldr_x_reg rt=%d rn=%d rm=%d size=%d => 0x%x\n", rt, rn, rm, size, x);
        return x;
    }

    /**
     * STR Xt, [Xn, #offset]
     * Store register, 64-bit, immediate offset
     */
    static uint32_t gen_str_x_imm(int rt, int rn, int imm12) {
        return 0xF9000000 |               // STR X*, unsigned immediate
               ((imm12 & 0xFFF) << 10) |
               ((rn & 0x1F) << 5)       |
               (rt & 0x1F);
    }
    
    /**
     * STP Xt1, Xt2, [Xn, #offset]
     * Store pair of registers, 64-bit
     * offset is in 8-byte units (-512 to 504)
     */
    static uint32_t gen_stp_x(int rt1, int rt2, int rn, int offset) {
        int imm7 = (offset / 8) & 0x7F;
        return (0b10 << 30) | (0b101 << 27) | (0b0 << 26) | (0b010 << 23) |
               (imm7 << 15) | ((rt2 & 0x1F) << 10) | ((rn & 0x1F) << 5) | (rt1 & 0x1F);
    }
    
    /**
     * LDP Xt1, Xt2, [Xn, #offset]
     * Load pair of registers, 64-bit, signed offset (no writeback)
     * offset is in 8-byte units (-512 to 504)
     */
    // LDP Xn, Xm, [Xr, #offset]   (signed offset)
    // offset must be a multiple of 8
    static uint32_t gen_ldp_x(int rt1, int rt2, int rn, int offset) {
        int imm7 = (offset / 8) & 0x7F;

        return (0b10 << 30)        | // size = 64-bit
               (0b101 << 27)       | // bits 29..27 = 101
               (0 << 26)           | // signed offset
               (0b010 << 23)       | // op2 = 010  <<< FIXED
               (1 << 22)           | // L = 1 (load)
               (imm7 << 15)        |
               ((rt2 & 0x1F) << 10)|
               ((rn  & 0x1F) << 5) |
               (rt1 & 0x1F);
    }
    
    // ===== Arithmetic Instructions =====
    
    /**
     * ADD Xd, Xn, Xm
     * Add (register), 64-bit
     */
    static uint32_t gen_add_x_reg(int rd, int rn, int rm) {
        return (0b1 << 31) | (0b0001011 << 24) | (0b00 << 22) |
               ((rm & 0x1F) << 16) | ((rn & 0x1F) << 5) | (rd & 0x1F);
    }
    
    /**
     * ADD Xd, Xn, #imm12
     * Add (immediate), 64-bit
     */
    static uint32_t gen_add_x_imm(int rd, int rn, uint16_t imm12) {
        return (0b1 << 31) | (0b0 << 30) | (0b10001 << 24) |
               ((imm12 & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F);
    }
    
    /**
     * SUB Xd, Xn, Xm
     * Subtract (register), 64-bit
     */
    static uint32_t gen_sub_x_reg(int rd, int rn, int rm) {
        return (0b1 << 31) | (0b1001011 << 24) | (0b00 << 22) |
               ((rm & 0x1F) << 16) | ((rn & 0x1F) << 5) | (rd & 0x1F);
    }
    
    /**
     * SUB Xd, Xn, #imm12
     * Subtract (immediate), 64-bit
     */
    static uint32_t gen_sub_x_imm(int rd, int rn, uint16_t imm12) {
        return (0b1 << 31) | (0b1 << 30) | (0b10001 << 24) |
               ((imm12 & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F);
    }
    
    /**
     * SDIV Xd, Xn, Xm
     * Signed divide, 64-bit
     */
    static uint32_t gen_sdiv_x(int rd, int rn, int rm) {
        return (0b1 << 31) | (0b0011010110 << 21) | ((rm & 0x1F) << 16) |
               (0b000011 << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F);
    }
    
    /**
     * UDIV Xd, Xn, Xm
     * Unsigned divide, 64-bit
     */
    static uint32_t gen_udiv_x(int rd, int rn, int rm) {
        return (0b1 << 31) | (0b0011010110 << 21) | ((rm & 0x1F) << 16) |
               (0b000010 << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F);
    }
    
    /**
     * MUL Xd, Xn, Xm
     * Multiply, 64-bit
     * Xd = Xn * Xm
     * (Implemented as MADD Xd, Xn, Xm, XZR)
     */
    static uint32_t gen_mul_x(int rd, int rn, int rm) {
        return (0b1 << 31) | (0b0011011 << 24) | (0b000 << 21) |
               ((rm & 0x1F) << 16) | (0b0 << 15) | (31 << 10) |
               ((rn & 0x1F) << 5) | (rd & 0x1F);
    }
    
    /**
     * MSUB Xd, Xn, Xm, Xa
     * Multiply-subtract, 64-bit
     * Xd = Xa - (Xn * Xm)
     */
    static uint32_t gen_msub_x(int rd, int rn, int rm, int ra) {
        return (0b1 << 31) | (0b0011011 << 24) | (0b000 << 21) |
               ((rm & 0x1F) << 16) | (0b1 << 15) | ((ra & 0x1F) << 10) |
               ((rn & 0x1F) << 5) | (rd & 0x1F);
    }
    
    // ===== Compare and Condition Instructions =====
    
    /**
     * CMP Xn, Xm (implemented as SUBS XZR, Xn, Xm)
     * Compare, 64-bit
     */
    static uint32_t gen_cmp_x(int rn, int rm) {
        return (0b1 << 31) | (0b1101011 << 24) | (0b00 << 22) |
               ((rm & 0x1F) << 16) | ((rn & 0x1F) << 5) | 31;
    }
    
    /**
     * CSET Xd, cond (implemented as CSINC Xd, XZR, XZR, invert(cond))
     * Conditional set, 64-bit
     * Sets Xd to 1 if condition is true, 0 otherwise
     */
    static uint32_t gen_cset_x(int rd, ConditionCode cond) {
        int inv_cond = (static_cast<int>(cond) ^ 1);
        return (0b1 << 31) |           // sf = 1 (64-bit)
               (0b0 << 30) |           // op = 0
               (0b0 << 29) |           // S = 0
               (0b11010100 << 21) |    // opcode for CSINC
               (31 << 16) |            // Rm = XZR
               ((inv_cond & 0xF) << 12) | // inverted condition
               (0b01 << 10) |          // op2 = 01 for CSINC
               (31 << 5) |             // Rn = XZR
               (rd & 0x1F);            // Rd
    }
    
    // ===== Branch Instructions =====
    
    /**
     * B.cond offset
     * Conditional branch
     * offset is in instructions (signed 19-bit, ±1MB range)
     */
    static uint32_t gen_bcond(ConditionCode cond, int32_t offset) {
        int32_t imm19 = offset & 0x7FFFF;
        return (0b01010100 << 24) | (imm19 << 5) | (static_cast<int>(cond) & 0xF);
    }
    
    /**
     * B offset
     * Unconditional branch
     * offset is in instructions (signed 26-bit, ±128MB range)
     */
    static uint32_t gen_b(int32_t offset) {
        int32_t imm26 = offset & 0x3FFFFFF;
        return (0b000101 << 26) | imm26;
    }
    
    /**
     * BL offset
     * Branch with link (call)
     * offset is in instructions (signed 26-bit)
     */
    static uint32_t gen_bl(int32_t offset) {
        int32_t imm26 = offset & 0x3FFFFFF;
        return (0b100101 << 26) | imm26;
    }
    
    /**
     * BR Xn
     * Branch to register
     */
    static uint32_t gen_br(int rn) {
        return (0b1101011 << 25) | (0b0000 << 21) | (0b11111 << 16) |
               ((rn & 0x1F) << 5);
    }
    
    /**
     * ADR Xd, offset
     * Form PC-relative address
     * offset is in bytes (signed 21-bit, ±1MB range)
     */
    static uint32_t gen_adr(int rd, int32_t offset) {
        uint32_t immlo = offset & 0x3;
        uint32_t immhi = (offset >> 2) & 0x7FFFF;
        return (0b0 << 31) | (immlo << 29) | (0b10000 << 24) | (immhi << 5) | (rd & 0x1F);
    }
    
    /**
     * LSL Xd, Xn, #shift
     * Logical shift left (immediate), 64-bit
     * Implemented as UBFM Xd, Xn, #(-shift MOD 64), #(63-shift)
     */
    static uint32_t gen_lsl_x_imm(int rd, int rn, int shift) {
        int immr = (64 - shift) & 0x3F;
        int imms = (63 - shift) & 0x3F;
        return (0b1 << 31) | (0b10 << 29) | (0b100110 << 23) | (0b1 << 22) |
               (immr << 16) | (imms << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F);
    }
    
    /**
     * BLR Xn
     * Branch with link to register (call register)
     */
    static uint32_t gen_blr(int rn) {
        return (0b1101011 << 25) | (0b0001 << 21) | (0b11111 << 16) |
               ((rn & 0x1F) << 5);
    }
    
    /**
     * RET (implied Xn = X30)
     * Return from subroutine
     */
    static uint32_t gen_ret() {
        return 0xD65F03C0;
    }
    
    static uint32_t gen_nop() {
        return 0xD503201F;
    }

    static uint32_t gen_prologue1() {
        return 0xA9BF7BF0; // stp x29, x30, [sp, #-16]!
    }
    
    static uint32_t gen_prologue2() {
        return 0x910003FD; // mov x29, sp
    }
    
    static uint32_t gen_epilogue() {
        return 0xA8C17BF0; // ldp x29, x30, [sp], #16
    }
};
