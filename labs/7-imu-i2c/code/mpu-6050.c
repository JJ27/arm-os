// engler: simplistic mpu6050 driver code.
//
// we smash all the code in here so its easy to find in the lab
// setting: when it works, seperate it out!
//
// three parts:
//  1. i2c helpers (you don't have to modify)
//  2. accel routines (you have to finish)
//  3. gyro routines (you have to finish)
//
// two algorithms: 
//   - implement the code from scratch (fun! let us know you're
//     doing)
//   - grep for TODO and fill in.
//
// KEY: document why you are doing what you are doing.
//  **** put page numbers for any device-specific things you do ***
//  **** put page numbers for any device-specific things you do ***
//  **** put page numbers for any device-specific things you do ***
//  **** put page numbers for any device-specific things you do ***
//  **** put page numbers for any device-specific things you do ***
//  **** put page numbers for any device-specific things you do ***
//  **** put page numbers for any device-specific things you do ***
// 
// also: a sentence or two will go a long way in a year when you want 
// to re-use the code.
#include "rpi.h"
#include "mpu-6050.h"

/**********************************************************************
 * 1. i2c helpers (see i2c.h)  for reading and write individual registers,
 * and for doing burst reads of N registers.
 *
 * recall: each register has a 1 byte name and a 1 byte value.  we
 * read or write them explicitly using i2c rather than using memory
 * loads and stores (as we do with broadcom memory mapped device 
 * interfaces).
 *
 * you don't have to modify this.
 */

// read a single device register <reg> from i2c device 
// <addr> and return the result.
uint8_t imu_rd(uint8_t addr, uint8_t reg) {
    i2c_write(addr, &reg, 1);
    output("");
    uint8_t v;
    i2c_read(addr,  &v, 1);
    //output("read reg\n");
    return v;
}

// set a single device register <reg> on device
// <addr> to value <v>
// 
// the operation is sent over i2c as two 8-bit values: 
// (byte 0 = <reg>, byte 1 = <v>)
void imu_wr(uint8_t addr, uint8_t reg, uint8_t v) {
    uint8_t data[2];
    data[0] = reg;
    data[1] = v;
    i2c_write(addr, data, 2);
}

// do a "burst read" of <n> registers into buffer <v>, where 
//  - <addr> = device addr
//  - <base_reg> = lowest reg in sequence
//  - <n> = total number of 8-bit registers to read.
int imu_rd_n(uint8_t addr, uint8_t base_reg, uint8_t *v, uint32_t n) {
    //output("imu_rd_n start\n");
    i2c_write(addr, (void*) &base_reg, 1);
    delay_us(100);
    //output("imu_rd_n write done\n");
    return i2c_read(addr, v, n);
}

/**********************************************************************
 * 2. simple accel setup and use
 */

// IMU + accel register map
enum {
    // p6, p14
    accel_config_reg = 0x1c,
    accel_off = 3,

    USER_CTRL = 0x6a,   // p 39

    // p31,32
    ACCEL_XOUT_H = 0x3b,
    accel_xout_l = 0x3c,
    accel_yout_h = 0x3d,
    accel_yout_l = 0x3e,
    accel_zout_h = 0x3f,
    accel_zout_l = 0x40,

    // p 41
    PWR_MGMT_1 = 0x6b,
    // p 42
    PWR_MGMT_2 = 108,

    // p 29
    INT_STATUS = 0x3a,
    // p 28
    INT_ENABLE = 0x38,
};

// returns a scaled milligauss value given the
// scale that <g> can range over.
static int mg_scaled(int v, int g) {
    return (v * 1000 * g) / SHRT_MAX;
}

// takes in raw data and scales it.
imu_xyz_t accel_scale(accel_t *h, imu_xyz_t xyz) {
    int g = h->g;
    int x = mg_scaled(h->g, xyz.x);
    int y = mg_scaled(h->g, xyz.y);
    int z = mg_scaled(h->g, xyz.z);
    return xyz_mk(x,y,z);
}

// check interrupt status (INT_STATUS) to see 
// if data is ready.
// 
// extension: hook up gpio interrupts and 
// pull the reading into a circular buffer
// (similar to device lab).
int accel_has_data(const accel_t *h) {
    // Read INT_STATUS register (0x3a) to check if data is ready
    // Bit 0 indicates data is ready (p29)
    uint8_t status = imu_rd(h->addr, INT_STATUS);
    return (status & 1);
}


