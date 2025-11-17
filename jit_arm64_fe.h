#ifndef JIT_ARM64_FE_H
#define JIT_ARM64_FE_H

#include "jit_arm64_be.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
#include <libkern/OSCacheControl.h>

/**
 * ARM64 JIT Frontend - High-level code generation interface
 * 
 * Generated function signature:
 *   void jit_func(void* memory, uint64_t* registers, int entry_point)
 * 
 * Parameters:
 *   - memory: Pointer to machine memory buffer
 *   - registers: Pointer to array of 16 uint64_t registers
 *   - entry_point: Instruction index to jump to (0 for start)
 * 
 * Register usage in generated code:
 *   - x0: memory pointer (preserved)
 *   - x1: registers pointer (preserved)
 *   - x2-x17: temporary/scratch registers
 *   - x19-x28: Available (minimal callee-saved usage)
 *   - x29: FP
 *   - x30: LR
 *   - SP: Stack pointer
 */
class ARM64JITFrontend {
private:
    std::vector<uint32_t> code;
    void* executable_memory;
    size_t executable_size;
    
    // Jump table for entry points
    std::vector<size_t> entry_points;
    
    size_t emit(uint32_t instruction) {
        code.push_back(instruction);
        return code.size() - 1;
    }
    
    /**
     * Load a 64-bit immediate value into a register
     * Uses MOVZ and MOVK instructions
     */
    void emit_load_imm64(int reg, uint64_t value) {
        uint16_t parts[4] = {
            (uint16_t)(value & 0xFFFF),
            (uint16_t)((value >> 16) & 0xFFFF),
            (uint16_t)((value >> 32) & 0xFFFF),
            (uint16_t)((value >> 48) & 0xFFFF)
        };
        
        // Find first non-zero part
        int first = -1;
        for (int i = 0; i < 4; i++) {
            if (parts[i] != 0) {
                first = i;
                break;
            }
        }
        
        if (first == -1) {
            emit(ARM64Backend::gen_movz_x(reg, 0, 0));
            return;
        }
        
        emit(ARM64Backend::gen_movz_x(reg, parts[first], first * 16));
        for (int i = first + 1; i < 4; i++) {
            if (parts[i] != 0) {
                emit(ARM64Backend::gen_movk_x(reg, parts[i], i * 16));
            }
        }
    }
    
    /**
     * Load a 32-bit immediate value into a register (W register)
     */
    void emit_load_imm32(int reg, uint32_t value) {
        uint16_t low = value & 0xFFFF;
        uint16_t high = (value >> 16) & 0xFFFF;
        
        emit(ARM64Backend::gen_movz_w(reg, low, 0));
        if (high != 0) {
            emit(ARM64Backend::gen_movk_w(reg, high, 16));
        }
    }
    
public:
    ARM64JITFrontend() : executable_memory(nullptr), executable_size(0) {}
    
    ~ARM64JITFrontend() {
        if (executable_memory) {
            munmap(executable_memory, executable_size);
        }
    }
    
    /**
     * Begin code generation
     * Sets up function prologue
     * 
     * On entry:
     *   x0 = memory pointer
     *   x1 = registers pointer
     *   x2 = entry_point (instruction index)
     */
    void begin() {
        code.clear();
        entry_points.clear();
        
        // Minimal prologue - only save FP and LR
        // We'll use x19 and x20 to preserve x0 and x1
        
        // Adjust SP and save FP, LR
        emit(ARM64Backend::gen_sub_x_imm(31, 31, 16));     // sub sp, sp, #16
        emit(ARM64Backend::gen_stp_x(29, 30, 31, 0));      // stp x29, x30, [sp]
        //emit(ARM64Backend::gen_mov_x(29, 31));             // mov x29, sp
        emit(ARM64Backend::gen_add_x_imm(29, 31, 0)); // add    x29, sp, #0
        
        // Adjust SP and save x19, x20
        emit(ARM64Backend::gen_sub_x_imm(31, 31, 16));     // sub sp, sp, #16
        emit(ARM64Backend::gen_stp_x(19, 20, 31, 0));      // stp x19, x20, [sp]
        
        // Preserve x0 (memory) and x1 (registers) in x19 and x20
        emit(ARM64Backend::gen_mov_x(19, 0));  // x19 = memory pointer
        emit(ARM64Backend::gen_mov_x(20, 1));  // x20 = registers pointer
        
        // Handle entry_point (x2)
        // If entry_point != 0, we would need a jump table
        // For now, we just proceed (entry_point will be implemented with jump table)
        
        // Note: Code will be generated after this point
        entry_points.push_back(code.size());  // Entry point 0 = start
    }
    
