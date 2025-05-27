#include "rpi.h"
#include "my-legit-dynamic-linker.h"

// From notmain.c
extern my_elf32 exec_e;
extern my_elf32 libpi_e;

// Resolve all undefined symbols in the .dynsym section of the given ELF32 file at load-time.
// This is needed because some symbols in a dynamic library might exist in our main executable (e.g., `notmain`)
void load_time_resolve(my_elf32 *exec_e, my_elf32 *dyn_e) {
    printk("[MY-DL] Resolving undefined symbols in shared library...\n");

    // Best-effort attempt to resolve undefined symbols in the .dynsym section
    uint32_t n_symbols = dyn_e->e_hash[1]; // this is equivalent to the number of symbols in .dynsym
    for (int i = 0; i < n_symbols; i++) {
        if (dyn_e->e_dynsym[i].st_shndx == SHN_UNDEF && dyn_e->e_dynsym[i].st_name) { // symbol is undefined
            char *symbol_name = dyn_e->e_dynstr + dyn_e->e_dynsym[i].st_name;
            // printk("[MY-DL] Undefined symbol: %s\n", symbol_name);
            // printk("[MY-DL] resolved addr: %x\n", resolve_symbol(&main_elf, symbol_name));
            dyn_e->e_dynsym[i].st_value = resolve_symbol(exec_e, symbol_name);
        }
    }
}

// Called by my_dl_entry_asm
// gotplt: the address of the third entry of .got.plt
// gotplt_entry: the address of the unresolved symbol's .got.plt entry
// Performs dynamic linking and resolves the symbol, saves its address in gotplt_entry
// Returns the address of the resolved symbol
uint32_t dynamic_linker_entry_c(uint32_t *gotplt, uint32_t *gotplt_entry) {
    // Calculate the index of the unresolved symbol in .got.plt
    // gotplt_entry points to GOT[i+3], and gotplt points to GOT[2]
    // So the index is (gotplt_entry - gotplt - 3)
    int symbol_index = gotplt_entry - gotplt - 3;

    // Find the relocation entry in .rel.dyn section
    // The relocation entry's INFO field contains the symbol index in .dynsym
    elf32_rel *rel_entry = &exec_e.e_reldyn[symbol_index];
    uint32_t symtab_idx = rel_entry->r_info >> 8;

    // Get the symbol name from .dynstr section
    char *symbol_name = exec_e.e_dynstr + exec_e.e_dynsym[symtab_idx].st_name;
    printk("[MY-DL] Dynamic linker: Unresolved symbol encountered: <%s>. Dynamic linker invoked.\n", symbol_name);

    // Resolve the symbol and fill in the got table entry
    uint32_t symbol_addr = resolve_symbol(&libpi_e, symbol_name);
    *gotplt_entry = symbol_addr;
    printk("[MY-DL] Dynamic linker: Resolved symbol %s to %x\n", symbol_name, symbol_addr);

    return symbol_addr; // the rest should be handled by asm
}
