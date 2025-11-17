//
//  main.cpp
//  eset1
//
//  Created by Gabriel Valky on 17/11/2025.
//


#include "evm2.h"
#include "jit_arm64_fe.h"
#include <map>

typedef void (*JITFunction)(void* memory, uint64_t* registers, int entry_point);

void host_print_value(uint64_t value) {
    printf("[HOST] Value: %lld / 0x%llx\n", value, value);
}

uint64_t host_read_value() {
    return 17;
}

int main()
{
    EVM2::Disassembler disasm("fibonacci_loop.evm");
    //EVM2::Disassembler disasm("math.evm");
//    EVM2::Disassembler disasm("memory.evm");

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
                {
                    jit.hostCallWithReturnArgs((uintptr_t)host_read_value, i.args[0].reg);
                }
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
                {
                    jit.mov(i.args[1].reg, i.args[0].reg);
                    break;
                }
//                if (i.args.size() == 2 && i.args[0].kind == EVM2::Arg::Kind::REG && i.args[1].kind == EVM2::Arg::Kind::MEM)
//                {
//                    jit.storeMemory(i.args[0].reg, i.args[0].addr, i.args[1].sizeBytes);
//                    break;
//                }
//                if (i.args.size() == 2 && i.args[0].kind == EVM2::Arg::Kind::MEM && i.args[1].kind == EVM2::Arg::Kind::REG)
//                {
//                    jit.loadMemory(i.args[0].reg, i.args[0].addr, i.args[1].sizeBytes);
//                    break;
//                }
                assert(0);
                break;
            case EVM2::Op::CONSOLEWRITE:
                assert(i.args.size() == 1 && i.args[0].kind == EVM2::Arg::Kind::REG);
                jit.hostCallWithArgs((uintptr_t)host_print_value, i.args[0].reg, 0, 0);
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
                jit.end();
                break;
            default:
                assert(0);
        }
    }
    
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
    ((JITFunction)func)(memory, registers, 0);
    printf("\n");
}