    /**
     * End code generation
     * Restores registers and returns
     */
    size_t end() {
        size_t pos = getCurrentIndex();
        // Restore x19, x20 and adjust SP
        emit(ARM64Backend::gen_ldp_x(19, 20, 31, 0));      // ldp x19, x20, [sp]
        emit(ARM64Backend::gen_add_x_imm(31, 31, 16));     // add sp, sp, #16
        
        // Restore FP, LR and adjust SP
        emit(ARM64Backend::gen_ldp_x(29, 30, 31, 0));      // ldp x29, x30, [sp]
        emit(ARM64Backend::gen_add_x_imm(31, 31, 16));     // add sp, sp, #16
        
        // Return
        emit(ARM64Backend::gen_ret());
        return pos;
    }
    
    /**
     * Mark a new entry point
     * Returns the entry point index
     */
//    int markEntryPoint() {
//        entry_points.push_back(code.size());
//        return entry_points.size() - 1;
//    }
    
    // ===== Register Operations =====
    
    /**
     * Load a register value into a temporary register
     * Returns: x2 = registers[reg_index] (full 64-bit)
     */
    void loadRegister(int reg_index, int temp_reg = 2) {
        // Load from registers array: ldr xt, [x20, #(reg_index * 8)]
        emit(ARM64Backend::gen_ldr_x_imm(temp_reg, 20, reg_index));
        
//        if (reg_index < 4096) {
//            emit(ARM64Backend::gen_ldr_x_imm(temp_reg, 20, reg_index));
//        } else {
//            // For large indices, compute address
//            emit_load_imm64(temp_reg, reg_index * 8);
//            emit(ARM64Backend::gen_add_x_reg(temp_reg, 20, temp_reg));
//            emit(ARM64Backend::gen_ldr_x_imm(temp_reg, temp_reg, 0));
//        }
    }
    
    /**
     * Store a temporary register into a register slot
     * Stores: registers[reg_index] = xt (full 64-bit)
     */
    void storeRegister(int reg_index, int temp_reg = 2) {
        emit(ARM64Backend::gen_str_x_imm(temp_reg, 20, reg_index));
        // Store to registers array: str xt, [x20, #(reg_index * 8)]
//        if (reg_index < 4096) {
//            emit(ARM64Backend::gen_str_x_imm(temp_reg, 20, reg_index));
//        } else {
//            emit_load_imm64(3, reg_index * 8);
//            emit(ARM64Backend::gen_add_x_reg(3, 20, 3));
//            emit(ARM64Backend::gen_str_x_imm(temp_reg, 3, 0));
//        }
    }
    
    /**
     * Move register value
     * registers[dest] = registers[src]
     */
    void mov(int dest, int src) {
        loadRegister(src, 2);
        storeRegister(dest, 2);
    }
    
    /**
     * Load immediate value into a register
     * registers[reg_index] = value (full 64-bit)
     */
    size_t loadImmediate(int reg_index, uint64_t value) {
        size_t pos = getCurrentIndex();
        emit_load_imm64(2, value);
        storeRegister(reg_index, 2);
        return pos;
    }
    
    /**
     * Load 64-bit value from memory
     * registers[dest_reg] = *(uint64_t*)(memory + address)
     */
    void loadMemory64(int dest_reg, uint64_t address) {
        emit_load_imm64(2, address);
        emit(ARM64Backend::gen_ldr_x_reg(2, 19, 2));  // ldr x2, [x19, x2]
        storeRegister(dest_reg, 2);
    }
    
    /**
     * Load 32-bit value from memory (zero-extended)
     * registers[dest_reg] = *(uint32_t*)(memory + address)
     */
    void loadMemory32(int dest_reg, uint64_t address) {
        emit_load_imm64(2, address);
        emit(ARM64Backend::gen_ldr_x_reg(2, 19, 2, 32));  // ldr w2, [x19, x2]
        storeRegister(dest_reg, 2);
    }
    
    /**
     * Load 16-bit value from memory (zero-extended)
     */
    void loadMemory16(int dest_reg, uint64_t address) {
        emit_load_imm64(2, address);
        emit(ARM64Backend::gen_ldr_x_reg(2, 19, 2, 16));
        storeRegister(dest_reg, 2);
    }
    
    /**
     * Load 8-bit value from memory (zero-extended)
     */
    void loadMemory8(int dest_reg, uint64_t address) {
        emit_load_imm64(2, address);
        emit(ARM64Backend::gen_ldr_x_reg(2, 19, 2, 8));
        storeRegister(dest_reg, 2);
    }
    
