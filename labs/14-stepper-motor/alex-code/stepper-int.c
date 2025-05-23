#include "stepper-int.h"
#include "timer-interrupt.h"
#include "cycle-count.h"
#include "rpi-inline-asm.h"

// you can/should play around with this
#define STEPPER_INT_TIMER_INT_PERIOD 100 

static int first_init = 1;

#define MAX_STEPPERS 16
static stepper_int_t * my_steppers[MAX_STEPPERS];
static unsigned num_steppers = 0;

void stepper_int_handler(unsigned pc) {
    // check and clear timer interrupt
    dev_barrier();
    unsigned pending = GET32(IRQ_basic_pending);
    if((pending & RPI_BASIC_ARM_TIMER_IRQ) == 0)
        return;
    PUT32(ARM_Timer_IRQ_Clear, 1);
    dev_barrier();  

    unsigned current_time = timer_get_usec();
    
    // Process each stepper
    for(unsigned i = 0; i < num_steppers; i++) {
        stepper_int_t *stepper = my_steppers[i];
        if(stepper->status != IN_JOB || Q_empty(&stepper->positions_Q))
            continue;
            
        stepper_position_t *current_pos = Q_start(&stepper->positions_Q);
        
        // Initialize position if not started
        if(current_pos->status == NOT_STARTED) {
            current_pos->usec_at_prev_step = current_time;
            current_pos->status = STARTED;
        }
        
        // Check if it's time to step
        if(current_time >= current_pos->usec_at_prev_step + current_pos->usec_between_steps) {
            int current_pos_steps = stepper_int_get_position_in_steps(stepper);
            
            // Step in appropriate direction
            if(current_pos_steps < current_pos->goal_steps) {
                stepper_step_forward(stepper->stepper);
            } else if(current_pos_steps > current_pos->goal_steps) {
                stepper_step_backward(stepper->stepper);
            }
            
            // Update last step time
            current_pos->usec_at_prev_step = current_time;
            
            // Check if we've reached the goal
            if(current_pos_steps == current_pos->goal_steps) {
                current_pos->status = FINISHED;
                Q_pop(&stepper->positions_Q);
                
                // If queue is empty, mark stepper as free
                if(Q_empty(&stepper->positions_Q)) {
                    stepper->status = NOT_IN_JOB;
                }
            }
        }
    }
}

void interrupt_vector(unsigned pc){
    stepper_int_handler(pc);
}

stepper_int_t * stepper_init_with_int(unsigned dir, unsigned step){
    if(num_steppers == MAX_STEPPERS){
        return NULL;
    }
    //kmalloc_init(1024);

    stepper_int_t *stepper = kmalloc(sizeof *stepper);
    stepper->stepper = stepper_init(dir, step);
    stepper->status = NOT_IN_JOB;
    stepper->positions_Q.head = 0;
    stepper->positions_Q.tail = 0;
    stepper->positions_Q.cnt = 0;
    
    // Add to steppers array
    my_steppers[num_steppers++] = stepper;

    //initialize interrupts; only do once, on the first init
    if(first_init){
        first_init = 0;
        interrupt_init();
        cycle_cnt_init();
        // prescale = 1.
        timer_init(1, STEPPER_INT_TIMER_INT_PERIOD);
        cpsr_int_enable();
    }

    return stepper;
}

stepper_int_t * stepper_int_init_with_microsteps(unsigned dir, unsigned step, unsigned MS1, unsigned MS2, unsigned MS3, stepper_microstep_mode_t microstep_mode){
    unimplemented();
}

stepper_position_t * stepper_int_enqueue_pos(stepper_int_t * stepper, int goal_steps, unsigned usec_between_steps){
    // Create new position
    stepper_position_t *new_pos = kmalloc(sizeof(stepper_position_t));
    new_pos->goal_steps = goal_steps;
    new_pos->usec_between_steps = usec_between_steps;
    new_pos->status = NOT_STARTED;
    new_pos->next = 0;  // Initialize next pointer
    
    // Add to queue (FIFO - append to tail)
    Q_append(&stepper->positions_Q, new_pos);
    
    // Mark stepper as busy if it wasn't already
    if(stepper->status == NOT_IN_JOB) {
        stepper->status = IN_JOB;
    }
    
    return new_pos;
}

int stepper_int_get_position_in_steps(stepper_int_t * stepper){
    return stepper_get_position_in_steps(stepper->stepper);
}

int stepper_int_is_free(stepper_int_t * stepper){
    return stepper->status == NOT_IN_JOB;
}

int stepper_int_position_is_complete(stepper_position_t * pos){
    return pos->status == FINISHED;
}

