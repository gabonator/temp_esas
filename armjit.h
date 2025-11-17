#ifndef ARM64_JIT_H
#define ARM64_JIT_H

#include <cstdint>
#include <cstring>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
#include <libkern/OSCacheControl.h>

/**
 * ARM64 JIT Compiler for Apple M1/M2
 *
 * Register Usage:
 * - x0: Buffer base pointer (preserved across operations)
 * - x19-x28: Variables 0-9 (callee-saved)
 * - Stack: Variables 10-31
 * - x1-x18: Temporary registers
 */
class ARM64JIT {
private:
    std::vector<uint32_t> code;
    void* executable_memory;
    size_t executable_size;
    
    static constexpr int NUM_REG_VARS = 10;  // w19-w28
    static constexpr int NUM_TOTAL_VARS = 32;
    
    // Get register number for variable (19-28 for vars 0-9, -1 for stack vars)
    int get_var_reg(int var_index) const {
        if (var_index >= 0 && var_index < NUM_REG_VARS) {
            return 19 + var_index;
        }
        return -1;
    }
    
    // Get stack offset for variable in bytes (relative to SP after prologue)
    int get_var_stack_offset(int var_index) const {
        if (var_index < NUM_REG_VARS) return -1;
        return (var_index - NUM_REG_VARS) * 4;
    }
    
    void emit(uint32_t instruction) {
        code.push_back(instruction);
    }
    
    // ===== Instruction Encoders =====
    
    // MOVZ Xd, #imm16, LSL #shift
    uint32_t gen_movz_x(int reg, uint16_t imm16, int shift) const {
        return (0b110 << 29) | (0b100101 << 23) | ((shift / 16) << 21) |
               ((uint32_t)imm16 << 5) | (reg & 0x1F);
    }
    
    // MOVK Xd, #imm16, LSL #shift
    uint32_t gen_movk_x(int reg, uint16_t imm16, int shift) const {
        return (0b111 << 29) | (0b100101 << 23) | ((shift / 16) << 21) |
               ((uint32_t)imm16 << 5) | (reg & 0x1F);
    }
    
    // MOVZ Wd, #imm16, LSL #shift
    uint32_t gen_movz_w(int reg, uint16_t imm16, int shift) const {
        return (0b010 << 29) | (0b100101 << 23) | ((shift / 16) << 21) |
               ((uint32_t)imm16 << 5) | (reg & 0x1F);
    }
    
    // MOVK Wd, #imm16, LSL #shift
    uint32_t gen_movk_w(int reg, uint16_t imm16, int shift) const {
        return (0b011 << 29) | (0b100101 << 23) | ((shift / 16) << 21) |
               ((uint32_t)imm16 << 5) | (reg & 0x1F);
    }
    
