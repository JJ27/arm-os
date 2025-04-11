#include "rpi.h"

unsigned read_until_timeout(int pin, int v, unsigned timeout) {
    unsigned initial_time = timer_get_usec_raw();
    while (1) {
        if (gpio_read(pin) == v) {
            return timer_get_usec_raw() - initial_time + 1;
        }
        if (timer_get_usec_raw() - initial_time >= timeout) {
            return 0;
        }
    }
}

void read_trial(int pin, unsigned timeout) {
    while(gpio_read(pin) == 1);

    unsigned v = 1;
    
    struct read {
        uint32_t usec;
        uint32_t v;
    } r[255];

    unsigned x = 0;
    unsigned t = 0;
    for (int i = 0; i < 255; i++) {
        if (!(t = read_until_timeout(pin, v, timeout))) {
            r[i].usec = timeout;
            r[i].v = !v;
            x = i;
            break;
        }
        r[i].usec = t;
        r[i].v = 1 - v;
        v = 1 - v;
        x = i;
    }

    unsigned discr = (600 + 1600) / 2;
    char *left = "1000000001111111100010000111011111";
    char *right = "1000000001111111101011010101001011";
    char *up = "1000000001111111100011000111001111";
    char *down = "1000000001111111101001010101101011";
    char *ok = "1000000001111111100111000110001111";
    char *star = "1000000001111111101101000100101111";
    char *pound = "1000000001111111110110000010011111";

    char *s = kmalloc(x/2 + 1);
    for (int i = 0; i < x + 1; i++) {
        //output("%d: v=%d: usec=%d ", i, r[i].v, r[i].usec);
        if (r[i].v == 1) {
            s[i/2] = (r[i].usec > discr) ? '1' : '0';
        }
        // if (i % 2 == 1) {
        //     output("\n");
        // }
    }
    //output("Total: %s\n", s);
    if (strcmp(s, left) == 0) {
        output("Left\n");
    } else if (strcmp(s, right) == 0) {
        output("Right\n");
    } else if (strcmp(s, up) == 0) {
        output("Up\n");
    } else if (strcmp(s, down) == 0) {
        output("Down\n");
    } else if (strcmp(s, ok) == 0) {
        output("OK\n");
    } else if (strcmp(s, star) == 0) {
        output("*\n");
    } else if (strcmp(s, pound) == 0) {
        output("#\n");
    }
}

void notmain(void) {
    kmalloc_init(1024);
    enum { 
        input = 21,         // input pin: "S" on IR
        timeout = 40000,   // timeout in usec
     };

    gpio_set_input(input);
    gpio_set_pullup(input);     
    assert(gpio_read(input) == 1);

   while (1) {
        read_trial(input, timeout);
    }
}