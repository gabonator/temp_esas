//
//  main.cpp
//  eset1
//
//  Created by Gabriel Valky on 17/11/2025.
//

#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <pthread.h>

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
  
    JitThread(std::shared_ptr<JitThread> jt, size_t entryPoint) : sharedMemory(jt->sharedMemory), jitFunc(jt->jitFunc), entry(entryPoint)
    {
        memcpy(registers, jt->registers, sizeof(registers));
    }
    
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

//uint8_t memory[1024*1024];
JITFunction func;
std::vector<uint8_t> file;

void RunTest(EVM2::Disassembler& disasm, uint8_t* memory32)
{
    static std::mutex mutexIo;
    
    // Compile the JIT code
    ARM64JITFrontend jit;
    JITInterface_t iface = {
        .print_value = [](uint64_t value) {
            std::lock_guard<std::mutex> lock(mutexIo);
            fprintf(stdout, "[Thread %lld] Value: %lld / 0x%llx\n", CThread::currentThreadId, value, value);
        },
        .read_value = []() -> uint64_t {
            std::this_thread::sleep_for(std::chrono::milliseconds(456));
            static std::atomic<int> counter{0};
            int count = counter.fetch_add(1);
            return 10; // TODO:
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
            fprintf(stderr, "[Terminate] Called from thread %lld\n", CThread::currentThreadId);
            CThread::getCurrent()->config->terminate();
        },
        .thread_create = [](uint64_t entry) -> uint64_t {
            auto currentThread = CThread::getCurrent();
            auto currentJitThread = std::dynamic_pointer_cast<JitThread>(currentThread->config);
            auto threadConfig = std::make_shared<JitThread>(currentJitThread, entry);
            auto thread = std::make_shared<CThread>(threadConfig);
            return thread->run();
        },
        .thread_join = [](uint64_t tid) {
            std::shared_ptr<CThread> thread = CThread::getById(tid);
            if (thread)
                thread->join();
        },
        .thread_sleep = [](uint64_t milliseconds) {
            if (auto current = CThread::getCurrent(); current && current->shouldStop)
            {
                current->config->terminate();
                return;
            }
// TODO: remove
//            auto current = CThread::getCurrent();
//            assert(current);
//
//            if (current->shouldStop)
//            {
//                current->config->terminate();
//                return;
//            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        },
        .thread_lock = [](uint64_t lid) {
            CThread::getCurrent()->lock(lid);
        },
        .thread_unlock = [](uint64_t lid) {
            CThread::getCurrent()->unlock(lid);
        },
        .file_read = [](uint64_t ofs, uint64_t toRead, uint64_t addr) -> uint64_t {
            std::lock_guard<std::mutex> lock(mutexIo);
            auto currentThread = CThread::getCurrent();
            auto currentJitThread = std::dynamic_pointer_cast<JitThread>(currentThread->config);
            uint8_t* mem = currentJitThread->sharedMemory;

            if (file.size() == 0)
                EVM2::Disassembler::readFile("crc.bin", file); // TODO:

            int willRead = (int)std::min(toRead, file.size() - ofs);

            for (int i=0; i<willRead; i++)
                mem[addr+i] = file[ofs+i];
            return std::max(0, willRead);
        },
        .file_write = [](uint64_t ofs, uint64_t toWrite, uint64_t addr) {
            std::lock_guard<std::mutex> lock(mutexIo);
            auto currentThread = CThread::getCurrent();
            auto currentJitThread = std::dynamic_pointer_cast<JitThread>(currentThread->config);
            uint8_t* mem = currentJitThread->sharedMemory;

            if (ofs+toWrite > file.size())
                file.resize(ofs+toWrite);
            for (int i=0; i<toWrite; i++)
                file[ofs+i] = mem[addr+i];
        }
    };
    
    func = Compile(disasm, jit, iface);
    
    // Create and configure the main thread
    auto mainThreadConfig = std::make_shared<JitThread>(memory32, func, jit.entry());
    auto mainThread = std::make_shared<CThread>(mainThreadConfig);
    mainThread->run();
    mainThread->join();  // Wait for thread to complete
}

void RunGuard(EVM2::Disassembler& disasm)
{
    pid_t pid = fork();
    if (pid == 0) {
        uint8_t* memory32 = nullptr;
        
        // Signal guards
        struct sigaction sa = {0};
        sa.sa_handler = [](int sig){
            write(2, "Caught SIGSEGV/SIGBUS exception\n", strlen("Caught SIGSEGV/SIGBUS exception\n"));
            _exit(3);
        };
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS,  &sa, NULL);
        // Memory guards
        size_t page_size = sysconf(_SC_PAGESIZE);
        size_t memory_size = (disasm.getHeader().dataSize + page_size - 1) & ~(page_size - 1);
        
        memory32 = (uint8_t*)mmap(NULL, 1ULL<<32, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        assert (mprotect(memory32, memory_size, PROT_READ | PROT_WRITE) >= 0);
        
        if (auto data = disasm.getData(); !data.empty())
            memcpy(memory32, &data[0], data.size());
        
        RunTest(disasm, memory32);
        
        munmap(memory32, 1ULL<<32);
        fflush(stdout);
        fflush(stderr);
        _exit(0);
    }
    
    int status = 0;
    waitpid(pid, &status, 0);

    // Note: doesnt work inside xcode
    assert(WIFEXITED(status));
    int code = WEXITSTATUS(status);
    if (code == 0) {
        printf("JIT exited normally.\n");
    } else if (code == 3) {
        fprintf(stderr, "Child caught memory exception and exited with code %d\n", code);
    } else {
        assert(0);
    }
}

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
//    EVM2::Disassembler disasm("multithreaded_file_write.evm");
    
//    EVM2::Disassembler disasm("gabo_boundary.evm"); // bad
//    EVM2::Disassembler disasm("gabo_label.evm");
    EVM2::Disassembler disasm("gabo_loop.evm");
//    EVM2::Disassembler disasm("gabo_stack.evm"); // catch EXC_BAD_ACCESS
//    EVM2::Disassembler disasm("gabo_thread.evm");
    disasm.print();
        
    RunGuard(disasm);
    
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