// combines a 8-bit <lo> and 8-bit <hi> from the sensor into
// a single *signed* 16-bit value.  you want to make sure that
// if the 16-th bit is set, the returned value will be negative.
// i'd suggest playing w/ gdb or small C programs to see that what
// C does matches your intuition.
static short mg_raw(uint8_t lo, uint8_t hi) {
    return (short)((hi << 8) | lo);
}

// sanity testing code.
static void test_mg(int expected, uint8_t l, uint8_t h, unsigned g) {
    int s_i = mg_scaled(mg_raw(h,l),g);

    output("expect = %d, got %d", expected, s_i);
    if(expected == s_i)
        output("success!\n");
    else
        output("failed!\n");

    assert(s_i == expected);
}
    
// set to accel 2g (p14), bandwidth 20hz (p15)
// https://stackoverflow.com/questions/60419390/mpu-6050-correctly-reading-data-from-the-fifo-register
accel_t mpu6050_accel_init(uint8_t addr, unsigned accel_g) {
    unsigned g = 0;
    switch(accel_g) {
    case accel_2g: g = 2; break;
    case accel_4g: g = 4; break;
    case accel_8g: g = 8; break;
    case accel_16g: g = 16; break;
    default: panic("invalid g=%b\n", g);
    }

    // first test that your scaling works.
    // this is device independent.
    test_mg(0, 0x00, 0x00, 2);
    test_mg(350, 0x16, 0x69, 2);
    test_mg(1000, 0x40, 0x09, 2);
    test_mg(-350, 0xe9, 0x97, 2);
    test_mg(-1000, 0xbf, 0xf7, 2);

    // Set accelerometer configuration (p14)
    // Bits 4:3 control the full scale range
    // 0b00 = ±2g, 0b01 = ±4g, 0b10 = ±8g, 0b11 = ±16g
    imu_wr(addr, accel_config_reg, accel_g << 3);

    output("accel_config_reg=%b\n", imu_rd(addr, accel_config_reg));
    return (accel_t) { .addr = addr, .g = g, .hz = 20 };
}

// hard reset: recall --- for all external devices connected to 3v or 
// 5v, rebooting the pi has no effect on the external device.  so either
// we would have to disconnect/reconnect the power (manually or w/ some
// transistor) or we can use the device control to do a hard reset.  we
// do the later.
void mpu6050_reset(uint8_t addr) {
    // make sure device is up.
    delay_ms(100);

    // page 41: to reset device: set bit 7 = 1 in register
    // PWR_MGMT_1 (register 0x6b)
    imu_wr(addr, PWR_MGMT_1, 0x80);  // Set bit 7 to reset

    // give time to shutdown, spin up.
    delay_ms(100);

    if(bit_is_on(imu_rd(addr, PWR_MGMT_1), 6))
        output("device booted up in sleep mode!\n");

    // clear sleep mode: (PWR_MGMT_1)
    // if you do *NOT* do this, then the device we have does not work.
    // according to my reading of the data sheet, the value of 0x6b should
    // be 0 after reset.  so i don't get this.
    imu_wr(addr, PWR_MGMT_1, 0x00);  // Clear sleep mode

    delay_ms(100);

    // page 39: USER_CTRL (register 0x6a): reset:
    //   - the signal path 
    //   - i2c master mode 
    //   - fifo
    // not sure if redundant after device reset --- datasheet
    // unclear --- so we do to be sure.
    imu_wr(addr, USER_CTRL, 0x7);  // Reset signal path, I2C master, and FIFO

    delay_ms(100);

    // NOTE: enable interrupts only on the IMU, not on your pi.
    // (INT_ENABLE) after you config (p27):
    // - latch to be held high until cleared;
    // - read to clear it.
    imu_wr(addr, INT_ENABLE, 0x1);  // Enable data ready interrupt
}

// block until there is data and then return it (raw)
//
// p26 interprets the data.
// if high bit is set, then result is negative.
//
// read them all at once for consistent
// readings using autoincrement.
imu_xyz_t accel_rd(const accel_t *h) {
    uint8_t addr = h->addr;
    uint8_t regs[6];

    // wait until data.
    //output("start waiting for data\n");
    while(!accel_has_data(h)) {
        //output("waiting for data\n");
    }
    //output("data found\n");

    // Read all 6 registers at once (p31-32)
    // ACCEL_XOUT_H (0x3b) through ACCEL_ZOUT_L (0x40)
    imu_rd_n(addr, ACCEL_XOUT_H, regs, 6);

    //output("img_rd_n done\n");

    // Combine high and low bytes for each axis
    int x = mg_raw(regs[1], regs[0]);  // X axis
    int y = mg_raw(regs[3], regs[2]);  // Y axis
    int z = mg_raw(regs[5], regs[4]);  // Z axis

    return xyz_mk(x, y, z);
}

