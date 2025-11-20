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
public:
    using Operand = EVM2::Arg;

private:
    std::vector<uint32_t> code;
    void* executable_memory;
    size_t executable_size;
    
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
     *   x2 = entry_point (number of ARM64 instructions to skip)
     */
    void begin() {
        code.clear();
        
        // Minimal prologue - only save FP and LR
        // We'll use x19 and x20 to preserve x0 and x1
        
        // Adjust SP and save FP, LR
        emit(ARM64Backend::gen_sub_x_imm(31, 31, 16));     // sub sp, sp, #16
        emit(ARM64Backend::gen_stp_x(29, 30, 31, 0));      // stp x29, x30, [sp]
        emit(ARM64Backend::gen_add_x_imm(29, 31, 0));      // add x29, sp, #0
        
        // Adjust SP and save x19, x20
        emit(ARM64Backend::gen_sub_x_imm(31, 31, 16));     // sub sp, sp, #16
        emit(ARM64Backend::gen_stp_x(19, 20, 31, 0));      // stp x19, x20, [sp]
        
        // Preserve x0 (memory) and x1 (registers)
        emit(ARM64Backend::gen_mov_x(19, 0));              // mov x19, x0 (memory pointer)
        emit(ARM64Backend::gen_mov_x(20, 1));              // mov x20, x1 (registers pointer)
        
        // Handle entry_point jump: skip x2 ARM64 instructions
        // x2 = number of instructions to skip (0 = no skip)
        // Each instruction is 4 bytes, so offset = x2 * 4
        // 
        // Layout:
        //   [N+0] lsl x9, x2, #2     - Multiply skip count by 4
        //   [N+1] adr x10, #12-entry()*4 (base address)
        //   [N+2] add x9, x10, x9    - Compute target = base + (skip * 4)
        //   [N+3] br x9              - Jump to computed address
        //   [N+4] <-- Target when x2=0 (first generated instruction)
        
        emit(ARM64Backend::gen_lsl_x_imm(9, 2, 2));        // lsl x9, x2, #2  (x9 = x2 * 4)
        emit(ARM64Backend::gen_adr(10, 12-(int)entry()*4));     // adr x10, #12-entry()*4 (get PC + base offset)
        emit(ARM64Backend::gen_add_x_reg(9, 10, 9));       // add x9, x10, x9 (x9 = PC + offset)
        emit(ARM64Backend::gen_br(9));                     // br x9 (jump to computed address)
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
    
    
    // ===== Register Operations =====
    void loadOperand(const Operand& op, int temp_reg = 2) {
        switch (op.kind)
        {
            case Operand::Kind::NONE:
                break;
            case Operand::Kind::REG:
                emit(ARM64Backend::gen_ldr_x_imm(temp_reg, 20, op.reg));
                break;
            case Operand::Kind::MEM:
                emit(ARM64Backend::gen_ldr_x_imm(temp_reg, 20, op.reg));
                emit(ARM64Backend::gen_reg_mem(temp_reg, 19, temp_reg, true, op.sizeBytes*8));
                break;
            case EVM2::Arg::Kind::ADDR:
                emit_load_imm64(temp_reg, op.addr);
                break;
            default:
                assert(0);
        }
    }

    void storeOperand(const Operand& op, int reg = 2)
    {
        switch (op.kind)
        {
            case Operand::Kind::NONE:
                break;
            case Operand::Kind::REG:
                emit(ARM64Backend::gen_str_x_imm(reg, 20, op.reg));
                break;
            case Operand::Kind::MEM:
                assert(reg != 3);
                emit(ARM64Backend::gen_ldr_x_imm(3, 20, op.reg));
                emit(ARM64Backend::gen_reg_mem(reg, 19, 3, false, op.sizeBytes*8));
                break;
            default:
                assert(0);
        }
    }
    
    /**
     * Move operands (used by MOV instruction)
     */
    void mov(Operand op1, Operand op2) {
        loadOperand(op2);
        storeOperand(op1);
    }
    
    /**
     * Load immediate value into an operand
     * dest = value (full 64-bit)
     */
    size_t loadImmediate(Operand dest, uint64_t value) {
        size_t pos = getCurrentIndex();
        emit_load_imm64(2, value);
        storeOperand(dest, 2);
        return pos;
    }
    
    /**
     * ALU operation type
     */
    enum class AluOp {
        ADD,      // dest = src1 + src2
        SUB,      // dest = src1 - src2
        MUL,      // dest = src1 * src2
        DIV,      // dest = src1 / src2 (signed)
        MOD,      // dest = src1 % src2 (unsigned)
        SIGNUM    // dest = signum(src1), src2 ignored
    };
    
    /**
     * Unified ALU operation
     * Performs arithmetic operation: dest = src1 OP src2
     */
    void alu(AluOp op, Operand dest, Operand src1, Operand src2 = {}) {
        loadOperand(src1, 2);
        loadOperand(src2, 3);

        switch (op) {
            case AluOp::ADD:
                emit(ARM64Backend::gen_add_x_reg(2, 2, 3));
                break;
                
            case AluOp::SUB:
                emit(ARM64Backend::gen_sub_x_reg(2, 2, 3));
                break;
                
            case AluOp::MUL:
                emit(ARM64Backend::gen_mul_x(2, 2, 3));
                break;
                
            case AluOp::DIV:
                emit(ARM64Backend::gen_sdiv_x(2, 2, 3));
                break;
                
            case AluOp::MOD:
                // dest = src1 - (src1 / src2) * src2
                emit(ARM64Backend::gen_udiv_x(4, 2, 3));
                emit(ARM64Backend::gen_msub_x(2, 4, 3, 2));
                break;
                
            case AluOp::SIGNUM:
                // Returns: -1 if src1 < 0, 0 if src1 == 0, 1 if src1 > 0
                emit(ARM64Backend::gen_cmp_x(2, 31));
                emit(ARM64Backend::gen_cset_x(3, ARM64Backend::ConditionCode::COND_GT));
                emit(ARM64Backend::gen_cmp_x(2, 31));
                emit(ARM64Backend::gen_cset_x(4, ARM64Backend::ConditionCode::COND_LT));
                emit(ARM64Backend::gen_sub_x_reg(2, 3, 4));
                break;
        }
        
        storeOperand(dest, 2);
    }
    
    /**
     * Compare two operands and set condition flags
     */
    size_t compare(const Operand& op1, const Operand& op2) {
        size_t pos = getCurrentIndex();
        loadOperand(op1, 2);
        loadOperand(op2, 3);
        emit(ARM64Backend::gen_cmp_x(2, 3));
        return pos;
    }

    /**
     * Branch if equal
     */
    size_t branchIfEqual(size_t target_index = 0) {
        int32_t offset = (int32_t)target_index - (int32_t)code.size();
        return emit(ARM64Backend::gen_bcond(ARM64Backend::ConditionCode::COND_EQ, offset));
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
    size_t call(size_t target_index = 0) {
        int32_t offset = (int32_t)target_index - (int32_t)code.size();
        return emit(ARM64Backend::gen_bl(offset));
    }
    
    /**
     * Return from subroutine
     */
    void ret() {
        emit(ARM64Backend::gen_ret());
    }

    /**
     * No operation
     */
    void nop() {
        emit(ARM64Backend::gen_nop());
    }
    
    /**
     * Function prologue for CALL targets
     */
    void funcPrologue() {
        emit(ARM64Backend::gen_prologue1());
        emit(ARM64Backend::gen_prologue2());
    }

    /**
     * Function epilogue for RET instruction
     */
    void funcEpilogue() {
        emit(ARM64Backend::gen_epilogue());
    }

    /**
     * Patch a branch instruction or immediate value
     */
    void patchBranchOrImm(size_t branch_index, size_t target_index) {
        if (branch_index >= code.size()) return;
        
        uint32_t inst = code[branch_index];

        // MOVZ immediate (used for thread entry points)
        if ((inst & 0xFF000000) == 0xD2000000) {
            int32_t imm16 = target_index & 0xffff;
            assert(imm16 == target_index);
            code[branch_index] = (inst & ~(0xffff << 5)) | (imm16 << 5);
            return;
        }

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
    
    /**
     * Call a host function with operand arguments
     * Loads operands into x0-x3 and stores result from x0
     */
    size_t hostCallWithOps(uint64_t func_ptr, const Operand& ret, const Operand& op1, 
                           const Operand& op2 = {}, const Operand& op3 = {}, const Operand& op4 = {}) {
        size_t pos = getCurrentIndex();
        loadOperand(op1, 0);
        loadOperand(op2, 1);
        loadOperand(op3, 2);
        loadOperand(op4, 3);
        emit_load_imm64(9, func_ptr);
        emit(ARM64Backend::gen_blr(9));
        storeOperand(ret, 0);
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
     * Index of first instruction after main prologue
     */

    size_t entry()
    {
        return 11;
    }
};
