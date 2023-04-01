#include "types.h"
#include "pit.h"

#include "percpu.h"
#include "process.h"
#include "smp.h"

void pit_irq_handler(void) {
    apic_eoi();
    // Try to preempt the current process
    // If preemption is disabled, mark the preemption as delayed
    if (cpu_local->preempt_disable == 0)
        sched_yield();
    else
        cpu_local->preempt_delayed = true;
}
