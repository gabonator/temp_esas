//
//  main.cpp
//  eset1
//
//  Created by Gabriel Valky on 17/11/2025.
//

#include "evm2.h"
#include "jit_arm64_fe.h"
#include "compile.h"
#include "thread.h"

class JitThread : public ThreadBase {
public:
    uint64_t registers[16] = {0};
    uint8_t* sharedMemory = nullptr;
    JITFunction jitFunc = nullptr;
    size_t entry = 0;
    jmp_buf halt_jmp_buf;
    
    JitThread() = default;
    JitThread(uint8_t* mem, JITFunction func, size_t entryPoint) 
        : sharedMemory(mem), jitFunc(func), entry(entryPoint) {}
  
    int run(uint64_t tid)
    {
        if (setjmp(halt_jmp_buf) == 0) {
            jitFunc(sharedMemory, registers, entry);
            return 0;
        } else
        {
            // Halted
            return 1;
        }
    }

    void terminate()
    {
        longjmp(halt_jmp_buf, 1);
    }
};

uint8_t memory[1024*1024];
JITFunction func;
std::vector<uint8_t> file;

int main()
{
//    EVM2::Disassembler disasm("fibonacci_loop.evm");
//    EVM2::Disassembler disasm("math.evm");
//    EVM2::Disassembler disasm("memory.evm");
//    EVM2::Disassembler disasm("xor.evm");
//    EVM2::Disassembler disasm("xor-with-stack-frame.evm");
//    EVM2::Disassembler disasm("threadingBase.evm");
    //EVM2::Disassembler disasm("lock.evm");
//    EVM2::Disassembler disasm("pseudorandom.evm");
//    EVM2::Disassembler disasm("philosophers.evm"); // bad
//    EVM2::Disassembler disasm("crc.evm");
    EVM2::Disassembler disasm("multithreaded_file_write.evm");
    disasm.print();
    memset(memory, 0, disasm.getHeader().dataSize);
    memcpy(memory, &disasm.getData()[0], disasm.getData().size());

    
    static std::mutex mutexIo;
    // Compile the JIT code
    ARM64JITFrontend jit;
    JITInterface_t iface = {
        .print_value = [](uint64_t value) {
            printf("[Thread %lld] Value: %lld / 0x%llx\n", CThread::currentThreadId, value, value);
        },
        .read_value = []() -> uint64_t {
            std::this_thread::sleep_for(std::chrono::milliseconds(456));
            static std::atomic<int> counter{0};
            int count = counter.fetch_add(1);
            return 3;
            switch (count)
            {
                case 0:
                    return 17;
                case 1:
                    return 99;
                default:
                    return 0;
            }
        },
        .terminate = []() {
            CThread::getCurrent()->config->terminate();
        },
        .thread_create = [](uint64_t entry) -> uint64_t {
            auto threadConfig = std::make_shared<JitThread>(memory, func, entry);
            // copy registers from parent at the time of creation
            auto parentJitThread = std::dynamic_pointer_cast<JitThread>(CThread::getCurrent()->config);
            if (parentJitThread) {
                memcpy(threadConfig->registers, parentJitThread->registers, sizeof(threadConfig->registers));
            }
            
            auto thread = std::make_shared<CThread>(threadConfig);
            return thread->run();
        },
        .thread_join = [](uint64_t tid) {
            std::shared_ptr<CThread> thread = CThread::getById(tid);
            if (thread)
                thread->join();
        },
        .thread_sleep = [](uint64_t milliseconds) {
            if (CThread::getCurrent()->shouldStop)
            {
                printf("Terminating\n");
                CThread::getCurrent()->config->terminate();
            } else
                std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        },
        .thread_lock = [](uint64_t lid) {
            CThread::getCurrent()->lock(lid);
        },
        .thread_unlock = [](uint64_t lid) {
            CThread::getCurrent()->unlock(lid);
        },
        .file_read = [](uint64_t ofs, uint64_t toRead, uint64_t addr) -> uint64_t {
            if (file.size() == 0)
            {
                std::lock_guard<std::mutex> lock(mutexIo);
                EVM2::Disassembler::readFile("crc.bin", file);
            }
            int willRead = std::min(toRead, file.size() - ofs);
            for (int i=0; i<willRead; i++)
                memory[addr+i] = file[ofs+i];
            return std::max(0, willRead);
        },
        .file_write = [](uint64_t ofs, uint64_t toWrite, uint64_t addr) {
            std::lock_guard<std::mutex> lock(mutexIo);
            if (ofs+toWrite > file.size())
                file.resize(ofs+toWrite);
            for (int i=0; i<toWrite; i++)
                file[ofs+i] = memory[addr+i];
        }
    };
    
    func = Compile(disasm, jit, iface);
    
    // Create and configure the main thread
    auto mainThreadConfig = std::make_shared<JitThread>(memory, func, jit.entry());
    
    auto mainThread = std::make_shared<CThread>(mainThreadConfig);
    mainThread->run();
    mainThread->join();  // Wait for thread to complete
        
    if (file.size() != 0 && file.size() != 16)
    {
        printf("File contents:\n");
        for (int i=0; i<file.size(); i++)
        {
            printf("%02x ", file[i]);
            if (i%16==15)
                printf("\n");
        }
        printf("\n");
    }
    return 0;
}
