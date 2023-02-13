#include "types.h"
#include "pit.h"

#include "framebuffer.h"
#include "process.h"

// Number of PIT cycles (1 ms each) that have passes since system start
static u64 cycle_count = 0;

void pit_irq_handler(void) {
    cycle_count++;
    // Write EOI to master PIC data port
    asm volatile ("out 0x20, al" : : "a"(0x20));
    sched_yield();
}