/*********************************************************************
 * gyro code
 * 
 * don't trust the scaling  code.
 */

// gyro registers.
enum {
    // p13, p6
    CONFIG = 26, 
    // p14, p6
    GYRO_CONFIG = 27, 

    // p7
    GYRO_XOUT_H = 67,
};

static inline int 
mdps_scaled(int x, int dps_scale) {
    return (x * dps_scale) / 1000;
}

// not sure this is right: use the code below?
imu_xyz_t gyro_scale(gyro_t *h, imu_xyz_t xyz) {
    int dps = h->dps;
    int x = mdps_scaled(dps, xyz.x);
    int y = mdps_scaled(dps, xyz.y);
    int z = mdps_scaled(dps, xyz.z);
    return xyz_mk(x,y,z);
}

// not sure this is right.
static int dps_to_scale(unsigned dps) {
    switch(dps) {
    // we do this just for testing.
    case 245:
    case 250:  return  8750;
    case 500:  return 17500;
    case 1000: return 35000;
    case 2000: return 70000;
    default: panic("invalid dps: %d\n", dps);
    }
}

// not sure this is right.
static int mdps_scale_deg2(int deg, int dps) {
    // hack to get around no div
    if(dps == 250)
        return (deg * 250) / SHRT_MAX;
    else if(dps == 500)
        return (deg * 500) / SHRT_MAX;
    else
        panic("bad dps=%d\n", dps);
}

// not sure this is right.
static int mdps_scale_deg(int deg, int dps) {
    return (deg * dps_to_scale(dps)) /1000;
}

static inline int within(int exp, int have, int tol) {
    int diff = exp - have;
    if(diff < 0)
        diff = -diff;
    return diff <= tol;
}

static void test_dps(int expected_i, uint8_t h, uint8_t l, int dps) {
    int s_i = mdps_scale_deg(mg_raw(l, h), dps);
    expected_i *= 1000;
    int tol = 5;
    if(!within(expected_i, s_i, tol))
        panic("expected %d, got = %d, scale=%d\n", expected_i, s_i, dps);
    else
        output("expected %d, got = %d, (within +/-%d) scale=%d\n", expected_i, s_i, tol, dps);
}

gyro_t mpu6050_gyro_init(uint8_t addr, unsigned gyro_dps) { 
    // device independent testing.
    unsigned dps = 245;
    test_dps(0, 0x00, 0x00, dps);
    test_dps(100, 0x2c, 0xa4, dps);
    test_dps(200, 0x59, 0x49, dps);
    test_dps(-100, 0xd3, 0x5c, dps);

    dps = 0;
    switch(gyro_dps) {
    case gyro_250dps:   dps = 250; break;
    case gyro_500dps:   dps = 500; break;
    case gyro_1000dps:  dps = 1000; break;
    case gyro_2000dps:  dps = 2000; break;
    default: panic("invalid dps: %b\n", dps);
    }

    // Set CONFIG register (p13)
    // Bits 2:0 = DLPF_CFG (Digital Low Pass Filter)
    // 0 = 260Hz bandwidth, 256Hz sample rate
    imu_wr(addr, CONFIG, 0x0);

    // Set gyroscope configuration (p14)
    // Bits 4:3 control the full scale range
    // 0b00 = ±250°/s, 0b01 = ±500°/s, 0b10 = ±1000°/s, 0b11 = ±2000°/s
    imu_wr(addr, GYRO_CONFIG, gyro_dps << 3);

    return (gyro_t) { .addr = addr, .dps = dps };
}

// use int or fifo to tell when data.
int gyro_has_data(const gyro_t *h) {
    // Read INT_STATUS register (0x3a) to check if data is ready
    // Bit 0 indicates data is ready (p29)
    uint8_t status = imu_rd(h->addr, INT_STATUS);
    return (status & 1);
}

// return a single raw gyro reading.
imu_xyz_t gyro_rd(const gyro_t *h) {
    uint8_t addr = h->addr;
    uint8_t regs[6];    

    while(!gyro_has_data(h))
        ;

    // Read all 6 registers at once (p32)
    // GYRO_XOUT_H (0x43) through GYRO_ZOUT_L (0x48)
    imu_rd_n(addr, GYRO_XOUT_H, regs, 6);

    // Combine high and low bytes for each axis
    int x = mg_raw(regs[1], regs[0]);  // X axis
    int y = mg_raw(regs[3], regs[2]);  // Y axis
    int z = mg_raw(regs[5], regs[4]);  // Z axis

    return xyz_mk(x, y, z);
}
