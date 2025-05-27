#include "my-dynamic-linker.h"

// Locate essential sections for relocation and dynamic linking.
// Fill in the convenience struct `my_elf32` with the addresses of the sections.
// We can use the .dynamic section to locate .hash, .dynsym, .dynstr, .got.plt, and .rel.dyn sections.
// Refer to 1-10, 2-11, 2-12, and 2-13
// Use the `d_tag` field to identify these sections in the dynamic section.
// The section addresses are stored in the `d_un.d_ptr` field.
void get_dynamic_sections(my_elf32 *e) {
    printk("[MY-DL] Identifying ELF32 dynamic sections...\n");

    char *elf32_base = (char *)e->e_header;
    elf32_header *e_header = (elf32_header *)elf32_base;
    elf32_sheader *e_sheaders = (elf32_sheader *)(elf32_base + e_header->e_shoff);
    e->n_dynamics = 0;
    e->e_hash = NULL;
    e->e_dynsym = NULL;
    e->e_dynstr = NULL;
    e->e_pltgot = NULL;
    e->e_reldyn = NULL;

    for (int i = 0; i < e_header->e_shnum; i++) {
        if (e_sheaders[i].sh_type == SHT_DYNAMIC) {
            elf32_dynamic *dyn = (elf32_dynamic *)(elf32_base + e_sheaders[i].sh_offset);
            
            while (dyn[e->n_dynamics].d_tag != DT_NULL)
                e->n_dynamics++;
            
            e->e_dynamics = dyn;
            
            // dynamic entries to find other sections
            for (int j = 0; j < e->n_dynamics; j++) {
                switch (dyn[j].d_tag) {
                    case DT_HASH:
                        e->e_hash = (uint32_t *)(elf32_base + dyn[j].d_un.d_ptr);
                        break;
                    case DT_SYMTAB:
                        e->e_dynsym = (elf32_sym *)(elf32_base + dyn[j].d_un.d_ptr);
                        break;
                    case DT_STRTAB:
                        e->e_dynstr = (char *)(elf32_base + dyn[j].d_un.d_ptr);
                        break;
                    case DT_PLTGOT:
                        e->e_pltgot = (uint32_t *)(elf32_base + dyn[j].d_un.d_ptr);
                        break;
                    case DT_JMPREL:
                        e->e_reldyn = (elf32_rel *)(elf32_base + dyn[j].d_un.d_ptr);
                        break;
                }
            }
            break;
        }
    }

    if (e->e_hash == NULL || e->e_dynsym == NULL || e->e_dynstr == NULL || e->e_pltgot == NULL || e->e_reldyn == NULL)
        panic("[MY-DL] Couldn't find .hash, .dynsym, .dynstr, .pltgot, or .rel.dyn section\n");
    else
        printk("[MY-DL] Found dynamic sections: .hash: %x, .dynsym: %x, .dynstr: %x, .got.plt: %x, .rel.dyn: %x\n",
            e->e_hash, e->e_dynsym, e->e_dynstr, e->e_pltgot, e->e_reldyn);
}

// Perform load-time relocation of all the symbols in the 
// .got section of this ELF32 file (Read through relocation tables, .rel.plt and .rel.dyn, 
// jump to the symbol table to find the symbol address, and fill in the appropriate entries)
// This is needed because shared libraries are position-independent and thus do not know the
// symbol addresses at runtime.
void load_time_relocation(my_elf32 *e) {
    printk("[MY-DL] Performing load-time relocation of all the symbols in shared library\n");

    char *elf32_base = (char *)e->e_header;
    elf32_header *e_header = (elf32_header *)elf32_base;
    elf32_sheader *e_sheaders = (elf32_sheader *)(elf32_base + e_header->e_shoff);

    for (int i = 0; i < e_header->e_shnum; i++) {

        // Relocation section found
        if (e_sheaders[i].sh_type == SHT_REL) {
            elf32_rel *e_rels = (elf32_rel *)(elf32_base + e_sheaders[i].sh_offset); // Convenience pointer

            for (int j = 0; j * e_sheaders[i].sh_entsize < e_sheaders[i].sh_size; j++) {
                elf32_rel *e_rel = &e_rels[j]; // Convenience pointer

                uint32_t rel_type = e_rel->r_info & 0xff;
                uint32_t symtab_idx = e_rel->r_info >> 8;
                uint32_t entry_addr = (uint32_t)elf32_base + e_rel->r_offset;

                if (rel_type == R_ARM_RELATIVE) { // we should add the base address to the entry
                    *(uint32_t *)entry_addr += (uint32_t)elf32_base;
                } else if (rel_type == R_ARM_GLOB_DAT || rel_type == R_ARM_JUMP_SLOT || rel_type == R_ARM_ABS32) { // we should resolve the symbol in .got section
                    elf32_sym symtab_entry = e->e_dynsym[symtab_idx];

                    uint32_t symbol_addr = symtab_entry.st_value;
                    if (symtab_entry.st_shndx != SHN_UNDEF)
                        // Originally resolved symbol (ex. NOT notmain)
                        symbol_addr += (uint32_t)elf32_base;
                    *(uint32_t *)entry_addr = symbol_addr;

                    // Sanity check
                    // printk("[MY-DL] %d-%d: %s, resolved to %x\n", j, symtab_idx, e->e_dynstr + symtab_entry.st_name, symbol_addr);
                } else {
                    printk("[MY-DL] Unknown relocation type: %d\n", rel_type);
                }
            }
        }
    }
}

// Given a symbol name and an elf file, find the address of the symbol in the
// shared library loaded into memory.
// A super fun extension: use the .hash section to find the symbol in O(1) time!
uint32_t resolve_symbol(my_elf32 *e, char *symbol_name) {
    printk("[MY-DL] Resolving symbol <%s>...\n", symbol_name);
    char *elf32_base = (char *)e->e_header;
    uint32_t symbol_addr = 0;
    
    // # symbols from hash table
    uint32_t nbuckets = e->e_hash[0];
    uint32_t nchains = e->e_hash[1];
    
    // all symbols in the dynamic symbol table
    for (int i = 0; i < nchains; i++) {
        // symbol name
        char *sym_name = e->e_dynstr + e->e_dynsym[i].st_name;
        
        // find symbol
        if (strcmp(sym_name, symbol_name) == 0) {
            // Found the symbol!
            symbol_addr = e->e_dynsym[i].st_value;
            if (e->e_dynsym[i].st_shndx != SHN_UNDEF) {
                // add base addr
                symbol_addr += (uint32_t)elf32_base;
            }
            break;
        }
    }

    if (symbol_addr == 0)
        panic("[MY-DL] Couldn't find symbol: %s\n", symbol_name);
    else
        printk("[MY-DL] Found symbol: %s at %x\n", symbol_name, symbol_addr);

    return symbol_addr;
}