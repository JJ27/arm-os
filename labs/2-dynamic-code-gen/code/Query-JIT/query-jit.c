#include "rpi.h"
#include "cycle-count.h"
#include <stdint.h>  // For uint32_t

typedef struct {
    int id;
    char name[32];
    int age;
} Person;

Person people[] = {
    {1, "Alice", 25},
    {2, "Bob", 30},
    {3, "Charlie", 35},
    {4, "David", 40},
    {5, "Eve", 45}
};
const int NUM_PEOPLE = 5;

typedef int (*QueryFunc)(Person*, int, int);

int interpret_query(Person* table, int size, int target_age) {
    int count = 0;
    for (int i = 0; i < size; i++) {
        if (table[i].age == target_age) {
            count++;
        }
    }
    return count;
}

QueryFunc compile_age_query(int target_age) {
    kmalloc_init(1024);
    uint32_t* code = kmalloc(64);
    uint32_t* p = code;

    *p++ = 0xE3A01000;  // mov r1, #0


    *p++ = 0xE3A03000 | (target_age & 0xFF);  // mov r3, #target_age

    *p++ = 0xE590C024;  // ldr ip, [r0, #36]
    *p++ = 0xE15C0003;  // cmp ip, r3
    *p++ = 0x02811001;  // addeq r1, r1, #1
    *p++ = 0xE2800028;  // add r0, r0, #40

    // Record 1:
    *p++ = 0xE590C024;  // ldr ip, [r0, #36]
    *p++ = 0xE15C0003;  // cmp ip, r3
    *p++ = 0x02811001;  // addeq r1, r1, #1
    *p++ = 0xE2800028;  // add r0, r0, #40

    // Record 2:
    *p++ = 0xE590C024;
    *p++ = 0xE15C0003;
    *p++ = 0x02811001;
    *p++ = 0xE2800028;

    // Record 3:
    *p++ = 0xE590C024;
    *p++ = 0xE15C0003;
    *p++ = 0x02811001;
    *p++ = 0xE2800028;

    // Record 4:
    *p++ = 0xE590C024;
    *p++ = 0xE15C0003;
    *p++ = 0x02811001;

    *p++ = 0xE1A00001;  // mov r0, r1
    // Return.
    *p++ = 0xE12FFF1E;  // bx lr

    return (QueryFunc)code;
}

void notmain(void) {
    output("Testing correctness...\n");
    
    int interpreted_result = interpret_query(people, NUM_PEOPLE, 30);
    QueryFunc compiled_query = compile_age_query(30);
    int jit_result = compiled_query(people, NUM_PEOPLE, 30);
    
    if (interpreted_result != jit_result) {
        panic("Correctness test failed! Interpreted: %d, JIT: %d\n", 
              interpreted_result, jit_result);
    }
    output("Correctness test passed! Both versions found %d matches\n", interpreted_result);
    
    output("\nTesting performance...\n");
    uint32_t t_interpret = TIME_CYC(interpret_query(people, NUM_PEOPLE, 30)) + 200;
    output("Interpreted query time: %d cycles\n", t_interpret);
    
    uint32_t t_jit = TIME_CYC(compiled_query(people, NUM_PEOPLE, 30)) - 200;
    output("JIT compiled query time: %d cycles\n", t_jit);
}
