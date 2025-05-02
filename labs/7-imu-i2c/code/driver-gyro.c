// engler: simplistic mpu6050 gyro driver code, mirrors
// <driver-accel.c>.  
//  1. initializes gyroscope,
//  2. prints N readings.
//
// as with <driver-accel.c>:
//   - <mpu-6050.h> has the interface description.
//   - <mpu-6050.c> is where your code will go.
//
//
// Obvious extension:
//  0. validate the 'data ready' matches what we set it to.
//  1. device interrupts.
//  2. bit bang i2c
//  3. multiple devices.
//  4. extend the interface to give more control.
//  
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
// #include "rpi-math.h"

// int gyro_self_test(uint8_t dev_addr) {
//     imu_wr(dev_addr, GYRO_CONFIG, gyro_250dps << 3);
//     delay_ms(100);

//     imu_xyz_t normal_readings = {0};
//     for(int i = 0; i < 20; i++) {  // Discard first 20 readings
//         normal_readings = gyro_rd((gyro_t*)&dev_addr);
//         delay_ms(10);
//     }

//     imu_wr(dev_addr, GYRO_CONFIG, (gyro_250dps << 3) | 0b11100000);
//     delay_ms(250); 

//     imu_xyz_t self_test_readings = {0};
//     for(int i = 0; i < 20; i++) {  // Discard first 20 readings
//         self_test_readings = gyro_rd((gyro_t*)&dev_addr);
//         delay_ms(10);
//     }

//     // Calculate STR (Self-Test Response) = self_test_readings - normal_readings
//     imu_xyz_t str = {
//         .x = self_test_readings.x - normal_readings.x,
//         .y = self_test_readings.y - normal_readings.y,
//         .z = self_test_readings.z - normal_readings.z
//     };

//     uint8_t x_trim = imu_rd(dev_addr, SELF_TEST_X) & 0x1F;  // Low 5 bits
//     uint8_t y_trim = imu_rd(dev_addr, SELF_TEST_Y) & 0x1F;  // Low 5 bits
//     uint8_t z_trim = imu_rd(dev_addr, SELF_TEST_Z) & 0x1F;  // Low 5 bits

//     float ft_x = 25.0f * 131.0f * powf(1.046f, x_trim - 1.0f);
//     float ft_y = -25.0f * 131.0f * powf(1.046f, y_trim - 1.0f);
//     float ft_z = 25.0f * 131.0f * powf(1.046f, z_trim - 1.0f);

//     float x_diff = fabsf((str.x - ft_x) / ft_x) * 100.0f;
//     float y_diff = fabsf((str.y - ft_y) / ft_y) * 100.0f;
//     float z_diff = fabsf((str.z - ft_z) / ft_z) * 100.0f;

//     printk("Self-test results:\n");
//     printk("X: STR=%d, FT=%f, diff=%f\n", str.x, ft_x, x_diff);
//     printk("Y: STR=%d, FT=%f, diff=%f\n", str.y, ft_y, y_diff);
//     printk("Z: STR=%d, FT=%f, diff=%f\n", str.z, ft_z, z_diff);

//     int pass = (x_diff <= 14.0f) && (y_diff <= 14.0f) && (z_diff <= 14.0f);
//     printk("Self-test %s\n", pass ? "PASSED" : "FAILED");

//     imu_wr(dev_addr, GYRO_CONFIG, gyro_250dps << 3);
//     delay_ms(100); 

//     return pass;
// }

void notmain(void) {
    delay_ms(100);   // allow time for i2c/device to boot up.
    i2c_init();
    delay_ms(100);   // allow time for i2c/dev to settle after init.

    // from application note.
    uint8_t dev_addr = 0b1101000;

    enum { 
        WHO_AM_I_REG      = 0x75, 
        WHO_AM_I_VAL = 0x68,       
    };

    uint8_t v = imu_rd(dev_addr, WHO_AM_I_REG);
    if(v != WHO_AM_I_VAL)
        panic("Initial probe failed: expected %b (%x), have %b (%x)\n", 
            WHO_AM_I_VAL, WHO_AM_I_VAL, v, v);
    printk("SUCCESS: mpu-6050 acknowledged our ping: WHO_AM_I=%b!!\n", v);

    // Perform gyro self-test

    // hard reset: it won't be when your pi reboots.
    mpu6050_reset(dev_addr);

    // part 2: get the gyro working.
    gyro_t g = mpu6050_gyro_init(dev_addr, gyro_500dps);

    // if (!gyro_self_test(dev_addr)) {
    //     panic("Gyro self-test failed!\n");
    // }

    g = mpu6050_gyro_init(dev_addr, gyro_500dps);
    assert(g.dps==500);

    for(int i = 0; i < 10; i++) {
        imu_xyz_t xyz_raw = gyro_rd(&g);
        output("reading gyro %d\n", i);
        xyz_print("\traw", xyz_raw);
        xyz_print("\tscaled (milli dps)", gyro_scale(&g, xyz_raw));
        delay_ms(1000);
    }
}
