#include "rpi.h"
#include "mbox.h"

uint32_t rpi_temp_get(void) ;

#include "cycle-count.h"

// compute cycles per second using
//  - cycle_cnt_read();
//  - timer_get_usec();
unsigned cyc_per_sec(void) {
    //todo("implement this!\n");
    unsigned start = cycle_cnt_read();
    delay_us(1000000);
    return cycle_cnt_read() - start;
}


void notmain(void) { 
    output("mailbox serial number = %llx\n", rpi_get_serialnum());
    //todo("implement the rest");

    output("mailbox revision number = %x\n", rpi_get_revision());
    output("mailbox model number = %x\n", rpi_get_model());

    uint32_t size = rpi_get_memsize();
    output("mailbox physical mem: size=%d (%dMB)\n", 
            size, 
            size/(1024*1024));

    // print as fahrenheit
    unsigned x = rpi_temp_get();

    // convert <x> to C and F
    unsigned C = (x / 1000), F = (C * 9 / 5) + 32;
    output("mailbox temp = %x, C=%d F=%d\n", x, C, F); 

    unsigned max = rpi_temp_max();
    output("mailbox max temp = %d\n", max);

    //todo("do overclocking!\n");
    int i = 0;
    int temp = 0;
    do {
        unsigned cyc = cyc_per_sec();
        output("cyc_per_sec = %d\n", cyc);
        output("new clock rate = %d\n", 700 + (100 * i));
        rpi_set_clock_rate(3, 700000000 + (100000000 * i), 1);
        delay_us(5000000);
        temp = rpi_temp_get();
        output("temp = %d\n", (temp / 1000) * 9 / 5 + 32);
        i++;
    } while(temp < max - 9000);
}