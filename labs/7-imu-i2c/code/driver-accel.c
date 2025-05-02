// engler: simplistic mpu6050 accel driver code.  
//   - <mpu-6050.h> has the interface description.
//   - <mpu-6050.c> is where your code will go.
//
// simple driver that:
//    1. initializes i2c and the 6050 IMU (mpu6050_reset)
//    2. prints does repeated readings (<accel_rd>)
//
// obvious extensions:
//    1. validating that the "data is ready" check exactly matches
//       our expected rate: repeatedly reading stale values and/or 
//       losing them will distort calculations.
//    2. device interrupts to pull readings into circular queue. 
//    3. roll your own i2c.
//    4. if you have (3): then do multiple IMU's.  having an imu glove
//       is a cool final project.
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
#include "rpi-math.h"

int accel_self_test(uint8_t dev_addr) {
    imu_wr(dev_addr, ACCEL_CONFIG, accel_8g << 3);
    delay_ms(100);

    imu_xyz_t normal_readings = {0};
    for(int i = 0; i < 20; i++) {  // Discard first 20 readings
        normal_readings = accel_rd((accel_t*)&dev_addr);
        delay_ms(10);
    }

    imu_wr(dev_addr, ACCEL_CONFIG, (accel_8g << 3) | 0b11100000);
    delay_ms(250);

    imu_xyz_t self_test_readings = {0};
    for(int i = 0; i < 20; i++) {  // Discard first 20 readings
        self_test_readings = accel_rd((accel_t*)&dev_addr);
        delay_ms(10);
    }

    // Calculate STR (Self-Test Response) = self_test_readings - normal_readings
    imu_xyz_t str = {
        .x = self_test_readings.x - normal_readings.x,
        .y = self_test_readings.y - normal_readings.y,
        .z = self_test_readings.z - normal_readings.z
    };


    uint8_t xa_upper = (imu_rd(dev_addr, SELF_TEST_X) & 0b11100000) >> 3;
    uint8_t ya_upper = (imu_rd(dev_addr, SELF_TEST_Y) & 0b11100000) >> 3;
    uint8_t za_upper = (imu_rd(dev_addr, SELF_TEST_Z) & 0b11100000) >> 3;

    uint8_t xa_lower = (imu_rd(dev_addr, 16) & 0b110000) >> 4;
    uint8_t ya_lower = (imu_rd(dev_addr, 16) & 0b1100) >> 2;
    uint8_t za_lower = (imu_rd(dev_addr, 16) & 0b11);

    uint8_t xa_test = xa_upper | xa_lower;
    uint8_t ya_test = ya_upper | ya_lower;
    uint8_t za_test = za_upper | za_lower;

    float ft_x = xa_test == 0 ? 0 : 4096.0f * 0.34f * powf(0.92f/0.34f, (xa_test - 1.0f)/(32.0f - 2.0f));
    float ft_y = ya_test == 0 ? 0 : 4096.0f * 0.34f * powf(0.92f/0.34f, (ya_test - 1.0f)/(32.0f - 2.0f));
    float ft_z = za_test == 0 ? 0 : 4096.0f * 0.34f * powf(0.92f/0.34f, (za_test - 1.0f)/(32.0f - 2.0f));
    float x_diff = fabsf((str.x - ft_x) / ft_x) * 100.0f;
    float y_diff = fabsf((str.y - ft_y) / ft_y) * 100.0f;
    float z_diff = fabsf((str.z - ft_z) / ft_z) * 100.0f;

    printk("Xa,ya,za test: %d, %d, %d\n", xa_test, ya_test, za_test);

    printk("Accel self-test results:\n");
    printk("X: STR=%d, FT=%f, diff=%f\n", str.x, ft_x, x_diff);
    printk("Y: STR=%d, FT=%f, diff=%f\n", str.y, ft_y, y_diff);
    printk("Z: STR=%d, FT=%f, diff=%f\n", str.z, ft_z, z_diff);

    int pass = (x_diff <= 14.0f) && (y_diff <= 14.0f) && (z_diff <= 14.0f);
    printk("Accel self-test %s\n", pass ? "PASSED" : "FAILED");

    imu_wr(dev_addr, ACCEL_CONFIG, accel_8g << 3);
    delay_ms(100);

    return pass;
}

// derive accel samples per second: do N readings and divide by the 
// time it took.
void accel_sps(accel_t *h) {
    // can comment this out.  am curious what rate we get for everyone.
    output("computing samples per second\n");
    uint32_t sum = 0, s, e;
    enum { N = 256 };
    for(int i = 0; i < N; i++) {
        s = timer_get_usec();
        imu_xyz_t xyz_raw = accel_rd(h);
        e = timer_get_usec();

        sum += (e - s);
    }
    uint32_t usec_per_sample = sum / N;
    uint32_t hz = (1000*1000) / usec_per_sample;
    output("usec between readings = %d usec, %d hz\n", usec_per_sample, hz);
}

void notmain(void) {
    delay_ms(100);   // allow time for i2c/device to boot up.
    i2c_init();      // can change this to use the <i2c_init_clk_div> for faster.
    delay_ms(100);   // allow time for i2c/dev to settle after init.

    // from application note: p15
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

    // hard reset: the device won't magically reset when your pi reboots.
    mpu6050_reset(dev_addr);

    // part 1: get the accel working.

    // initialize.
    accel_t h = mpu6050_accel_init(dev_addr, accel_2g);
    assert(h.g==2);
    
    // Perform accelerometer self-test
    if (!accel_self_test(dev_addr)) {
        panic("Accelerometer self-test failed!\n");
    }

    // Re-initialize after self-test
    h = mpu6050_accel_init(dev_addr, accel_2g);
    assert(h.g==2);
    
    // if you want to compute samples per second.
    accel_sps(&h);

    for(int i = 0; i < 100; i++) {
        imu_xyz_t xyz_raw = accel_rd(&h);
        output("reading %d\n", i);
        xyz_print("\traw", xyz_raw);
        xyz_print("\tscaled (milligaus: 1000=1g)", accel_scale(&h, xyz_raw));

        delay_ms(1000);
    }
}
