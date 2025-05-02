// extensions: 
//  1. tune the different delays
//  2. make sure you handle failures and return error codes or 
//     explicitly die

#include "rpi.h"
#include "sw-i2c.h"
#include <stdint.h>
#include <stdbool.h>

static void i2c_start_cond(i2c_t *h);
static void i2c_stop_cond(i2c_t *h);

// Hardware-specific support functions that MUST be customized:
#define I2CSPEED 100

// can swap the two interfaces
#define USE_PULLUP
#ifdef USE_PULLUP

// put all the code that assumes the use of pullups.

// this worked for me if i used pullup
static void pin_setup(uint32_t scl, uint32_t sda) {
    gpio_set_input(scl);
    gpio_set_input(sda);
    gpio_set_pullup(scl);
    gpio_set_pullup(sda);
}
#else

// or put the code that assumes you explicitly set scl/sda

// this worked for me if i explicitly set values.
static void pin_setup(uint32_t scl, uint32_t sda) {
    gpio_set_output(scl);
    gpio_set_output(sda);
    gpio_write(scl,1);
    gpio_write(sda,1);
}

#endif

static inline void I2C_delay(i2c_t *h) { 
    //delay_us(4); 
    volatile int v;
    int i;

    for (i = 0; i < I2CSPEED / 2; ++i) {
        v;
    }
}

static bool read_SCL(i2c_t *h) {
    if (!h->SCL_is_input_p) {
        gpio_set_input(h->SCL);
        h->SCL_is_input_p = 1;
    }
    return gpio_read(h->SCL);
}

static bool read_SDA(i2c_t *h) {
    //if (!h->SDA_is_input_p) {
        gpio_set_input(h->SDA);
        h->SDA_is_input_p = 1;
    //}
    return gpio_read(h->SDA);
}

static void set_SCL(i2c_t *h) {
    if (!h->SCL_is_input_p) {
        gpio_set_input(h->SCL);
        h->SCL_is_input_p = 1;
    }
}

static void clear_SCL(i2c_t *h) {
    if (h->SCL_is_input_p) {
        gpio_set_output(h->SCL);
        h->SCL_is_input_p = 0;
    }
    gpio_write(h->SCL, 0);
}

static void set_SDA(i2c_t *h) {
    if (!h->SDA_is_input_p) {
        gpio_set_input(h->SDA);
        h->SDA_is_input_p = 1;
    }
}

static void clear_SDA(i2c_t *h) {
    if (h->SDA_is_input_p) {
        gpio_set_output(h->SDA);
        h->SDA_is_input_p = 0;
    }
    gpio_write(h->SDA, 0);
}

static void arbitration_lost(i2c_t *h) {
    panic("I2C arbitration lost\n");
}

i2c_t sw_i2c_init(uint8_t addr, uint32_t scl, uint32_t sda) {
    assert(scl<32);
    assert(sda<32);
    pin_setup(scl,sda);

    return (i2c_t) { 
        .is_transmit_p = 1, 
        .SCL = scl, 
        .SDA = sda, 
        .addr = addr,
        .SCL_is_input_p = 1,
        .SDA_is_input_p = 1,
        .started_p = false
    };
}

static void i2c_write_bit(i2c_t *h, bool bit) {
    if (bit) {
        set_SDA(h);
    } else {
        clear_SDA(h);
    }

    I2C_delay(h);

    set_SCL(h);

    I2C_delay(h);

    while (read_SCL(h) == 0) { // Clock stretching
    }

    if (bit && (read_SDA(h) == 0)) {
        arbitration_lost(h);
    }

    // Clear the SCL to low for next change
    clear_SCL(h);
}

// Read a bit from I2C bus
static bool i2c_read_bit(i2c_t *h) {
    bool bit;

    set_SDA(h);

    I2C_delay(h);

    set_SCL(h);

    while (read_SCL(h) == 0) { // Clock stretching
    }

    I2C_delay(h);

    bit = read_SDA(h);

    clear_SCL(h);

    return bit;
}

static bool i2c_write_byte(i2c_t *h, bool send_start, bool send_stop, uint8_t byte) {
    unsigned bit;
    bool nack;

    if (send_start) {
        i2c_start_cond(h);
    }

    for (bit = 0; bit < 8; ++bit) {
        i2c_write_bit(h, (byte & 0x80) != 0);
        byte <<= 1;
    }

    nack = i2c_read_bit(h);

    if (send_stop) {
        i2c_stop_cond(h);
    }

    return nack;
}

static uint8_t i2c_read_byte(i2c_t *h, bool nack, bool send_stop) {
    uint8_t byte = 0;
    unsigned bit;

    for (bit = 0; bit < 8; ++bit) {
        byte = (byte << 1) | i2c_read_bit(h);
    }

    i2c_write_bit(h, nack);

    if (send_stop) {
        i2c_stop_cond(h);
    }

    return byte;
}

void i2c_start_cond(i2c_t *h) {
    if (h->started_p) { 
        set_SDA(h);
        I2C_delay(h);
        set_SCL(h);
        while (read_SCL(h) == 0) { // Clock stretching
        }

        I2C_delay(h);
    }

    if (read_SDA(h) == 0) {
        arbitration_lost(h);
    }

    clear_SDA(h);
    I2C_delay(h);
    clear_SCL(h);
    h->started_p = true;
}

void i2c_stop_cond(i2c_t *h) {
    clear_SDA(h);
    I2C_delay(h);

    set_SCL(h);
    // Clock stretching
    while (read_SCL(h) == 0) {
    }

    I2C_delay(h);

    set_SDA(h);
    I2C_delay(h);

    if (read_SDA(h) == 0) {
        arbitration_lost(h);
    }

    h->started_p = false;
}

int sw_i2c_write(i2c_t *h, uint8_t data[], unsigned nbytes) {
    // Start cond + device address
    if (i2c_write_byte(h, true, false, (h->addr << 1) | 0)) {
        return -1; // No ACK 
    }

    for (unsigned i = 0; i < nbytes; i++) {
        if (i2c_write_byte(h, false, false, data[i])) {
            return -1; // No ACK
        }
    }

    i2c_stop_cond(h);
    return 0;
}

int sw_i2c_read(i2c_t *h, uint8_t data[], unsigned nbytes) {
    if (i2c_write_byte(h, true, false, (h->addr << 1) | 1)) {
        return -1; // No ACK
    }

    for (unsigned i = 0; i < nbytes; i++) {
        bool last_byte = (i == nbytes - 1);
        data[i] = i2c_read_byte(h, last_byte, last_byte);
    }

    return 0;
}
