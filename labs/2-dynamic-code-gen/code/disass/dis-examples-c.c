// used to hold example C code that you want to see asm for.
#include "rpi.h"

uint32_t mov_example(uint32_t a1, uint32_t a2, uint32_t a3) { return a2+a3; }

void call_hello(void) {
    printk("hello world\n");
}

typedef struct {
    int id;
    char name[32];
    int age;
} Person;

// Sample data
Person people[] = {
    {1, "Alice", 25},
    {2, "Bob", 30},
    {3, "Charlie", 35},
    {4, "David", 40},
    {5, "Eve", 45}
};
const int NUM_PEOPLE = 5;

// Function pointer type for compiled queries
typedef int (*QueryFunc)(Person*);

// Interpreted query execution
int interpret_query(Person* table, int size, int target_age) {
    int count = 0;
    for (int i = 0; i < size; i++) {
        if (table[i].age == target_age) {
            count++;
        }
    }
    return count;
}

void notmain(void) {
    
}
