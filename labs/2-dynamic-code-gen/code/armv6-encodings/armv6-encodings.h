#ifndef __ARMV6_ENCODINGS_H__
#define __ARMV6_ENCODINGS_H__

enum {
    armv6_lr = 14,
    armv6_pc = 15,
    armv6_sp = 13,
    armv6_r0 = 0,

    op_mov = 0b1101,
    armv6_mvn = 0b1111,
    armv6_orr = 0b1100,
    op_mult = 0b0000,

    cond_always = 0b1110
};

// trivial wrapper for register values so type-checking works better.
typedef struct {
    uint8_t reg;
} reg_t;
static inline reg_t reg_mk(unsigned r) {
    if(r >= 16)
        panic("illegal reg %d\n", r);
    return (reg_t){ .reg = r };
}

// how do you add a negative? do a sub?
static inline uint32_t armv6_mov(reg_t rd, reg_t rn) {
    // could return an error.  could get rid of checks.
    //         I        opcode        | rd
    return cond_always << 28
        | op_mov << 21 
        | rd.reg << 12 
        | rn.reg
        ;
}

// static inline uint32_t* armv6_mov_code(uint32_t *code, reg_t rd, reg_t rn) {
//     *code = armv6_mov(rd, rn);
//     code++;
//     return code;
// }

static inline uint32_t 
armv6_mov_imm8_rot4(reg_t rd, uint32_t imm8, unsigned rot4) {
    if(imm8>>8)
        panic("immediate %d does not fit in 8 bits!\n", imm8);
    if(rot4 % 2)
        panic("rotation %d must be divisible by 2!\n", rot4);
    rot4 /= 2;
    if(rot4>>4)
        panic("rotation %d does not fit in 4 bits!\n", rot4);

    //todo("implement mov with rotate\n");
    return (cond_always << 28)
         | (1 << 25)               // immediate flag
         | (op_mov << 21)
         | (rd.reg << 12)
         | ((rot4 & 0xF) << 8)
         | (imm8 & 0xFF);
}
static inline uint32_t 
armv6_mov_imm8(reg_t rd, uint32_t imm8) {
    return armv6_mov_imm8_rot4(rd,imm8,0);
}

static inline uint32_t 
armv6_mvn_imm8(reg_t rd, uint32_t imm8) {
    if(imm8 >> 8)
        panic("immediate %d does not fit in 8 bits!\n", imm8);
    return (cond_always << 28)
         | (1 << 25)
         | (armv6_mvn << 21)
         | (rd.reg << 12)
         | (0 << 8)
         | (imm8 & 0xFF);
}

static inline uint32_t 
armv6_bx(reg_t rd) {
    return 0xE12FFF10 | (rd.reg);
}

// use 8-bit immediate imm8, with a 4-bit rotation.
static inline uint32_t 
armv6_orr_imm8_rot4(reg_t rd, reg_t rn, unsigned imm8, unsigned rot4) {
    if(imm8>>8)
        panic("immediate %d does not fit in 8 bits!\n", imm8);
    if(rot4 % 2)
        panic("rotation %d must be divisible by 2!\n", rot4);
    rot4 /= 2;
    if(rot4>>4)
        panic("rotation %d does not fit in 4 bits!\n", rot4);
    return (cond_always << 28)
         | (1 << 25)                 // immediate flag
         | (armv6_orr << 21)
         | (rn.reg << 16)
         | (rd.reg << 12)
         | ((rot4 & 0xF) << 8)
         | (imm8 & 0xFF);
}

static inline uint32_t 
armv6_orr_imm8(reg_t rd, reg_t rn, unsigned imm8) {
    if(imm8>>8)
        panic("immediate %d does not fit in 8 bits!\n", imm8);

    return armv6_orr_imm8_rot4(rd, rn, imm8, 0);
}

// a4-80
static inline uint32_t 
armv6_mult(reg_t rd, reg_t rm, reg_t rs) {
    return (cond_always << 28)
         | (rd.reg << 16)
         | (rs.reg << 8)
         | (0x9 << 4)
         | (rm.reg);
}


// load a word from memory[offset]
// ldr rd, [rn,#offset]
static inline uint32_t 
armv6_ldr_off12(reg_t rd, reg_t rn, int offset) {
    // a5-20
    int u;
    uint32_t off;
    if(offset < 0) {
        u = 0;
        off = -offset;
    } else {
        u = 1;
        off = offset;
    }
    if(off >= (1 << 12))
        panic("offset %d too big!\n", offset);

    return (cond_always << 28)
         | (1 << 26)             // bits 27-26 = 01
         | (1 << 24)             // P = 1 (pre-index)
         | (u << 23)             // U = sign bit
         | (0 << 22)             // B = 0 (word access)
         | (0 << 21)             // W = 0 (no write-back)
         | (1 << 20)             // L = 1 (load)
         | (rn.reg << 16)
         | (rd.reg << 12)
         | (off & 0xfff);
}

/**********************************************************************
 * synthetic instructions.
 * 
 * these can result in multiple instructions generated so we have to 
 * pass in a location to store them into.
 */

static inline uint32_t armv6_bl(uint32_t bl_pc, uint32_t target) {
    // Calculate the offset from PC+8 to target as a signed value
    int32_t offset = (int32_t)(target - (bl_pc + 8));
    
    // The bl instruction format:
    // 31:28 = condition (0xE for always)
    // printk("offset: %d\n", offset);
    // printk("Final bl: %x\n", 0xEB000000 | (offset & 0x00FFFFFF));
    return 0xEB000000 | ((offset >> 2) & 0x00FFFFFF);
}

static inline uint32_t *
armv6_load_imm32(uint32_t *code, reg_t rd, uint32_t imm32) {
    // *code = armv6_bl((uint32_t)code + 4, ((uint32_t)code + 8));
    // code++;
    // *code = imm32;
    // code++;
    // *code = armv6_ldr_off12(rd, reg_mk(armv6_pc), -4);
    // return code;

    *code = armv6_bl((uint32_t)code, ((uint32_t)code + 8)) & ~(1 << 24);
    code++;

    *code = imm32;
    code++;

    *code = armv6_ldr_off12(rd, reg_mk(armv6_pc), -12);
    code++;

    return code;
}

// MLA (Multiply Accumulate) multiplies two signed or unsigned 32-bit
// values, and adds a third 32-bit value.
//
// a4-66: multiply accumulate.
//      rd = rm * rs + rn.
static inline uint32_t
armv6_mla(reg_t rd, reg_t rm, reg_t rs, reg_t rn) {    
    return (cond_always << 28)
         | (1 << 21)             // set accumulate bit
         | (rd.reg << 16)
         | (rn.reg << 12)
         | (rs.reg << 8)
         | (0x9 << 4)
         | (rm.reg);
}

static inline uint32_t* armv6_mla_code(uint32_t *code, reg_t rd, reg_t rm, reg_t rs, reg_t rn) {
    *code = armv6_mla(rd, rm, rs, rn);
    code++;
    return code;
}

#endif