    // STR Xn, [Xm, #offset]
    uint32_t gen_str_x_imm(int rt, int rn, int imm12) const {
        return (0b11 << 30) | (0b111 << 27) | (0b00 << 22) |
               ((imm12 & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rt & 0x1F);
    }
    
    // STR Wn, [Xm, #offset]
    uint32_t gen_str_w_imm(int rt, int rn, int imm12) const {
        return (0b10 << 30) | (0b111 << 27) | (0b00 << 22) |
               ((imm12 & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rt & 0x1F);
    }
    
    // STR Xn, [Xm, Xr]
    uint32_t gen_str_x_reg(int rt, int rn, int rm) const {
        return (0b11 << 30) | (0b111 << 27) | (0b00 << 22) | (0b1 << 21) |
               ((rm & 0x1F) << 16) | (0b011 << 13) | (0b0 << 12) |
               ((rn & 0x1F) << 5) | (rt & 0x1F);
    }
    
    // LDR Xn, [Xm, #offset]
    uint32_t gen_ldr_x_imm(int rt, int rn, int imm12) const {
        return (0b11 << 30) | (0b111 << 27) | (0b01 << 22) |
               ((imm12 & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rt & 0x1F);
    }
    
    // LDR Wn, [Xm, #offset]
    uint32_t gen_ldr_w_imm(int rt, int rn, int imm12) const {
        return (0b10 << 30) | (0b111 << 27) | (0b01 << 22) |
               ((imm12 & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rt & 0x1F);
    }
    
    // LDR Wn, [Xm, Xr]
    uint32_t gen_ldr_w_reg(int rt, int rn, int rm) const {
        return (0b10 << 30) | (0b111 << 27) | (0b01 << 22) | (0b1 << 21) |
               ((rm & 0x1F) << 16) | (0b011 << 13) | (0b0 << 12) |
               ((rn & 0x1F) << 5) | (rt & 0x1F);
    }
    
    // STR Wn, [Xm, Xr]
    uint32_t gen_str_w_reg(int rt, int rn, int rm) const {
        return (0b10 << 30) | (0b111 << 27) | (0b00 << 22) | (0b1 << 21) |
               ((rm & 0x1F) << 16) | (0b011 << 13) | (0b0 << 12) |
               ((rn & 0x1F) << 5) | (rt & 0x1F);
    }
    // ADD Xd, Xn, Xm
    uint32_t gen_add_x_reg(int rd, int rn, int rm) const {
        return (0b1 << 31) | (0b0001011 << 24) | (0b00 << 22) |
               ((rm & 0x1F) << 16) | ((rn & 0x1F) << 5) | (rd & 0x1F);
    }
    
    // ADD Wd, Wn, Wm
    uint32_t gen_add_w_reg(int rd, int rn, int rm) const {
        return (0b0 << 31) | (0b0001011 << 24) | (0b00 << 22) |
               ((rm & 0x1F) << 16) | ((rn & 0x1F) << 5) | (rd & 0x1F);
    }
    
    // SUB Xd, Xn, #imm12
    uint32_t gen_sub_x_imm(int rd, int rn, uint16_t imm12) const {
        return (0b1 << 31) | (0b1 << 30) | (0b10001 << 24) |
               ((imm12 & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F);
    }
    
    // ADD Xd, Xn, #imm12
    uint32_t gen_add_x_imm(int rd, int rn, uint16_t imm12) const {
        return (0b1 << 31) | (0b0 << 30) | (0b10001 << 24) |
               ((imm12 & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F);
    }
    
    // MOV Xd, Xn (ORR Xd, XZR, Xn)
    uint32_t gen_mov_x(int rd, int rn) const {
        return (0b1 << 31) | (0b0101010 << 24) | ((rn & 0x1F) << 16) |
               (31 << 5) | (rd & 0x1F);
    }
    
    // MOV Wd, Wn (ORR Wd, WZR, Wn)
    uint32_t gen_mov_w(int rd, int rn) const {
        return (0b0 << 31) | (0b0101010 << 24) | ((rn & 0x1F) << 16) |
               (31 << 5) | (rd & 0x1F);
    }
    
    // CMP Wn, Wm (SUBS WZR, Wn, Wm)
    uint32_t gen_cmp_w(int rn, int rm) const {
        return (0b0 << 31) | (0b1101011 << 24) | (0b00 << 22) |
               ((rm & 0x1F) << 16) | ((rn & 0x1F) << 5) | 31;
    }
    
    // CSET Wd, cond (CSINC Wd, WZR, WZR, invert(cond))
    uint32_t gen_cset_w(int rd, int cond) const {
        int inv_cond = cond ^ 1;  // Invert condition
        return (0b0 << 31) | (0b11010100 << 23) | (31 << 16) |
               (inv_cond << 12) | (31 << 5) | (rd & 0x1F);
    }
    
    // CSINC Wd, Wn, Wm, cond (conditional select and increment)
    uint32_t gen_csinc_w(int rd, int rn, int rm, int cond) const {
        return (0b0 << 31) | (0b11010100 << 23) | ((rm & 0x1F) << 16) |
               (cond << 12) | (0b01 << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F);
    }
    
    // B.cond (conditional branch)
    // offset is in instructions (signed 19-bit, ±1MB range)
    uint32_t gen_bcond(int cond, int32_t offset) const {
        int32_t imm19 = offset & 0x7FFFF;
        return (0b01010100 << 24) | (imm19 << 5) | (cond & 0xF);
    }
    
    // B (unconditional branch)
    // offset is in instructions (signed 26-bit)
    uint32_t gen_b(int32_t offset) const {
        int32_t imm26 = offset & 0x3FFFFFF;
        return (0b000101 << 26) | imm26;
    }
    
    // BL (branch with link - call)
    uint32_t gen_bl(int32_t offset) const {
        int32_t imm26 = offset & 0x3FFFFFF;
        return (0b100101 << 26) | imm26;
    }
    
    // BLR Xn
    uint32_t gen_blr(int rn) const {
        return (0b1101011 << 25) | (0b0001 << 21) | (0b11111 << 16) |
               ((rn & 0x1F) << 5);
    }
    
    // RET
    uint32_t gen_ret() const {
        return 0xD65F03C0;
    }
    
    // STP Xn, Xm, [Xr, #offset]
    uint32_t gen_stp_x(int rt1, int rt2, int rn, int offset) const {
        int imm7 = (offset / 8) & 0x7F;
        uint32_t x= (0b10 << 30) | (0b101 << 27) | (0b0 << 26) | (0b010 << 23) |
               (imm7 << 15) | ((rt2 & 0x1F) << 10) | ((rn & 0x1F) << 5) | (rt1 & 0x1F);
        return x;
    }
    
    // LDP Xn, Xm, [Xr, #offset]
    uint32_t gen_ldp_x(int rt1, int rt2, int rn, int offset) const {
        int imm7 = (offset / 8) & 0x7F;
        // P=0, U=1, W=0, L=1 → bits 26..21 = 0b010101
            return (0b10 << 30)         | // size = 64-bit
                   (0b101 << 27)        | // opcode for LDP/STP
                   (0b010101 << 21)     | // P,U,W,L bits
                   (imm7 << 15)         | // imm7
                   ((rt2 & 0x1F) << 10) | // Rt2
                   ((rn & 0x1F) << 5)   | // Rn
                   (rt1 & 0x1F);          // Rt1
//        return (0b10 << 30) | (0b101 << 27) | (0b0 << 26) | (0b011 << 23) |
//               (imm7 << 15) | ((rt2 & 0x1F) << 10) | ((rn & 0x1F) << 5) | (rt1 & 0x1F);
    }

    // LDRB Wt, [Xn, Xm]
    uint32_t gen_ldrb_reg(int rt, int rn, int rm) const {
        return (0b00 << 30) | (0b111 << 27) | (0b01 << 22) | (0b1 << 21) |
               ((rm & 0x1F) << 16) | (0b011 << 13) | (0b0 << 12) |
               ((rn & 0x1F) << 5) | (rt & 0x1F);
    }

    // STRB Wt, [Xn, Xm]
    uint32_t gen_strb_reg(int rt, int rn, int rm) const {
        return (0b00 << 30) | (0b111 << 27) | (0b00 << 22) | (0b1 << 21) |
               ((rm & 0x1F) << 16) | (0b011 << 13) | (0b0 << 12) |
               ((rn & 0x1F) << 5) | (rt & 0x1F);
    }

    // LDRH Wt, [Xn, Xm]
    uint32_t gen_ldrh_reg(int rt, int rn, int rm) const {
        return (0b01 << 30) | (0b111 << 27) | (0b01 << 22) | (0b1 << 21) |
               ((rm & 0x1F) << 16) | (0b011 << 13) | (0b0 << 12) |
               ((rn & 0x1F) << 5) | (rt & 0x1F);
    }

    // STRH Wt, [Xn, Xm]
    uint32_t gen_strh_reg(int rt, int rn, int rm) const {
        return (0b01 << 30) | (0b111 << 27) | (0b00 << 22) | (0b1 << 21) |
               ((rm & 0x1F) << 16) | (0b011 << 13) | (0b0 << 12) |
               ((rn & 0x1F) << 5) | (rt & 0x1F);
    }

    // LDR Xt, [Xn, Xm]
    uint32_t gen_ldr_x_reg(int rt, int rn, int rm) const {
        return (0b11 << 30) | (0b111 << 27) | (0b01 << 22) | (0b1 << 21) |
               ((rm & 0x1F) << 16) | (0b011 << 13) | (0b0 << 12) |
               ((rn & 0x1F) << 5) | (rt & 0x1F);
    }

    // STR Xt, [Xn, Xm]
    uint32_t gen_str_x_reg2(int rt, int rn, int rm) const {
        return (0b11 << 30) | (0b111 << 27) | (0b00 << 22) | (0b1 << 21) |
               ((rm & 0x1F) << 16) | (0b011 << 13) | (0b0 << 12) |
               ((rn & 0x1F) << 5) | (rt & 0x1F);
    }

    // ===== Helper Methods =====
    
    void load_immediate_64(int reg, uint64_t value) {
        uint16_t parts[4] = {
            (uint16_t)(value & 0xFFFF),
            (uint16_t)((value >> 16) & 0xFFFF),
            (uint16_t)((value >> 32) & 0xFFFF),
            (uint16_t)((value >> 48) & 0xFFFF)
        };
        
        int first = -1;
        for (int i = 0; i < 4; i++) {
            if (parts[i] != 0) {
                first = i;
                break;
            }
        }
        
        if (first == -1) {
            emit(gen_movz_x(reg, 0, 0));
            return;
        }
        
        emit(gen_movz_x(reg, parts[first], first * 16));
        for (int i = first + 1; i < 4; i++) {
            if (parts[i] != 0) {
                emit(gen_movk_x(reg, parts[i], i * 16));
            }
        }
    }
    
    void load_immediate_32(int reg, uint32_t value) {
        uint16_t low = value & 0xFFFF;
        uint16_t high = (value >> 16) & 0xFFFF;
        
        emit(gen_movz_w(reg, low, 0));
        if (high != 0) {
            emit(gen_movk_w(reg, high, 16));
        }
    }
    
    // Load variable into register
    void load_var_to_reg(int var_index, int dest_reg) {
        int var_reg = get_var_reg(var_index);
        if (var_reg >= 0) {
            if (var_reg != dest_reg) {
                emit(gen_mov_w(dest_reg, var_reg));
            }
        } else {
            int offset = get_var_stack_offset(var_index);
            emit(gen_ldr_w_imm(dest_reg, 31, offset / 4));
        }
    }
    
    // Store register to variable
    void store_reg_to_var(int src_reg, int var_index) {
        int var_reg = get_var_reg(var_index);
        if (var_reg >= 0) {
            if (var_reg != src_reg) {
                emit(gen_mov_w(var_reg, src_reg));
            }
        } else {
            int offset = get_var_stack_offset(var_index);
            emit(gen_str_w_imm(src_reg, 31, offset / 4));
        }
    }

public:
    ARM64JIT() : executable_memory(nullptr), executable_size(0) {}
    
    ~ARM64JIT() {
        if (executable_memory) {
            munmap(executable_memory, executable_size);
        }
    }
    
    /**
     * Begin JIT code generation
     * Sets up function prologue and saves registers
     */
    void begin() {
        code.clear();
        
        // Stack frame size: 128 bytes (16-byte aligned)
        // [sp+0 to sp+87]:   22 variables (10-31) = 88 bytes
        // [sp+88 to sp+127]: Padding + saved registers area
        int stack_size = 128;
        /*
        // Save FP and LR, and allocate stack space
        // stp x29, x30, [sp, #-16]!  (pre-index by 16)
        emit(0xA9BF7BFD);  // stp x29, x30, [sp, #-16]!
        
        // mov x29, sp - Must use ADD since x31 in MOV means XZR, not SP!
        // add x29, sp, #0
        emit(gen_add_x_imm(29, 31, 0));
        
        // sub sp, sp, #(stack_size - 16)
        emit(gen_sub_x_imm(31, 31, stack_size - 16));
        */
        
        emit(gen_sub_x_imm(31, 31, stack_size + 16)); // sub sp, sp, #0x90
        emit(0xA9087BFD); // stp x29, x30, [sp, #0x80]
        emit(gen_add_x_imm(29, 31, 128)); // add    x29, sp, #0x80
        
        // Save callee-saved registers
        emit(gen_stp_x(19, 20, 31, 0));
        emit(gen_stp_x(21, 22, 31, 16));
        emit(gen_stp_x(23, 24, 31, 32));
        emit(gen_stp_x(25, 26, 31, 48));
        emit(gen_stp_x(27, 28, 31, 64));
    }
    
    /**
     * End JIT code generation
     * Restores registers and returns
     */
    void end() {
        int stack_size = 128;
        
        // Restore callee-saved registers
        emit(gen_ldp_x(19, 20, 31, 0));
        emit(gen_ldp_x(21, 22, 31, 16));
        emit(gen_ldp_x(23, 24, 31, 32));
        emit(gen_ldp_x(25, 26, 31, 48));
        emit(gen_ldp_x(27, 28, 31, 64));
        /*
        // Restore stack pointer: add sp, sp, #(stack_size - 16)
        emit(gen_add_x_imm(31, 31, stack_size - 16));
        
        // Restore FP and LR, and adjust SP
        // ldp x29, x30, [sp], #16  (post-index)
        emit(0xA8C17BFD);  // ldp x29, x30, [sp], #16
        */
        
        emit(0xA9487BFD);
        emit(gen_add_x_imm(31, 31, stack_size+16));
        
        // ret
        emit(gen_ret());
    }
    

    /**
     * store(uint64_t address, uint64_t value)
     * Stores a 64-bit value to buffer[address]
     */
    void store(uint64_t address, uint64_t value) {
        load_immediate_64(1, value);
        
        if (address < 32768 && (address % 8) == 0) {
            emit(gen_str_x_imm(1, 0, address / 8));
        } else {
            load_immediate_64(2, address);
            emit(gen_str_x_reg(1, 0, 2));
        }
    }
    
    /**
     * load(uint64_t address, int index_of_variable)
     * Loads a 32-bit value from buffer[address] into variable
     */
    void load(uint64_t address, int index_of_variable) {
        if (address < 16384 && (address % 4) == 0) {
            emit(gen_ldr_w_imm(1, 0, address / 4));
        } else {
            load_immediate_64(2, address);
            emit(gen_ldr_w_reg(1, 0, 2));
        }
        
        store_reg_to_var(1, index_of_variable);
    }
    
    /**
     * loadImmediate(int index_of_variable, uint64_t value)
     * Loads an immediate value into a variable
     */
    void loadImmediate(int index_of_variable, uint64_t value) {
        uint32_t val32 = (uint32_t)value;
        
        int var_reg = get_var_reg(index_of_variable);
        if (var_reg >= 0) {
            load_immediate_32(var_reg, val32);
        } else {
            load_immediate_32(1, val32);
            int offset = get_var_stack_offset(index_of_variable);
            emit(gen_str_w_imm(1, 31, offset / 4));
        }
    }
    
    /****
     * loadFromVarAddress(int addressVarIndex, int destVarIndex)
     * Loads a 32-bit value from buffer at address held in variable `addressVarIndex`
     * and stores it into variable `destVarIndex`.
     */
    void load32FromVarAddress(int addressVarIndex, int destVarIndex) {
        // Load the address (offset relative to buffer base in x0) into w2
        load_var_to_reg(addressVarIndex, 2); // w2 = var[addressVarIndex]
        // Perform load: ldr w1, [x0, x2]
        emit(gen_ldr_w_reg(1, 0, 2));
        // Store the 32-bit loaded value into destination variable
        store_reg_to_var(1, destVarIndex);
    }
    /**
     * storeToVarAddress(int addressVarIndex, int srcVarIndex)
     * Stores a 32-bit value from variable `srcVarIndex` into buffer at address
     * held in variable `addressVarIndex`.
     */
    void store32ToVarAddress(int addressVarIndex, int srcVarIndex);

    /** Load 8 bits from buffer at address in variable into dest variable (zero-extend). */
    void load8FromVarAddress(int addressVarIndex, int destVarIndex) {
        load_var_to_reg(addressVarIndex, 2); // w2 = offset
        emit(gen_ldrb_reg(1, 0, 2));         // w1 = *(uint8_t*)(x0 + x2)
        store_reg_to_var(1, destVarIndex);   // store zero-extended byte into var
    }

    /** Store 8 bits from src variable to buffer at address in variable. */
    void store8ToVarAddress(int addressVarIndex, int srcVarIndex) {
        load_var_to_reg(addressVarIndex, 2); // w2 = offset
        load_var_to_reg(srcVarIndex, 1);     // w1 = value (low 8 bits used)
        emit(gen_strb_reg(1, 0, 2));         // *(uint8_t*)(x0 + x2) = w1
    }

    /** Load 16 bits from buffer at address in variable into dest variable (zero-extend). */
    void load16FromVarAddress(int addressVarIndex, int destVarIndex) {
        load_var_to_reg(addressVarIndex, 2);
        emit(gen_ldrh_reg(1, 0, 2));         // w1 = *(uint16_t*)(x0 + x2)
        store_reg_to_var(1, destVarIndex);
    }

    /** Store 16 bits from src variable to buffer at address in variable. */
    void store16ToVarAddress(int addressVarIndex, int srcVarIndex) {
        load_var_to_reg(addressVarIndex, 2);
        load_var_to_reg(srcVarIndex, 1);
        emit(gen_strh_reg(1, 0, 2));
    }

    /** Load 64 bits from buffer at address in variable into dest variable index (stores low 32 bits). */
    void load64FromVarAddress(int addressVarIndex, int destVarIndex) {
        load_var_to_reg(addressVarIndex, 2); // w2 = offset
        // Load 64-bit into x3 then move low 32 to w1 for variable storage
        emit(gen_ldr_x_reg(3, 0, 2));       // x3 = *(uint64_t*)(x0 + x2)
        // Move low 32 bits of x3 into w1 (mov w1, w3)
        emit(gen_mov_w(1, 3));
        store_reg_to_var(1, destVarIndex);
    }

    /** Store 64 bits from src variable to buffer at address in variable using zero-extended 32-bit src. */
    void store64ToVarAddress(int addressVarIndex, int srcVarIndex) {
        load_var_to_reg(addressVarIndex, 2); // w2 = offset
        // Load 32-bit src into w1, zero-extend to x1 implicitly by using x1 in store
        load_var_to_reg(srcVarIndex, 1);
        // Move w1 to x1 (zero-extend) using ORR X1, XZR, X1
        emit(gen_mov_x(1, 1));
        // Store x1 into memory
        emit(gen_str_x_reg2(1, 0, 2));
    }
    
    /**
     * hostCall(int func, int arg0, int arg1, int arg2)
     * Calls a host function with 3 variable arguments
     */
    void hostCall(int func, int arg0, int arg1, int arg2) {
        emit(gen_mov_x(10, 0));
        
        if (func < 4096) {
            emit(gen_ldr_x_imm(9, 0, func));
        } else {
            load_immediate_64(8, func * 8);
            emit(gen_add_x_reg(8, 0, 8));
            emit(gen_ldr_x_imm(9, 8, 0));
        }
        
        load_var_to_reg(arg0, 0);
        load_var_to_reg(arg1, 1);
        load_var_to_reg(arg2, 2);
        
        emit(gen_blr(9));
        emit(gen_mov_x(0, 10));
    }
    
    /**
     * add(int var1, int var2, int var3)
     * var1 = var2 + var3
     */
    void add(int var1, int var2, int var3) {
        load_var_to_reg(var2, 1);
        load_var_to_reg(var3, 2);
        emit(gen_add_w_reg(1, 1, 2));
        store_reg_to_var(1, var1);
    }
    
    /**
     * compare(int var1, int var2, int var_result)
     * var_result = (var1 < var2) ? -1 : ((var1 > var2) ? 1 : 0)
     */
    void compare(int var1, int var2, int var_result) {
        // Load variables
        load_var_to_reg(var1, 1);
        load_var_to_reg(var2, 2);
        
        // Compare: cmp w1, w2
        emit(gen_cmp_w(1, 2));
        
        // Set result based on flags
        // If less than (LT): result = -1
        // If greater than (GT): result = 1
        // If equal (EQ): result = 0
        
        // First, set to 0
        emit(gen_movz_w(3, 0, 0));  // w3 = 0
        
        // cset w4, GT (w4 = 1 if GT, else 0)
        emit(gen_cset_w(4, 0xC));  // GT condition
        
        // cset w5, LT (w5 = 1 if LT, else 0)
        emit(gen_cset_w(5, 0xB));  // LT condition
        
        // result = w4 - w5  (1-0=1 if GT, 0-1=-1 if LT, 0-0=0 if EQ)
        // SUB w3, w4, w5
        emit((0b0 << 31) | (0b1001011 << 24) | (0b00 << 22) |
             (5 << 16) | (4 << 5) | 3);
        
        store_reg_to_var(3, var_result);
    }
    
    /**
     * branchIfEqual(int var1, int var2, size_t target_index)
     * Branch to target_index if var1 == var2
     */
    void branchIfEqual(int var1, int var2, size_t target_index) {
        load_var_to_reg(var1, 1);
        load_var_to_reg(var2, 2);
        
        // cmp w1, w2
        emit(gen_cmp_w(1, 2));
        
        // Calculate offset (target - current - 1)
        int32_t offset = (int32_t)target_index - (int32_t)code.size() - 1;
        
        // b.eq target (condition code 0x0 = EQ)
        emit(gen_bcond(0x0, offset));
    }
    
    /**
     * jump(size_t target_index)
     * Unconditional jump to target_index
     */
    void jump(size_t target_index) {
        int32_t offset = (int32_t)target_index - (int32_t)code.size() - 1;
        emit(gen_b(offset));
    }
    
    /**
     * call(size_t target_index)
     * Call function at target_index (saves return address)
     */
    void call(size_t target_index) {
        int32_t offset = (int32_t)target_index - (int32_t)code.size() - 1;
        emit(gen_bl(offset));
    }
    
    /**
     * ret()
     * Return from function (returns to address in x30)
     */
    void ret() {
        emit(gen_ret());
    }
    
    /**
     * getCurrentIndex()
     * Returns current position in code array (for branch targets)
     */
    size_t getCurrentIndex() const {
        return code.size();
    }
    
    /**
     * patchBranch(size_t branch_index, size_t target_index)
     * Patch a previously emitted branch instruction
     */
    void patchBranch(size_t branch_index, size_t target_index) {
        if (branch_index >= code.size()) return;
        
        uint32_t inst = code[branch_index];
        int32_t offset = (int32_t)target_index - (int32_t)branch_index - 1;
        
        // Check if it's a conditional branch (b.cond)
        if ((inst & 0xFF000000) == 0x54000000) {
            int32_t imm19 = offset & 0x7FFFF;
            code[branch_index] = (inst & 0xFF00001F) | (imm19 << 5);
        }
        // Check if it's unconditional branch (b)
        else if ((inst & 0xFC000000) == 0x14000000) {
            int32_t imm26 = offset & 0x3FFFFFF;
            code[branch_index] = (inst & 0xFC000000) | imm26;
        }
        // Check if it's bl
        else if ((inst & 0xFC000000) == 0x94000000) {
            int32_t imm26 = offset & 0x3FFFFFF;
            code[branch_index] = (inst & 0xFC000000) | imm26;
        }
    }
    
    /**
     * Make code executable and return function pointer
     */
    void* finalize() {
        size_t code_size = code.size() * sizeof(uint32_t);
        size_t page_size = sysconf(_SC_PAGESIZE);
        executable_size = (code_size + page_size - 1) & ~(page_size - 1);
        
        executable_memory = mmap(nullptr, executable_size,
                                PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
        
        if (executable_memory == MAP_FAILED) {
            return nullptr;
        }
        
        memcpy(executable_memory, code.data(), code_size);
        
        if (mprotect(executable_memory, executable_size,
                    PROT_READ | PROT_EXEC) != 0) {
            munmap(executable_memory, executable_size);
            executable_memory = nullptr;
            return nullptr;
        }
        
        sys_icache_invalidate(executable_memory, code_size);
        
        return executable_memory;
    }
    
    void disassemble() const {
        printf("Generated code (%zu instructions, %zu bytes):\n",
               code.size(), code.size() * 4);
        for (size_t i = 0; i < code.size(); i++) {
            printf("%04zx: %08x\n", i * 4, code[i]);
        }
    }
    
    size_t getCodeSize() const {
        return code.size() * sizeof(uint32_t);
    }
};

#endif // ARM64_JIT_H

