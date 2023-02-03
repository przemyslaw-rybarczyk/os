#include "types.h"
#include "pit.h"

#include "framebuffer.h"

// Number of PIT cycles (1 ms each) that have passes since system start
static u64 cycle_count = 0;

void pit_irq_handler(void) {
    cycle_count++;
    if (cycle_count % 1000 == 0) {
        print_hex(cycle_count / 1000, 8);
        print_string(" seconds have elapsed since system start\n");
    }
    // Write EOI to master PIC data port
    asm volatile ("out 0x20, al" : : "a"(0x20));
}