    /**
     * Store 64-bit value to memory
     * *(uint64_t*)(memory + address) = registers[src_reg]
     */
    void storeMemory64(int src_reg, uint64_t address) {
        loadRegister(src_reg, 2);
        emit_load_imm64(3, address);
        emit(ARM64Backend::gen_str_x_reg(2, 19, 3));
    }
    
    /**
     * Store 32-bit value to memory
     */
    void storeMemory32(int src_reg, uint64_t address) {
        loadRegister(src_reg, 2);
        emit_load_imm64(3, address);
        emit(ARM64Backend::gen_str_w_reg(2, 19, 3));
    }
    
    /**
     * Store 16-bit value to memory
     */
    void storeMemory16(int src_reg, uint64_t address) {
        loadRegister(src_reg, 2);
        emit_load_imm64(3, address);
        emit(ARM64Backend::gen_strh_reg(2, 19, 3));
    }
    
    /**
     * Store 8-bit value to memory
     */
    void storeMemory8(int src_reg, uint64_t address) {
        loadRegister(src_reg, 2);
        emit_load_imm64(3, address);
        emit(ARM64Backend::gen_strb_reg(2, 19, 3));
    }
    
    // TODO: add comment
    void storeMemory(int src_reg, uint64_t address, int size) {
        switch (size) {
            case 8:
                storeMemory64(src_reg, address);
                break;
            case 4:
                storeMemory32(src_reg, address);
                break;
            case 2:
                storeMemory16(src_reg, address);
                break;
            case 1:
                storeMemory8(src_reg, address);
                break;
            default:
                assert(0);
        }
    }
    
    /**
     * Load from memory using register as address
     * registers[dest_reg] = *(uint64_t*)(memory + registers[addr_reg])
     */
    void loadMemoryReg64(int dest_reg, int addr_reg) {
        loadRegister(addr_reg, 2);
        emit(ARM64Backend::gen_ldr_x_reg(2, 19, 2));
        storeRegister(dest_reg, 2);
    }
    
    /**
     * Load 32-bit from memory using register as address
     */
    void loadMemoryReg32(int dest_reg, int addr_reg) {
        loadRegister(addr_reg, 2);
        emit(ARM64Backend::gen_ldr_x_reg(2, 19, 2, 32));
        storeRegister(dest_reg, 2);
    }
    
    /**
     * Store to memory using register as address
     */
    void storeMemoryReg64(int addr_reg, int src_reg) {
        loadRegister(addr_reg, 2);
        loadRegister(src_reg, 3);
        emit(ARM64Backend::gen_str_x_reg(3, 19, 2));
    }
    
    /**
     * Store 32-bit to memory using register as address
     */
    void storeMemoryReg32(int addr_reg, int src_reg) {
        loadRegister(addr_reg, 2);
        loadRegister(src_reg, 3);
        emit(ARM64Backend::gen_str_w_reg(3, 19, 2));
    }
    
    // TODO: add comment, missing opts
    void loadMemory(int dest_reg, int addr_reg, int size) {
        switch (size) {
            case 8:
                loadMemoryReg64(dest_reg, addr_reg);
                break;
            case 4:
                loadMemoryReg32(dest_reg, addr_reg);
                break;
//            case 2:
//                loadMemoryReg16(dest_reg, addr_reg);
//                break;
//            case 1:
//                loadMemoryReg8(dest_reg, addr_reg);
//                break;
            default:
                assert(0);
        }
    }

    
    // ===== Arithmetic Operations =====
    
    /**
     * Add two registers
     * registers[dest] = registers[src1] + registers[src2]
     */
    void add(int dest, int src1, int src2) {
        loadRegister(src1, 2);
        loadRegister(src2, 3);
        emit(ARM64Backend::gen_add_x_reg(2, 2, 3));
        storeRegister(dest, 2);
    }
    
    /**
     * Subtract two registers
     * registers[dest] = registers[src1] - registers[src2]
     */
    void sub(int dest, int src1, int src2) {
        loadRegister(src1, 2);
        loadRegister(src2, 3);
        emit(ARM64Backend::gen_sub_x_reg(2, 2, 3));
        storeRegister(dest, 2);
    }
    
    /**
     * Multiply two registers
     * registers[dest] = registers[src1] * registers[src2]
     */
    void mul(int dest, int src1, int src2) {
        loadRegister(src1, 2);
        loadRegister(src2, 3);
        emit(ARM64Backend::gen_mul_x(2, 2, 3));
        storeRegister(dest, 2);
    }
    
    /**
     * Signed divide two registers
     * registers[dest] = registers[src1] / registers[src2] (signed)
     */
    void div(int dest, int src1, int src2) {
        loadRegister(src1, 2);
        loadRegister(src2, 3);
        emit(ARM64Backend::gen_sdiv_x(2, 2, 3));
        storeRegister(dest, 2);
    }
    
