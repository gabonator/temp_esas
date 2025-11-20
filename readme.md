# Assignment

- Task was to run EVM2 code, the naive way could be interpreting the instruction one by one, but that would be not very efficient nor interesting for implementation
- I decided to write a JIT compiler which turns the EVM2 code into arm64 machine code to be run on Mac ARM, I guess there are plenty of JIT compilers for x86, but out of curiosty I wanted to do my very simple implementation compatible with Apple's M1 processor
- There was a huge security mishap with E*** virtual machine which was used by some viruses to bypass security and elevate privileges, so I tried to focus on security here
- Memory size is limited by 32 bits, even it is not said explicitly it can be understood from the EVM2 file header
- All registers are 64 bit long, we cannot identify which registers hold memory pointers. So it is probably impossible to relocate the program to some "work" area. Unfortunately the linear space begins at address 0, so I decided that all memory operations will be done as `[memory_base_ptr + reg_value]`, where the memory_base_ptr points to a huge 8GB chunk of memory. Only the initial part aligned to page size is allowed to access. Any read/write behind the allocated memory causes the JIT to terminate
- EVM uses 16 registers, but looking at the ABI I couldn't map them directly to ARM's registers. So they are placed in separate buffer.
- JIT program takes three arguments: memory_base_ptr, registers_base_ptr (uint64_t[16]) and entry point. Entry point defaults to 11 - it is the first instruction after program prologue. In case it is firing up a new thread, the entry point is set to the label where the worker code begins
- Stack is limited to few kilobytes
- Summary of safety features of this JIT:
  - all memory operations done as `LDR   Rt, [Rn, Rm]` where the `Rm` register is treated as zero extended 32-bit register, so it is impossible for the JIT code to access anything beyond 8GB
  - buffer holding registers is indexed directly - so it is impossible to access data outside the 0..15
  - stack is not under our control that easily, but we run everything inside thread and set the limit by `pthread_attr_setstacksize` to very low value. So even excess stack use should be covered. Note that we use stack only for call return addresses
  - program is terminated after few seconds - after 3 seconds it configures the sleep command to terminate execution. But if the program is stuck completely, it will be forcefully terminated after 5 seconds
  - considering these points it seems very complicated or impossible for the emulated program to escape and took control over the JIT host
- Program is written focusing on readibility and should be built in debug configuration, there could be done some improvements:
  - assertions - for release builds most of them should terminate program
  - EVM2 disassembler stores everything in std::vector. It could stream the instructions into JIT compiler
  - timeout is per thread - it should be global for whole app
  - excessive thread generation is not limited in any way - we have test which generate 1000 of them, so it's hard to tell what is the sane limit for that
- Project structure:
  - `jit_arm64_be.h` - used for generating machine code instructions
  - `jit_arm64_fe.h` - higher abstraction for building the JIT code
  - `compile.h` - iterates through EVM2 instructions and generates JIT stream
  - `thread.h` - C++ class for simple creating and managing of threads
  - `evm2.h` - disassembler completely written by Claude AI based on the assignment PDF and some more refining queries
  - `main.cpp` - main app
  - `res/` - resources with EASM files, input values and example output
    - `gabo_boundary.easm` - allocates 0x10000 bytes, but tries to read data at offset 0x10000
    - `gabo_label.easm` - current implementation disallows having a label that is target of jump&branch at the same time, this verifies that behavior
    - `gabo_loop.easm` - infinite loop - for testing the hard timeout
    - `gabo_stack.easm` - excess stack use test
    - `gabo_thread.easm` - check if child thread has correct copy of registers and they do not interfere with parent

- Building&Testing:
  - `cd res`
  - `g++ -std=c++23 ../main.cpp -o test.elf`
  - `./test.sh`

  