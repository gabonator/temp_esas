#include "armjit.h"
#include <stdio.h>
#include <stdlib.h>

// Host functions
void host_print(uint64_t value, uint64_t unused1, uint64_t unused2) {
    printf("[HOST] Value: %llu / 0x%llx\n", value, value);
}

void host_print_compare(uint64_t a, uint64_t b, uint64_t result) {
    const char* cmp = (result == 0xFFFFFFFF) ? "<" : ((result == 1) ? ">" : "==");
    printf("[HOST] %llu %s %llu\n", a, b, cmp);
}

void subroutine_example(uint64_t a, uint64_t b, uint64_t c) {
    printf("[SUBROUTINE] Called with: %llu, %llu, %llu\n", a, b, c);
}

typedef void (*JITFunction)(void* buffer);

int main() {
    printf("=== ARM64 JIT Compiler - Complete Test Suite ===\n\n");
    
    // Allocate buffer
    const size_t BUFFER_SIZE = 1024 * 1024;
    void* buffer = mmap(nullptr, BUFFER_SIZE,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (buffer == MAP_FAILED) {
        perror("mmap buffer");
        return 1;
    }
    
    printf("Buffer allocated at: %p\n\n", buffer);
    
    // Store function pointers
    ((uint64_t*)buffer)[0] = (uint64_t)host_print;
    ((uint64_t*)buffer)[1] = (uint64_t)host_print_compare;
    ((uint64_t*)buffer)[2] = (uint64_t)subroutine_example;

    // ===== Test 0:  =====
    printf("--- Test 0: print ---\n");
    {
        ARM64JIT jit;
        jit.begin();

        jit.loadImmediate(0, 42);
        jit.loadImmediate(1, 100);
        jit.add(2, 0, 1);
        jit.hostCall(0, 2, 0, 0);
        
        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing...\n");
        ((JITFunction)func)(buffer);
        printf("\n");
    }

    // ===== Test 0:  =====
    printf("--- Test 0: memory ---\n");
    {
        ARM64JIT jit;
        jit.begin();

        jit.loadImmediate(0, 0); // reg, val
        jit.loadImmediate(1, 4);
        jit.loadImmediate(2, 8);
        jit.loadImmediate(3, 0x0123456789ABCDEF);
//        jit.store64ToVarAddress(0, 3); // mem[0] = r3
//        jit.load64FromVarAddress(0, 4); // r4 = mem64[0]
//        jit.load32FromVarAddress(0, 5); // r5 = mem32[0]
//        jit.load64FromVarAddress(1, 6); // r6 = mem64[1]
//        jit.load32FromVarAddress(1, 7); // r7 = mem32[1]
        
  
//        jit.hostCall(0, 3, 0, 0);
        jit.load64FromVarAddress(0, 9); jit.hostCall(0, 9, 0, 0);
//        jit.load32FromVarAddress(0, 10); jit.hostCall(0, 10, 0, 0);
//        jit.load16FromVarAddress(0, 10); jit.hostCall(0, 10, 0, 0);
//        jit.load8FromVarAddress(0, 10); jit.hostCall(0, 10, 0, 0);
//
//        jit.load64FromVarAddress(1, 10); jit.hostCall(0, 10, 0, 0);
//        jit.load32FromVarAddress(1, 10); jit.hostCall(0, 10, 0, 0);
//        jit.load16FromVarAddress(1, 10); jit.hostCall(0, 10, 0, 0);
//        jit.load8FromVarAddress(1, 10); jit.hostCall(0, 10, 0, 0);
//
//        jit.load64FromVarAddress(2, 10); jit.hostCall(0, 10, 0, 0);
//        jit.load32FromVarAddress(2, 10); jit.hostCall(0, 10, 0, 0);
//        jit.load16FromVarAddress(2, 10); jit.hostCall(0, 10, 0, 0);
//        jit.load8FromVarAddress(2, 10); jit.hostCall(0, 10, 0, 0);

        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing...\n");
        ((JITFunction)func)(buffer);
        printf("\n");
    }

    // ===== Test 1: Compare instruction =====
    printf("--- Test 1: Compare Instruction ---\n");
    {
        ARM64JIT jit;
        jit.begin();
        /*
        // Test: 10 vs 20 (should be -1, less than)
        jit.loadImmediate(0, 10);
        jit.loadImmediate(1, 20);
        jit.compare(0, 1, 2);
        jit.hostCall(1, 0, 1, 2);
        
        // Test: 30 vs 15 (should be 1, greater than)
        jit.loadImmediate(3, 30);
        jit.loadImmediate(4, 15);
        jit.compare(3, 4, 5);
        jit.hostCall(1, 3, 4, 5);
        
        // Test: 42 vs 42 (should be 0, equal)
        jit.loadImmediate(6, 42);
        jit.loadImmediate(7, 42);
        jit.compare(6, 7, 8);
        jit.hostCall(1, 6, 7, 8);
        */

        jit.loadImmediate(0, 42);
        jit.loadImmediate(1, 100);
        jit.add(2, 0, 1);
        jit.hostCall(0, 2, 0, 0);
        
        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing...\n");
        ((JITFunction)func)(buffer);
        printf("\n");
    }
    
    // ===== Test 2: Branch if equal =====
    printf("--- Test 2: Branch If Equal ---\n");
    {
        ARM64JIT jit;
        jit.begin();
        
        // var[0] = 10
        // var[1] = 10
        // if (var[0] == var[1]) jump to label
        // var[2] = 999 (should be skipped)
        // label: var[2] = 42
        
        jit.loadImmediate(0, 10);
        jit.loadImmediate(1, 10);
        
        size_t branch_pos = jit.getCurrentIndex();
        jit.branchIfEqual(0, 1, 0);  // Will patch this
        
        // This should be skipped
        jit.loadImmediate(2, 999);
        
        size_t label = jit.getCurrentIndex();
        jit.patchBranch(branch_pos, label);
        
        jit.loadImmediate(2, 42);
        jit.hostCall(0, 2, 0, 0);
        
        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing (should print 42, not 999)...\n");
        ((JITFunction)func)(buffer);
        printf("\n");
    }
    
    // ===== Test 3: Branch if not equal =====
    printf("--- Test 3: Branch If NOT Equal ---\n");
    {
        ARM64JIT jit;
        jit.begin();
        
        jit.loadImmediate(0, 10);
        jit.loadImmediate(1, 20);
        
        size_t branch_pos = jit.getCurrentIndex();
        jit.branchIfEqual(0, 1, 0);  // Will NOT branch (not equal)
        
        // This SHOULD execute
        jit.loadImmediate(2, 123);
        
        size_t label = jit.getCurrentIndex();
        jit.patchBranch(branch_pos, label);
        
        jit.hostCall(0, 2, 0, 0);
        
        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing (should print 123)...\n");
        ((JITFunction)func)(buffer);
        printf("\n");
    }
    
    // ===== Test 4: Unconditional jump =====
    printf("--- Test 4: Unconditional Jump ---\n");
    {
        ARM64JIT jit;
        jit.begin();
        
        jit.loadImmediate(0, 1);
        
        size_t jump_pos = jit.getCurrentIndex();
        jit.jump(0);  // Will patch
        
        // This should be skipped
        jit.loadImmediate(0, 999);
        jit.loadImmediate(0, 888);
        jit.loadImmediate(0, 777);
        
        size_t target = jit.getCurrentIndex();
        jit.patchBranch(jump_pos, target);
        
        jit.loadImmediate(0, 55);
        jit.hostCall(0, 0, 0, 0);
        
        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing (should print 55)...\n");
        ((JITFunction)func)(buffer);
        printf("\n");
    }
    
    // ===== Test 5: Loop with branch =====
    printf("--- Test 5: Loop (Count to 10) ---\n");
    {
        ARM64JIT jit;
        jit.begin();
        
        // var[0] = counter (0 to 10)
        // var[1] = 10 (limit)
        // var[2] = 1 (increment)
        
        jit.loadImmediate(0, 0);   // counter = 0
        jit.loadImmediate(1, 10);  // limit = 10
        jit.loadImmediate(2, 1);   // increment = 1
        
        size_t loop_start = jit.getCurrentIndex();
        
        // Print counter
        jit.hostCall(0, 0, 0, 0);
        
        // counter++
        jit.add(0, 0, 2);
        
        // Compare counter with limit
        jit.compare(0, 1, 3);
        
        // If counter < limit (result == -1), loop back
        jit.loadImmediate(4, 0xFFFFFFFF);  // -1
        size_t branch_pos = jit.getCurrentIndex();
        jit.branchIfEqual(3, 4, loop_start);
        
        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing loop...\n");
        ((JITFunction)func)(buffer);
        printf("\n");
    }
    
    // ===== Test 6: Call and Return (subroutine) =====
    printf("--- Test 6: Call and Return ---\n");
    {
        ARM64JIT jit;
        jit.begin();
        
        // Main code
        jit.loadImmediate(0, 100);
        jit.loadImmediate(1, 200);
        
        size_t call_pos = jit.getCurrentIndex();
        jit.call(0);  // Will patch to subroutine
        
        jit.loadImmediate(2, 42);
        jit.hostCall(0, 2, 0, 0);
        
        size_t main_end = jit.getCurrentIndex();
        jit.jump(0);  // Jump to actual end
        
        // Subroutine starts here
        size_t subroutine_start = jit.getCurrentIndex();
        jit.patchBranch(call_pos, subroutine_start);
        
        jit.add(2, 0, 1);  // var[2] = var[0] + var[1] = 300
        jit.hostCall(0, 2, 0, 0);  // Print 300
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
        
        printf("Executing (should print 300, then 42)...\n");
        ((JITFunction)func)(buffer);
        printf("\n");
    }
    
    // ===== Test 7: Fibonacci using branches =====
    printf("--- Test 7: Fibonacci (first 10 numbers) ---\n");
    {
        ARM64JIT jit;
        jit.begin();
        
        // var[0] = current fib number
        // var[1] = previous fib number
        // var[2] = temp
        // var[3] = counter
        // var[4] = limit (10)
        // var[5] = 1 (increment)
        
        jit.loadImmediate(0, 1);   // fib[0] = 1
        jit.loadImmediate(1, 0);   // fib[-1] = 0
        jit.loadImmediate(3, 0);   // counter = 0
        jit.loadImmediate(4, 10);  // limit = 10
        jit.loadImmediate(5, 1);   // increment = 1
        
        size_t loop_start = jit.getCurrentIndex();
        
        // Print current fib number
        jit.hostCall(0, 0, 0, 0);
        
        // temp = current + previous
        jit.add(2, 0, 1);
        
        // previous = current
        jit.loadImmediate(6, 0);  // Copy var[0] to var[1] via var[6]
        jit.add(6, 0, 6);         // var[6] = var[0] + 0
        jit.loadImmediate(1, 0);
        jit.add(1, 6, 1);
        
        // current = temp
        jit.loadImmediate(0, 0);
        jit.add(0, 2, 0);
        
        // counter++
        jit.add(3, 3, 5);
        
        // if counter < limit, continue
        jit.compare(3, 4, 7);
        jit.loadImmediate(8, 0xFFFFFFFF);
        jit.branchIfEqual(7, 8, loop_start);
        
        jit.end();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("Executing...\n");
        ((JITFunction)func)(buffer);
        printf("\n");
    }
    
    // ===== Test 8: Complex control flow =====
    printf("--- Test 8: Complex Control Flow ---\n");
    {
        ARM64JIT jit;
        jit.begin();
        
        // if (var[0] == 5) {
        //     var[1] = 100;
        // } else {
        //     var[1] = 200;
        // }
        // print var[1]
        
        jit.loadImmediate(0, 5);
        jit.loadImmediate(9, 5);
        
        size_t if_branch = jit.getCurrentIndex();
        jit.branchIfEqual(0, 9, 0);
        
        // else block
        jit.loadImmediate(1, 200);
        size_t else_jump = jit.getCurrentIndex();
        jit.jump(0);
        
        // if block
        size_t if_label = jit.getCurrentIndex();
        jit.patchBranch(if_branch, if_label);
        jit.loadImmediate(1, 100);
        
        // after if-else
        size_t after_if = jit.getCurrentIndex();
        jit.patchBranch(else_jump, after_if);
        
        jit.hostCall(0, 1, 0, 0);
        
        jit.end();
        
        printf("Code size: %zu bytes\n", jit.getCodeSize());
        jit.disassemble();
        
        void* func = jit.finalize();
        if (!func) {
            fprintf(stderr, "Failed to finalize\n");
            return 1;
        }
        
        printf("\nExecuting (should print 100 since var[0]==5)...\n");
        ((JITFunction)func)(buffer);
        printf("\n");
    }
    
    // Cleanup
    munmap(buffer, BUFFER_SIZE);
    
    printf("=== All Tests Complete ===\n");
    
    return 0;
}