    /**
     * Signed modulo (remainder)
     * registers[dest] = registers[src1] % registers[src2] (unsigned)
     * Implemented as: dest = src1 - (src1 / src2) * src2
     */
    void mod(int dest, int src1, int src2) {
        loadRegister(src1, 2);   // x2 = dividend
        loadRegister(src2, 3);   // x3 = divisor
        
        // x4 = x2 / x3 (quotient, unsigned)
        emit(ARM64Backend::gen_udiv_x(4, 2, 3));
        
        // x2 = x2 - (x4 * x3)  using MSUB: x2 = x2 - (x4 * x3)
        emit(ARM64Backend::gen_msub_x(2, 4, 3, 2));
        
        storeRegister(dest, 2);
    }
    
    /**
     * Add immediate to register
     * registers[dest] = registers[src] + imm
     */
    void addImmediate(int dest, int src, uint64_t imm) {
        loadRegister(src, 2);
        if (imm <= 0xFFF) {
            emit(ARM64Backend::gen_add_x_imm(2, 2, (uint16_t)imm));
        } else {
            emit_load_imm64(3, imm);
            emit(ARM64Backend::gen_add_x_reg(2, 2, 3));
        }
        storeRegister(dest, 2);
    }
    
    /**
     * Signum function
     * registers[dest] = signum(registers[src])
     * Returns: -1 if src < 0, 0 if src == 0, 1 if src > 0
     * 
     * Algorithm:
     *   result = (src > 0) - (src < 0)
     * This is implemented as:
     *   cmp src, #0
     *   cset x2, gt      // x2 = 1 if src > 0, else 0
     *   cmp src, #0
     *   csinc x3, xzr, xzr, ge  // x3 = 0 if src >= 0, else 1
     *   sub x2, x2, x3   // result = (src > 0) - (src < 0)
     */
    void signum(int dest, int src) {
        loadRegister(src, 2);  // x2 = src value
        
        // Compare with 0
        emit(ARM64Backend::gen_cmp_x(2, 31));  // cmp x2, xzr
        
        // x3 = 1 if src > 0, else 0
        emit(ARM64Backend::gen_cset_x(3, ARM64Backend::COND_GT));
        
        // Compare with 0 again
        emit(ARM64Backend::gen_cmp_x(2, 31));  // cmp x2, xzr
        
        // x4 = 1 if src < 0, else 0
        emit(ARM64Backend::gen_cset_x(4, ARM64Backend::COND_LT));
        
        // result = x3 - x4 = (src > 0) - (src < 0)
        emit(ARM64Backend::gen_sub_x_reg(2, 3, 4));
        
        storeRegister(dest, 2);
    }
    
    // ===== Comparison and Branches =====
    
    /**
     * Compare two registers and set flags
     * Compares registers[reg1] with registers[reg2]
     */
    size_t compare(int reg1, int reg2) {
        size_t pos = getCurrentIndex();
        loadRegister(reg1, 2);
        loadRegister(reg2, 3);
        emit(ARM64Backend::gen_cmp_x(2, 3));
        return pos;
    }
    
    /**
     * Branch if equal
     * Branches to target if last comparison was equal
     */
    size_t branchIfEqual(size_t target_index = 0) {
        int32_t offset = (int32_t)target_index - (int32_t)code.size();
        return emit(ARM64Backend::gen_bcond(ARM64Backend::COND_EQ, offset));
    }
    
    /**
     * Branch if not equal
     */
    size_t branchIfNotEqual(size_t target_index) {
        int32_t offset = (int32_t)target_index - (int32_t)code.size();
        return emit(ARM64Backend::gen_bcond(ARM64Backend::COND_NE, offset));
    }
    
    /**
     * Branch if less than (signed)
     */
    size_t branchIfLessThan(size_t target_index) {
        int32_t offset = (int32_t)target_index - (int32_t)code.size();
        return emit(ARM64Backend::gen_bcond(ARM64Backend::COND_LT, offset));
    }
    
    /**
     * Branch if greater than (signed)
     */
    size_t branchIfGreaterThan(size_t target_index) {
        int32_t offset = (int32_t)target_index - (int32_t)code.size();
        return emit(ARM64Backend::gen_bcond(ARM64Backend::COND_GT, offset));
    }
    
    /**
     * Unconditional jump
     */
    size_t jump(size_t target_index = 0) {
        int32_t offset = (int32_t)target_index - (int32_t)code.size();
        return emit(ARM64Backend::gen_b(offset));
    }
    
