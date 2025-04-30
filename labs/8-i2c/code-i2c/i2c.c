#include "rpi.h"
#include "libc/helper-macros.h"
#include "i2c.h"
#include "gpio.h"
#include <stdint.h>

typedef struct {
    uint32_t c;
    uint32_t s;
    uint32_t dlen;
    uint32_t a;
    uint32_t fifo;
} i2c_t;

static i2c_t *i2c = (i2c_t *)0x20804000;

// GPIO pins
#define SDA_PIN 2
#define SCL_PIN 3

void i2c_init(void) {
    // Set GPIO pins 2 and 3 to ALT0
    gpio_set_function(SDA_PIN, GPIO_FUNC_ALT0);
    gpio_set_function(SCL_PIN, GPIO_FUNC_ALT0);
    dev_barrier();
    gpio_set_pullup(SDA_PIN);
    gpio_set_pullup(SCL_PIN);

    // Enable I2C + clear fifo
    i2c->c = (1 << 15) | (1 << 4);
    dev_barrier();

    // Clear s
    i2c->s = 0;
    dev_barrier();

    // Wait for transfer to complete and check for errors
    //while (i2c->s & (1 << 0)) { }  // wait for done?? idk
    if (i2c->s & ((1 << 8) | (1 << 9) | (1 << 10))) {  // Check initialization
        panic("I2C initialization failed\n");
    }
    dev_barrier();
}

int i2c_read(unsigned addr, uint8_t data[], unsigned nbytes) {
    //output("read start\n");
    while (i2c->s & (1 << 0)) { } // transfer active??
    dev_barrier();

    i2c->c |= ((1 << 4) | (1 << 5));

    // Check fifo empty + no clock stretch timeout + no errors
    if (i2c->s & ((1 << 8) | (1 << 9) | (1 << 10))) {
        return -1;  // Error!
    }

    // Clear DONE field
    i2c->s = (1 << 1);
    dev_barrier();

    // Set addr + dlen
    i2c->a = addr;
    i2c->dlen = nbytes;
    dev_barrier();

    // Start transfer via read
    i2c->c = (1 << 15) | (1 << 7) | (1 << 0);  // I2CEN = 1, READ = 1, START = 1
    dev_barrier();

    // Wait for transfer initialization
    //while (!(i2c->s & (1 << 0))) { }
    dev_barrier();

    // Read bytes
    for (unsigned i = 0; i < nbytes; i++) {
        // Wait for data available
        while (!(i2c->s & (1 << 5))) { }
        data[i] = i2c->fifo;
        dev_barrier();
    }

    // Wait for transfer to finish itself
    while (i2c->s & (1 << 0)) { }
    dev_barrier();

    // Check errors
    if (i2c->s & ((1 << 8) | (1 << 9) | (1 << 10))) {
        return -1;
    }
    //output("read end\n");

    return 0;
}

int i2c_write(unsigned addr, uint8_t data[], unsigned nbytes) {
    //output("write start\n");
    while (i2c->s & (1 << 0)) { } // check transfer active?
   
    dev_barrier();

    i2c->c |= ((1 << 4) | (1 << 5));

    if (i2c->s & ((1 << 8) | (1 << 9) | (1 << 10))) {
        return -1;  // Error
    }

    // Clear DONE
    i2c->s = (1 << 0);
    dev_barrier();
    //output("write end\n");

    // Set addr + dlen
    i2c->a = addr;
    i2c->dlen = nbytes;
    dev_barrier();

    // Start transfer via write
    i2c->c = (1 << 15) | (1 << 7);  // I2CEN = 1, START = 1
    dev_barrier();

    // Wait for transfer initialization
    //while (!(i2c->s & (1 << 0))) { }
    
    dev_barrier();

    // Write bytes
    for (unsigned i = 0; i < nbytes; i++) {
        // Wait for FIFO space
        while (!(i2c->s & (1 << 4))) { }  // Check FIFO full
        i2c->fifo = data[i];
        dev_barrier();
    }

    // Wait for transfer to finish itself
    while (i2c->s & (1 << 0)) { }
    dev_barrier();

    if (i2c->s & ((1 << 8) | (1 << 9) | (1 << 10))) {
        return -1;
    }

    return 0;
}

