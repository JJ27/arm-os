#include "stepper.h"
#include "rpi.h"
#include "math-helpers.h"

stepper_t * stepper_init(unsigned dir, unsigned step){
    // kmalloc_init(1024);
    stepper_t * stepper = kmalloc(sizeof(stepper_t));
    
    // Initialize the stepper structure
    stepper->step_count = 0;
    stepper->dir = dir;
    stepper->step = step;
    
    // Set up GPIO pins
    gpio_set_output(dir);
    gpio_set_output(step);
    
    // Initialize both pins to 0
    gpio_write(dir, 0);
    gpio_write(step, 0);

    return stepper;
}

// If you want to do microstep extension:
void stepper_set_microsteps(stepper_t * stepper, stepper_microstep_mode_t microstep_mode){
    unimplemented();
}

// If you want to do microstep extension:
stepper_t * stepper_init_with_microsteps(unsigned dir, unsigned step, unsigned MS1, unsigned MS2, unsigned MS3, stepper_microstep_mode_t microstep_mode){
    unimplemented();
}

// how many gpio writes should you do?
void stepper_step_forward(stepper_t * stepper){
    // Set direction to forward (1)
    gpio_write(stepper->dir, 1);
    
    // Step sequence: 1-0-1
    gpio_write(stepper->step, 1);
    delay_us(100);  // Small delay to ensure step is registered
    gpio_write(stepper->step, 0);
    
    stepper->step_count++;
}

void stepper_step_backward(stepper_t * stepper){
    // Set direction to backward (0)
    gpio_write(stepper->dir, 0);
    
    // Step sequence: 1-0-1
    gpio_write(stepper->step, 1);
    delay_us(100);  // Small delay to ensure step is registered
    gpio_write(stepper->step, 0);
    
    stepper->step_count--;
}

int stepper_get_position_in_steps(stepper_t * stepper){
    return stepper->step_count;
}
