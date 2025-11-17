//
//  main.cpp
//  eset1
//
//  Created by Gabriel Valky on 17/11/2025.
//


#include "evm2.h"
#include "jit_arm64_fe.h"
#include <map>
#include <csetjmp>

// Global jump buffer for handling HALT
thread_local jmp_buf halt_jmp_buf;

typedef void (*JITFunction)(void* memory, uint64_t* registers, int entry_point);

void host_print_value(uint64_t value) {
    printf("[HOST] Value: %lld / 0x%llx\n", value, value);
}

uint64_t host_read_value() {
    static int counter = 0;
    switch (counter++)
    {
        case 0:
            return 17;
        case 1:
            return 99;
        default:
            assert(0);
    }
}

void host_terminate(uint64_t value) {
    // Jump back to the setjmp point with value 1
    longjmp(halt_jmp_buf, 1);
}

int main()
{
//    EVM2::Disassembler disasm("fibonacci_loop.evm");
//    EVM2::Disassembler disasm("math.evm");
//    EVM2::Disassembler disasm("memory.evm");
//    EVM2::Disassembler disasm("xor.evm");
    EVM2::Disassembler disasm("xor-with-stack-frame.evm");
    
    // Print using the built-in print method
    disasm.print();

    // Get the instructions
    ARM64JITFrontend jit;
    jit.begin();

    std::vector<std::pair<size_t, uint32_t>> fixups;
    std::map<uint32_t, size_t> mapping;
    
    const auto& instructions = disasm.getInstructions();
    for (const EVM2::Instruction& i : instructions)
    {
        mapping.insert({i.bitOffset, jit.getCurrentIndex()});
        
        switch (i.opcode)
        {
            case EVM2::Op::LOADCONST:
                assert(i.args.size() == 2 && i.args[0].kind == EVM2::Arg::Kind::CONST && i.args[1].kind == EVM2::Arg::Kind::REG);
                jit.loadImmediate(i.args[1].reg, i.args[0].constValue);
                break;
            case EVM2::Op::CONSOLEREAD:
                assert(i.args.size() == 1 && i.args[0].kind == EVM2::Arg::Kind::REG);
                jit.hostCallWithReturnRegs((uintptr_t)host_read_value, i.args[0].reg);
                break;
            case EVM2::Op::JUMPEQ:
                assert(i.args.size() == 3 && i.args[0].kind == EVM2::Arg::Kind::ADDR && i.args[1].kind == EVM2::Arg::Kind::REG && i.args[2].kind == EVM2::Arg::Kind::REG);
                jit.compare(i.args[1].reg, i.args[2].reg);
                fixups.push_back({jit.branchIfEqual(), i.args[0].addr});
                break;
            case EVM2::Op::ADD:
                assert(i.args.size() == 3 && i.args[0].kind == EVM2::Arg::Kind::REG && i.args[1].kind == EVM2::Arg::Kind::REG && i.args[2].kind == EVM2::Arg::Kind::REG);
                jit.add(i.args[2].reg, i.args[0].reg, i.args[1].reg);
                break;
            case EVM2::Op::MOV:
                if (i.args.size() == 2 && i.args[0].kind == EVM2::Arg::Kind::REG && i.args[1].kind == EVM2::Arg::Kind::REG)
                    jit.mov(i.args[1].reg, i.args[0].reg);
                else if (i.args.size() == 2 && i.args[0].kind == EVM2::Arg::Kind::REG && i.args[1].kind == EVM2::Arg::Kind::MEM)
                    jit.storeMemoryReg(i.args[0].reg, i.args[0].addr, i.args[1].sizeBytes*8);
                else if (i.args.size() == 2 && i.args[0].kind == EVM2::Arg::Kind::MEM && i.args[1].kind == EVM2::Arg::Kind::REG)
                    jit.loadMemoryReg(i.args[1].reg, i.args[0].addr, i.args[0].sizeBytes*8);
                else
                    assert(0);
                break;
            case EVM2::Op::CONSOLEWRITE:
                assert(i.args.size() == 1);
                switch (i.args[0].kind)
                {
                    case EVM2::Arg::Kind::REG:
                        jit.hostCallWithRegs((uintptr_t)host_print_value, i.args[0].reg);
                        break;
                    case EVM2::Arg::Kind::MEM:
                        jit.hostCallWithMem((uintptr_t)host_print_value, i.args[0].reg, i.args[0].sizeBytes);
                        break;
                    default:
                        assert(0);
                }
                break;
            case EVM2::Op::SUB:
                assert(i.args.size() == 3 && i.args[0].kind == EVM2::Arg::Kind::REG && i.args[1].kind == EVM2::Arg::Kind::REG && i.args[2].kind == EVM2::Arg::Kind::REG);
                jit.sub(i.args[2].reg, i.args[0].reg, i.args[1].reg);
                break;
            case EVM2::Op::DIV:
                assert(i.args.size() == 3 && i.args[0].kind == EVM2::Arg::Kind::REG && i.args[1].kind == EVM2::Arg::Kind::REG && i.args[2].kind == EVM2::Arg::Kind::REG);
                jit.div(i.args[2].reg, i.args[0].reg, i.args[1].reg);
                break;
            case EVM2::Op::MOD:
                assert(i.args.size() == 3 && i.args[0].kind == EVM2::Arg::Kind::REG && i.args[1].kind == EVM2::Arg::Kind::REG && i.args[2].kind == EVM2::Arg::Kind::REG);
                jit.mod(i.args[2].reg, i.args[0].reg, i.args[1].reg);
                break;
            case EVM2::Op::MUL:
                assert(i.args.size() == 3 && i.args[0].kind == EVM2::Arg::Kind::REG && i.args[1].kind == EVM2::Arg::Kind::REG && i.args[2].kind == EVM2::Arg::Kind::REG);
                jit.mul(i.args[2].reg, i.args[0].reg, i.args[1].reg);
                break;
            case EVM2::Op::COMPARE:
                assert(i.args.size() == 3 && i.args[0].kind == EVM2::Arg::Kind::REG && i.args[1].kind == EVM2::Arg::Kind::REG && i.args[2].kind == EVM2::Arg::Kind::REG);
                jit.sub(i.args[2].reg, i.args[0].reg, i.args[1].reg);
                jit.signum(i.args[2].reg, i.args[2].reg);
                break;
            case EVM2::Op::JUMP:
                assert(i.args.size() == 1 && i.args[0].kind == EVM2::Arg::Kind::ADDR);
                fixups.push_back({jit.jump(), i.args[0].addr});
                break;
            case EVM2::Op::HLT:
                jit.hostCall((uintptr_t)host_terminate);
                break;
            case EVM2::Op::CALL:
                fixups.push_back({jit.call(), i.args[0].addr});
                break;
            case EVM2::Op::RET:
                jit.ret();
                break;
            default:
                assert(0);
        }
    }
    
    jit.end();

    for (const auto [instruction, target] : fixups)
    {
        auto it = mapping.find(target);
        assert(it != mapping.end());
        jit.patchBranch(instruction, it->second);
    }
    
    uint8_t memory[1024*1024];
    uint64_t registers[16];
    memset(registers, 0, sizeof(registers));
    void* func = jit.finalize();
    assert(func);
    printf("Executing...\n");
    
    // Set up the jump point
    if (setjmp(halt_jmp_buf) == 0) {
        // First time through - execute the JIT code
        ((JITFunction)func)(memory, registers, 0);
        printf("JIT code returned normally (no HALT encountered)\n");
    } else {
        // Returned via longjmp from host_terminate
        printf("Done (HALT executed).\n");
    }
    
    return 0;
}
