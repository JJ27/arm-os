#include "rpi.h"

#include "my-fat32-driver.h"
#include "my-elf-loader.h"

// Load the ELF file from the SD card to memory, starting at address `base`
void load_elf(char *filename, char *base) {
    int size = my_fat32_read(filename, base);
    if (size < 0)
        panic("[MY-ELF] Couldn't read ELF file from the FAT32 filesystem\n");
    else
        printk("[MY-ELF] ELF file loaded into memory (%x - %x)\n", base, base + size);
}

// Verify the ELF header. We'll do only a few checks as an exercise.
// Refer to 1-3 of ELF.pdf for the ELF header format
void verify_elf(elf32_header *e_header) {
    // 1. Verify the ELF file magic number, which is 0x7f, 'E', 'L', 'F', in that order
    if (e_header->e_ident[0] != 0x7f || 
        e_header->e_ident[1] != 'E' || 
        e_header->e_ident[2] != 'L' || 
        e_header->e_ident[3] != 'F')
        panic("[MY-ELF] Not an ELF file!\n");
    else
        printk("[MY-ELF] ELF file magic number verified\n");

    // 2. Verify that the ELF file is either executable or shared object
    // ET_EXEC = 2, ET_DYN = 3
    if (e_header->e_type != 2 && e_header->e_type != 3)
        panic("[MY-ELF] Not an executable or shared object ELF file!\n");
    else
        printk("[MY-ELF] ELF file type verified\n");

    // 3. Verify that the ELF file is for 32-bit architecture
    // ELFCLASS32 = 1
    if (e_header->e_ident[4] != 1)
        panic("[MY-ELF] Not a 32-bit ELF file!\n");
    else
        printk("[MY-ELF] ELF file architecture verified\n");
}

// Zero-initialize the .bss section
// ** cstart.c no longer does this! This would be a more "legal" way of initializing the bss section **
// In order to do this, we need to:
//   - Find out where the section header table starts from the ELF header
//   - Iterate through the section header table entries until we find the .bss section
//      - Thankfully, the .bss section is the only section that has sh_type == SHT_NOBITS
//        for our case. So we can rely on this for now.
//   - Zero-initialize the .bss section
// Refer to 1-4, 1-9, 1-10, and 1-13
void bss_zero_init(elf32_header *e_header) {
    // Get the section header table
    elf32_sheader *e_sheaders = (elf32_sheader *)((char *)e_header + e_header->e_shoff);
    
    // Iterate through section headers to find .bss section
    for (int i = 0; i < e_header->e_shnum; i++) {
        // .bss section has type SHT_NOBITS
        if (e_sheaders[i].sh_type == SHT_NOBITS) {
            char *bss_start = (char *)e_sheaders[i].sh_addr;
            char *bss_end = bss_start + e_sheaders[i].sh_size;
            memset(bss_start, 0, bss_end - bss_start);
            printk("[MY-ELF] BSS section zero-initialized (%x - %x)\n", bss_start, bss_end);
            return;
        }
    }
}

// Jump to the ELF file's entry point.
void jump_to_elf_entry(elf32_header *e_header) {
    // Find the entry point of the ELF file
    uint32_t entry_point = e_header->e_entry;
    printk("[MY-ELF] Entry point: %x\n", entry_point);

    // Branch to the entry point
    printk("[MY-ELF] Branching to the entry point\n");
    void (*entry)() = (void (*)())entry_point;
    entry();
    panic("[MY-ELF] Shouldn't reach here!\n");
}