    /**
     * Call subroutine (with link)
     */
    void call(size_t target_index) {
        int32_t offset = (int32_t)target_index - (int32_t)code.size();
        emit(ARM64Backend::gen_bl(offset));
    }
    
    /**
     * Return from subroutine
     */
    void ret() {
        emit(ARM64Backend::gen_ret());
    }
    
    /**
     * Patch a branch instruction at branch_index to target target_index
     */
    void patchBranch(size_t branch_index, size_t target_index) {
        if (branch_index >= code.size()) return;
        
        uint32_t inst = code[branch_index];
        int32_t offset = (int32_t)target_index - (int32_t)branch_index;
        
        // Conditional branch (b.cond)
        if ((inst & 0xFF000000) == 0x54000000) {
            int32_t imm19 = offset & 0x7FFFF;
            code[branch_index] = (inst & 0xFF00001F) | (imm19 << 5);
        }
        // Unconditional branch (b)
        else if ((inst & 0xFC000000) == 0x14000000) {
            int32_t imm26 = offset & 0x3FFFFFF;
            code[branch_index] = (inst & 0xFC000000) | imm26;
        }
        // Branch with link (bl)
        else if ((inst & 0xFC000000) == 0x94000000) {
            int32_t imm26 = offset & 0x3FFFFFF;
            code[branch_index] = (inst & 0xFC000000) | imm26;
        }
    }
    
    // ===== Host Function Calls =====
    
    /**
     * Call a host function with register arguments
     * All 16 registers are passed as uint64_t values
     * 
     * void (*func)(uint64_t regs[16])
     */
    void hostCall(uint64_t func_ptr) {
        // Save x19, x20 (they contain our memory and registers pointers)
        // They're already saved, but we need to make sure they're not clobbered
        
        // Load function pointer into x9
        emit_load_imm64(9, func_ptr);
        
        // Argument is x20 (registers pointer)
        emit(ARM64Backend::gen_mov_x(0, 20));
        
        // Call function
        emit(ARM64Backend::gen_blr(9));
        
        // After return, our x19 and x20 are still intact (callee-saved)
    }
    
    /**
     * Call a host function with explicit register arguments
     * void (*func)(uint64_t r0, uint64_t r1, uint64_t r2, uint64_t r3, ...)
     */
    size_t hostCallWithArgs(uint64_t func_ptr, int arg0)
    {
        size_t pos = getCurrentIndex();
        // Load function pointer
        emit_load_imm64(9, func_ptr);
        
        // Load arguments into x0-x7
        loadRegister(arg0, 0);
        emit(ARM64Backend::gen_blr(9));
        return pos;
    }

    size_t hostCallWithArgs(uint64_t func_ptr, int arg0, int arg1, int arg2)
    {
        size_t pos = getCurrentIndex();
        // Load function pointer
        emit_load_imm64(9, func_ptr);
        
        // Load arguments into x0-x7
        loadRegister(arg0, 0);
        loadRegister(arg1, 1);
        loadRegister(arg2, 2);
//        loadRegister(arg3, 3);
//        if (arg4 >= 0) loadRegister(arg4, 4);
//        if (arg5 >= 0) loadRegister(arg5, 5);
//        if (arg6 >= 0) loadRegister(arg6, 6);
//        if (arg7 >= 0) loadRegister(arg7, 7);
        
        // Call function
        emit(ARM64Backend::gen_blr(9));
        
        return pos;
        // Restore preserved pointers (x19 = memory, x20 = registers)
        // No need to restore, they're callee-saved
    }

    size_t hostCallWithReturnArgs(uint64_t func_ptr, int argOut)
    {
        size_t pos = getCurrentIndex();
        emit_load_imm64(9, func_ptr);
        emit(ARM64Backend::gen_blr(9));
        storeRegister(argOut, 0);
        return pos;
    }

    // ===== Code Management =====
    
    /**
     * Get current instruction index
     */
    size_t getCurrentIndex() const {
        return code.size();
    }
    
    /**
     * Get code size in bytes
     */
    size_t getCodeSize() const {
        return code.size() * sizeof(uint32_t);
    }
    
    /**
     * Finalize code and make it executable
     * Returns a function pointer: void (*)(void*, uint64_t*, int)
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
    
    /**
     * Disassemble generated code (hex dump)
     */
    void disassemble() const {
        printf("Generated code (%zu instructions, %zu bytes):\n",
               code.size(), code.size() * 4);
        for (size_t i = 0; i < code.size(); i++) {
            printf("%04zx: %08x\n", i * 4, code[i]);
        }
    }
};

#endif // JIT_ARM64_FE_H
