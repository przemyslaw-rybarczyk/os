#include "types.h"
#include "pit.h"

#include "percpu.h"
#include "process.h"
#include "smp.h"

void pit_irq_handler(void) {
    apic_eoi();
    if (cpu_local->preempt_disable == 0)
        sched_yield();
}
