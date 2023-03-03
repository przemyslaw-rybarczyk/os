#include "types.h"
#include "pit.h"

#include "process.h"
#include "smp.h"

void pit_irq_handler(void) {
    apic_eoi();
    sched_yield();
}
