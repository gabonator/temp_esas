#include "jit_arm64_fe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ===== Host Functions =====

/**
 * Host function that receives all 16 registers
 * This demonstrates the new calling convention
 */
void host_print_registers(uint64_t* regs) {
    printf("[HOST] Registers dump:\n");
    for (int i = 0; i < 16; i++) {
        printf("  r%d = %llu (0x%llx)\n", i, regs[i], regs[i]);
    }
}

/**
 * Host function with explicit arguments
 */
void host_print_value(uint64_t value) {
    printf("[HOST] Value: %lld / 0x%llx\n", value, value);
}

uint64_t host_get_value() {
    printf("[HOST] get value returns 7\n");
    return 7;
}

/**
 * Host function that receives multiple register values
 */
void host_print_three(uint64_t a, uint64_t b, uint64_t c) {
    printf("[HOST] Values: a=%llu, b=%llu, c=%llu\n", a, b, c);
}

/**
 * Host function to demonstrate computation
 */
void host_print_sum(uint64_t a, uint64_t b, uint64_t sum) {
    printf("[HOST] %llu + %llu = %llu\n", a, b, sum);
}

// Type for JIT-generated function
typedef void (*JITFunction)(void* memory, uint64_t* registers, int entry_point);

int main() {
    printf("=== ARM64 JIT Compiler - Frontend/Backend Demo ===\n\n");
    
    // Allocate memory buffer (simulating machine memory)
    const size_t MEMORY_SIZE = 64 * 1024;  // 64KB
    void* memory = malloc(MEMORY_SIZE);
    if (!memory) {
        perror("malloc memory");
        return 1;
    }
    memset(memory, 0, MEMORY_SIZE);
    
    // Initialize some test data in memory
    ((uint64_t*)memory)[0] = 0x0123456789ABCDEF;
    ((uint32_t*)memory)[2] = 0xDEADBEEF;
    ((uint16_t*)memory)[6] = 0x1234;
    ((uint8_t*)memory)[14] = 0x42;
    
    // Allocate register array (16 uint64_t registers)
    uint64_t registers[16];
    memset(registers, 0, sizeof(registers));
    
    printf("Memory allocated: %p (%zu bytes)\n", memory, MEMORY_SIZE);
    printf("Registers array: %p (16 x uint64_t)\n\n", registers);
    
    // ===== Test 1: Basic arithmetic =====
    printf("--- Test 1: Basic Arithmetic ---\n");
    {
        ARM64JITFrontend jit;
        jit.begin();
        
        // r0 = 42
        // r1 = 100
        // r2 = r0 + r1
        jit.loadImmediate(0, 42);
        jit.loadImmediate(1, 100);
        jit.add(2, 0, 1);
        
        // Call host function to print r0, r1, r2
        jit.hostCallWithArgs((uint64_t)host_print_value, 2);
        
        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing...\n");
        ((JITFunction)func)(memory, registers, 0);
        printf("After execution: r2 = %llu\n\n", registers[2]);
    }
    
    // ===== Test 2: Memory operations =====
    printf("--- Test 2: Memory Operations ---\n");
    {
        ARM64JITFrontend jit;
        jit.begin();
        
        // Load 64-bit value from memory[0]
        jit.loadMemory64(0, 0);
        
        // Load 32-bit value from memory[8]
        jit.loadMemory32(1, 8);
        
        // Load 16-bit value from memory[12]
        jit.loadMemory16(2, 12);
        
        // Load 8-bit value from memory[14]
        jit.loadMemory8(3, 14);
        
        // Print all values
        jit.hostCallWithArgs((uint64_t)host_print_value, 0);
        jit.hostCallWithArgs((uint64_t)host_print_value, 1);
        jit.hostCallWithArgs((uint64_t)host_print_value, 2);
        jit.hostCallWithArgs((uint64_t)host_print_value, 3);
        
        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing...\n");
        printf("Expected values:\n");
        printf("  64-bit from offset 0: 0x0123456789ABCDEF\n");
        printf("  32-bit from offset 8: 0xDEADBEEF\n");
        printf("  16-bit from offset 12: 0x1234\n");
        printf("  8-bit from offset 14: 0x42\n");
        printf("Actual values:\n");
        ((JITFunction)func)(memory, registers, 0);
        printf("\n");
    }
    
    
    // ===== Test 4: Branches and control flow =====
    printf("--- Test 4: Branches and Control Flow ---\n");
    {
        ARM64JITFrontend jit;
        jit.begin();
        
        // r0 = 10
        // r1 = 10
        // if (r0 == r1) jump to equal_label
        // r2 = 999 (skipped)
        // jump to end
        // equal_label:
        // r2 = 42
        // end:
        
        jit.loadImmediate(0, 10);
        jit.loadImmediate(1, 10);
        jit.compare(0, 1);
        
        size_t branch_pos = jit.branchIfEqual();
        // r0 != r1
        jit.loadImmediate(2, 999);
        size_t skip_jump = jit.jump();
        
        // r0 == r1
        size_t equal_label = jit.loadImmediate(2, 42);
        jit.patchBranch(branch_pos, equal_label);

        // End
        size_t end_label = jit.hostCallWithArgs((uint64_t)host_print_value, 2);
        jit.patchBranch(skip_jump, end_label);
        
        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing (should print 42, not 999)...\n");
        ((JITFunction)func)(memory, registers, 0);
        printf("\n");
    }
    
    // ===== Test 5: Loop (count from 0 to 9) =====
    printf("--- Test 5: Loop (Count 0-9) ---\n");
    {
        ARM64JITFrontend jit;
        jit.begin();
        
        // r0 = counter (0 to 9)
        // r1 = 10 (limit)
        // loop:
        //   print r0
        //   r0 = r0 + 1
        //   if r0 < r1 goto loop
        
        jit.loadImmediate(0, 0);   // counter = 0
        jit.loadImmediate(1, 10);  // limit = 10
        
        size_t loop_start = jit.getCurrentIndex();
        
        // Print counter
        jit.hostCallWithArgs((uint64_t)host_print_value, 0, 0, 0);
        
        // counter++
        jit.addImmediate(0, 0, 1);
        
        // Compare counter with limit
        jit.compare(0, 1);
        
        // If counter < limit, loop back
        jit.branchIfLessThan(loop_start);
        
        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing...\n");
        ((JITFunction)func)(memory, registers, 0);
        printf("\n");
    }

    // ===== Test 6: Subroutine call =====
    printf("--- Test 6: Subroutine Call and Return ---\n");
    {
        ARM64JITFrontend jit;
        jit.begin();
        
        // Main code
        jit.loadImmediate(0, 5);
        jit.loadImmediate(1, 7);
        
        size_t call_pos = jit.getCurrentIndex();
        jit.call(0);  // Will patch to subroutine
        
        // After subroutine returns, print result
        jit.hostCallWithArgs((uint64_t)host_print_value, 2, 0, 0);
        
        size_t main_end = jit.getCurrentIndex();
        jit.jump(0);  // Jump to actual end
        
        // Subroutine: adds r0 and r1, stores in r2
        size_t subroutine_start = jit.getCurrentIndex();
        jit.patchBranch(call_pos, subroutine_start);
        
        jit.add(2, 0, 1);  // r2 = r0 + r1
        jit.ret();
        
        // Actual end
        size_t actual_end = jit.getCurrentIndex();
        jit.patchBranch(main_end, actual_end);
        
        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing (should compute 5 + 7 = 12)...\n");
        ((JITFunction)func)(memory, registers, 0);
        printf("\n");
    }
  
    printf("--- Test: Math ---\n");
    {
        ARM64JITFrontend jit;
        jit.begin();
        
        jit.loadImmediate(0, 0x100);
        jit.loadImmediate(1, 0x18);
        jit.add(2, 0, 1);
        jit.hostCallWithArgs((uint64_t)host_print_value, 2);
        jit.sub(2, 0, 1);
        jit.hostCallWithArgs((uint64_t)host_print_value, 2);
        jit.div(2, 0, 1);
        jit.hostCallWithArgs((uint64_t)host_print_value, 2);
        jit.mod(2, 0, 1);
        jit.hostCallWithArgs((uint64_t)host_print_value, 2);
        jit.mul(2, 0, 1);
        jit.hostCallWithArgs((uint64_t)host_print_value, 2);
        
        jit.sub(2, 0, 1);
        jit.signum(2, 2);
        jit.hostCallWithArgs((uint64_t)host_print_value, 2);
        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing...\n");
        ((JITFunction)func)(memory, registers, 0);
    }

    // ===== Test 7: Fibonacci sequence =====
    printf("--- Test 7: Fibonacci Sequence (first 10 numbers) ---\n");
    {
        ARM64JITFrontend jit;
        jit.begin();
        
        // r0 = current fib
        // r1 = previous fib
        // r2 = temp
        // r3 = counter
        // r4 = limit
        
        jit.loadImmediate(0, 1);   // fib[0] = 1
        jit.loadImmediate(1, 0);   // fib[-1] = 0
        jit.loadImmediate(3, 0);   // counter = 0
        jit.loadImmediate(4, 10);  // limit = 10
        
        size_t loop_start = jit.getCurrentIndex();
        
        // Print current fib number
        jit.hostCallWithArgs((uint64_t)host_print_value, 0, 0, 0);
        
        // temp = current + previous
        jit.add(2, 0, 1);
        
        // previous = current
        jit.loadImmediate(5, 0);
        jit.add(5, 0, 5);  // r5 = r0
        jit.loadImmediate(1, 0);
        jit.add(1, 5, 1);  // r1 = r5
        
        // current = temp
        jit.loadImmediate(0, 0);
        jit.add(0, 2, 0);  // r0 = r2
        
        // counter++
        jit.addImmediate(3, 3, 1);
        
        // if counter < limit, continue
        jit.compare(3, 4);
        jit.branchIfLessThan(loop_start);
        
        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing...\n");
        ((JITFunction)func)(memory, registers, 0);
        printf("\n");
    }
    
    printf("--- Test 7: Fibonacci 2 ---\n");
    {
        memset(registers, sizeof(registers), 0); // TODO: zero cleared
        ARM64JITFrontend jit;
        jit.begin();
        jit.loadImmediate(1, 0);
        jit.loadImmediate(2, 1);
        jit.loadImmediate(14, 1);
        jit.hostCallWithReturnArgs((uintptr_t)host_get_value, 3);
        
        size_t loc_loop = jit.compare(3, 15);
        size_t jump_end = jit.branchIfEqual();
        jit.add(4, 1, 2);
        jit.mov(1, 2);
        jit.mov(2, 4);
        jit.hostCallWithArgs((uint64_t)host_print_value, 1, 0, 0);
        jit.sub(3, 3, 14);
        jit.jump(loc_loop);

        size_t loc_end = jit.end();
        jit.patchBranch(jump_end, loc_end);
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing...\n");
        ((JITFunction)func)(memory, registers, 0);
        printf("\n");
    }

    
    // ===== Test 8: Writing to memory =====
    printf("--- Test 8: Memory Write Operations ---\n");
    {
        ARM64JITFrontend jit;
        jit.begin();
        
        // Clear some memory
        uint64_t write_offset = 1024;
        
        // Write 64-bit value
        jit.loadImmediate(0, 0xFEEDFACECAFEBEEF);
        jit.storeMemory64(0, write_offset);
        
        // Write 32-bit value
        jit.loadImmediate(1, 0x12345678);
        jit.storeMemory32(1, write_offset + 8);
        
        // Read them back
        jit.loadMemory64(2, write_offset);
        jit.loadMemory32(3, write_offset + 8);
        
        // Print
        jit.hostCallWithArgs((uint64_t)host_print_value, 2, 0, 0);
        jit.hostCallWithArgs((uint64_t)host_print_value, 3, 0, 0);
        
        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing...\n");
        ((JITFunction)func)(memory, registers, 0);
        
        // Verify directly
        printf("Verification from C:\n");
        printf("  64-bit at offset 1024: 0x%llx\n", ((uint64_t*)((char*)memory + write_offset))[0]);
        printf("  32-bit at offset 1032: 0x%x\n", ((uint32_t*)((char*)memory + write_offset + 8))[0]);
        printf("\n");
    }
    
    // ===== Test 9: Register-indirect memory writes =====
    printf("--- Test 9: Register-Indirect Memory Writes ---\n");
    {
        ARM64JITFrontend jit;
        jit.begin();
        
        // r0 = address (2048)
        // r1 = value (0xDEADC0DE)
        // memory[r0] = r1
        
        jit.loadImmediate(0, 2048);
        jit.loadImmediate(1, 0xDEADC0DE);
        jit.storeMemoryReg32(0, 1);
        
        // Read it back
        jit.loadMemoryReg32(2, 0);
        jit.hostCallWithArgs((uint64_t)host_print_value, 2, 0, 0);
        
        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing...\n");
        ((JITFunction)func)(memory, registers, 0);
        printf("\n");
    }
    
    // ===== Test 10: Host function with all registers =====
    printf("--- Test 10: Host Function Receives All Registers ---\n");
    {
        ARM64JITFrontend jit;
        jit.begin();
        
        // Initialize all 16 registers with different values
        for (int i = 0; i < 16; i++) {
            jit.loadImmediate(i, i * 111);
        }
        
        // Call host function that receives pointer to all registers
        jit.hostCall((uint64_t)host_print_registers);
        
        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing...\n");
        ((JITFunction)func)(memory, registers, 0);
        printf("\n");
    }
    
    // ===== Test 11: Complex computation =====
    printf("--- Test 11: Complex Computation (a*2 + b*3) ---\n");
    {
        ARM64JITFrontend jit;
        jit.begin();
        
        // r0 = 10 (a)
        // r1 = 20 (b)
        // r2 = a * 2 = a + a
        // r3 = b * 3 = b + b + b
        // r4 = r2 + r3
        
        jit.loadImmediate(0, 10);
        jit.loadImmediate(1, 20);
        
        // r2 = a * 2
        jit.add(2, 0, 0);
        
        // r3 = b * 3
        jit.add(3, 1, 1);  // r3 = b + b
        jit.add(3, 3, 1);  // r3 = r3 + b
        
        // r4 = r2 + r3
        jit.add(4, 2, 3);
        
        // Print intermediate and final results
        jit.hostCallWithArgs((uint64_t)host_print_value, 2, 0, 0);  // a*2
        jit.hostCallWithArgs((uint64_t)host_print_value, 3, 0, 0);  // b*3
        jit.hostCallWithArgs((uint64_t)host_print_value, 4, 0, 0);  // total
        
        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing (10*2 + 20*3 = 20 + 60 = 80)...\n");
        ((JITFunction)func)(memory, registers, 0);
        printf("\n");
    }
    
    // Cleanup
    free(memory);
    
    printf("=== All Tests Complete ===\n");
    
    return 0;
}
