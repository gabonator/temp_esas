#include <map>

typedef void (*JITFunction)(void* memory, uint64_t* registers, size_t entry_point);

struct JITInterface_t
{
    void (*print_value)(uint64_t value);
    uint64_t (*read_value)();
    void (*terminate)();
    uint64_t (*thread_create)(uint64_t label);
    void (*thread_join)(uint64_t id);
    void (*thread_sleep)(uint64_t ms);
    void (*thread_lock)(uint64_t id);
    void (*thread_unlock)(uint64_t id);
    uint64_t (*file_read)(uint64_t ofs, uint64_t toRead, uint64_t addr);
    void (*file_write)(uint64_t ofs, uint64_t toWrite, uint64_t addr);
};

JITFunction Compile(const EVM2::Disassembler& disasm, ARM64JITFrontend& jit, JITInterface_t& iface)
{
    std::vector<std::pair<size_t, EVM2::Arg::addr_t>> fixups;
    std::map<EVM2::Arg::addr_t, size_t> mapping;
    std::map<EVM2::Arg::addr_t, char> labels;
    
    // Identify call/jump labels
    const auto& instructions = disasm.getInstructions();
    for (const EVM2::Instruction& i : instructions)
    {
        switch (i.opcode)
        {
            case EVM2::Op::JUMPEQ:
                assert(i.args.size() == 3 && i.args[0].kind == EVM2::Arg::Kind::ADDR);
                assert(labels[i.args[0].addr] != 'C');
                labels[i.args[0].addr] = 'G';
                break;
            case EVM2::Op::CALL:
                assert(i.args.size() == 1 && i.args[0].kind == EVM2::Arg::Kind::ADDR);
                assert(labels[i.args[0].addr] != 'G');
                labels[i.args[0].addr] = 'C';
                break;
            default:
                break;
        }
    }
    
    jit.begin();

    // compile
    for (const EVM2::Instruction& i : instructions)
    {
        mapping.insert({i.bitOffset, jit.getCurrentIndex()});
        
        if (auto it = labels.find(i.bitOffset); it != labels.end() && it->second == 'C')
            jit.funcPrologue();
        
        switch (i.opcode)
        {
            case EVM2::Op::LOADCONST:
                assert(i.args.size() == 2 && i.args[0].kind == EVM2::Arg::Kind::CONST && i.args[1].kind == EVM2::Arg::Kind::REG);
                jit.loadImmediate(i.args[1].reg, i.args[0].constValue);
                break;
            case EVM2::Op::CONSOLEREAD:
                assert(i.args.size() == 1 && i.args[0].kind == EVM2::Arg::Kind::REG);
                jit.hostCallWithOps((uintptr_t)iface.read_value, i.args[0], {});
                break;
            case EVM2::Op::JUMPEQ:
                assert(i.args.size() == 3 && i.args[0].kind == EVM2::Arg::Kind::ADDR);
                jit.compare(i.args[1], i.args[2]);
                fixups.push_back({jit.branchIfEqual(), i.args[0].addr});
                break;
            case EVM2::Op::ADD:
                assert(i.args.size() == 3);
                jit.add(i.args[2], i.args[0], i.args[1]);
                break;
            case EVM2::Op::MOV:
                assert(i.args.size() == 2);
                jit.mov(i.args[1], i.args[0]);
                break;
            case EVM2::Op::CONSOLEWRITE:
                assert(i.args.size() == 1);
                jit.hostCallWithOps((uintptr_t)iface.print_value, {}, i.args[0]);
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
                jit.hostCallWithOps((uintptr_t)iface.terminate, {}, {});
                break;
            case EVM2::Op::CALL:
                assert(i.args.size() == 1 && i.args[0].kind == EVM2::Arg::Kind::ADDR);
                fixups.push_back({jit.call(), i.args[0].addr});
                break;
            case EVM2::Op::RET:
                jit.funcEpilogue();
                jit.ret();
                break;
            case EVM2::Op::CREATETHREAD:
                assert(i.args.size() == 2 && i.args[0].kind == EVM2::Arg::Kind::ADDR);
                fixups.push_back({jit.hostCallWithOps((uintptr_t)iface.thread_create, i.args[1], i.args[0]), i.args[0].addr});
                break;
            case EVM2::Op::JOINTHREAD:
                assert(i.args.size() == 1);
                jit.hostCallWithOps((uintptr_t)iface.thread_join, {}, i.args[0]);
                break;
            case EVM2::Op::LOCK:
                assert(i.args.size() == 1);
                jit.hostCallWithOps((uintptr_t)iface.thread_lock, {}, i.args[0]);
                break;
            case EVM2::Op::UNLOCK:
                assert(i.args.size() == 1);
                jit.hostCallWithOps((uintptr_t)iface.thread_unlock, {}, i.args[0]);
                break;
            case EVM2::Op::SLEEP:
                assert(i.args.size() == 1);
                jit.hostCallWithOps((uintptr_t)iface.thread_sleep, {}, i.args[0]);
                break;
            case EVM2::Op::READ:
                assert(i.args.size() == 4);
                jit.hostCallWithOps((uintptr_t)iface.file_read, i.args[3], i.args[0], i.args[1], i.args[2]);
                break;
            case EVM2::Op::WRITE:
                assert(i.args.size() == 3);
                jit.hostCallWithOps((uintptr_t)iface.file_write, {}, i.args[0], i.args[1], i.args[2]);
                break;
            default:
                assert(0);
        }
        jit.nop();
    }
    jit.end();
    
    // patch branches and immediates
    for (const auto [instruction, target] : fixups)
    {
        auto it = mapping.find(target);
        assert(it != mapping.end());
        jit.patchBranchOrImm(instruction, it->second);
    }
    
    // finalize
    void* func = jit.finalize();
    assert(func);

    return (JITFunction)func;
}
