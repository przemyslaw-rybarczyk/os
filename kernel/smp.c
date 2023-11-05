#include "types.h"
#include "smp.h"

#include "percpu.h"
#include "process.h"
#include "time.h"

void apic_timer_irq_handler(void) {
    apic_eoi();
    // Check that the TSC is actually past the deadline.
    // If it's not, this interrupt is a result of a race condition and we ignore it.
    if (!tsc_past_deadline())
        return;
    // Try to preempt the current process
    // If preemption is disabled, mark the preemption as delayed
    if (cpu_local->preempt_disable == 0)
        process_switch();
    else
        cpu_local->preempt_delayed = true;
}
