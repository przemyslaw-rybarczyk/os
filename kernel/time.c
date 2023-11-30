#include "types.h"
#include "time.h"

#include "percpu.h"
#include "process.h"
#include "smp.h"
#include "spinlock.h"

#define STATUS_B_24_HOUR 2
#define STATUS_B_BINARY 4

struct rtc_time {
    u8 second;
    u8 minute;
    u8 hour;
    u8 day;
    u8 month;
    u8 year;
};

static u8 convert_from_bcd(u8 n) {
    return 10 * (n >> 4) + (n & 0x0F);
}

// Number of days in year before start of month
static u16 month_offset[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

// Convert time in the RTC format to a timestamp
i64 convert_time_from_rtc(struct rtc_time rtc_time, u8 status_b) {
    // Check if highest bit of hour is set and clear it
    // Its value is needed if converting from 12-hour format later.
    bool hour_pm = rtc_time.hour & 0x80;
    rtc_time.hour &= 0x7F;
    // If time is in BCD, convert it to binary
    if (!(status_b & STATUS_B_BINARY)) {
        rtc_time.second = convert_from_bcd(rtc_time.second);
        rtc_time.minute = convert_from_bcd(rtc_time.minute);
        rtc_time.hour = convert_from_bcd(rtc_time.hour);
        rtc_time.day = convert_from_bcd(rtc_time.day);
        rtc_time.month = convert_from_bcd(rtc_time.month);
        rtc_time.year = convert_from_bcd(rtc_time.year);
    }
    // If hour is in 12-hour format, convert it to 24-hour format
    if (!(status_b & STATUS_B_24_HOUR)) {
        if (rtc_time.hour == 12)
            rtc_time.hour = 0;
        if (hour_pm)
            rtc_time.hour += 12;
    }
    // Assume year is in the range 2000-2099
    i64 year = 30 + rtc_time.year;
    // Days since epoch
    i64 day =
        year * 365 + (year + 1) / 4
        + month_offset[rtc_time.month - 1]
        + ((year % 4) == 0 && rtc_time.month > 2)
        + rtc_time.day - 1;
    // Seconds since epoch
    i64 second = rtc_time.second + 60 * (rtc_time.minute + 60 * (rtc_time.hour + 24 * day));
    return 10000000 * second;
}

void start_interrupt_timer(u64 tsc_deadline);
void disable_interrupt_timer(void);

spinlock_t wait_queue_lock;

// Doubly linked list of all waiting processes, ordered by timeout
static Process *wait_queue_start = NULL;
static Process *wait_queue_end = NULL;

// Update the interrupt timer after updating the wait queue
// Must be called with wait queue lock held.
static void update_interrupt_timer(void) {
    // Find first process that hasn't been scheduled yet on a different CPU
    Process *first_unscheduled_process = NULL;
    for (Process *p = wait_queue_start; p != NULL; p = p->next_process) {
        if (!p->timeout_scheduled) {
            first_unscheduled_process = p;
            break;
        }
    }
    // Schedule first unscheduled process, timeslice timeout, or keep current process, wichever comes first
    if (first_unscheduled_process != NULL
            && (!cpu_local->timeslice_interrupt_enabled || timestamp_to_tsc(first_unscheduled_process->timeout) < cpu_local->timeslice_timeout)
            && (cpu_local->waiting_process == NULL || cpu_local->waiting_process->timeout > first_unscheduled_process->timeout)) {
        if (cpu_local->waiting_process != NULL)
            cpu_local->waiting_process->timeout_scheduled = false;
        first_unscheduled_process->timeout_scheduled = true;
        cpu_local->waiting_process = first_unscheduled_process;
        start_interrupt_timer(timestamp_to_tsc(first_unscheduled_process->timeout));
    } else if (cpu_local->timeslice_interrupt_enabled
            && (cpu_local->waiting_process == NULL || timestamp_to_tsc(cpu_local->waiting_process->timeout) > cpu_local->timeslice_timeout)) {
        if (cpu_local->waiting_process != NULL)
            cpu_local->waiting_process->timeout_scheduled = false;
        cpu_local->waiting_process = NULL;
        start_interrupt_timer(cpu_local->timeslice_timeout);
    } else if (cpu_local->waiting_process == NULL) {
        disable_interrupt_timer();
    }
}

void schedule_timeslice_interrupt(u64 time) {
    spinlock_acquire(&wait_queue_lock);
    cpu_local->timeslice_interrupt_enabled = true;
    cpu_local->timeslice_timeout = time;
    update_interrupt_timer();
    spinlock_release(&wait_queue_lock);
}

void cancel_timeslice_interrupt(void) {
    spinlock_acquire(&wait_queue_lock);
    cpu_local->timeslice_interrupt_enabled = false;
    update_interrupt_timer();
    spinlock_release(&wait_queue_lock);
}

// Insert current process into wait queue
// Must be called with wait queue lock held.
void wait_queue_insert_current_process(i64 time) {
    Process *p = wait_queue_start;
    for (; p != NULL && p->timeout <= time; p = p->next_process)
        ;
    if (p == NULL) {
        // All processes in queue have larger timeout, so we insert at the end.
        cpu_local->current_process->prev_process = wait_queue_end;
        cpu_local->current_process->next_process = NULL;
        if (wait_queue_end == NULL)
            wait_queue_start = cpu_local->current_process;
        else
            wait_queue_end->next_process = cpu_local->current_process;
        wait_queue_end = cpu_local->current_process;
    } else {
        // p is first process with greater timeout, so we insert before it.
        cpu_local->current_process->next_process = p;
        cpu_local->current_process->prev_process = p->prev_process;
        if (p->prev_process == NULL)
            wait_queue_start = cpu_local->current_process;
        else
            p->prev_process->next_process = cpu_local->current_process;
        p->prev_process = cpu_local->current_process;
    }
    cpu_local->current_process->in_timeout_queue = true;
    cpu_local->current_process->timeout_scheduled = false;
    cpu_local->current_process->timeout = time;
}

// Remove process from wait queue
// Returns true if process was in the queue.
// Must be called with wait queue lock held.
bool wait_queue_remove_process(Process *process) {
    if (process->in_timeout_queue) {
        if (process->prev_process == NULL)
            wait_queue_start = process->next_process;
        else
            process->prev_process->next_process = process->next_process;
        if (process->next_process == NULL)
            wait_queue_end = process->prev_process;
        else
            process->next_process->prev_process = process->prev_process;
        process->in_timeout_queue = false;
        return true;
    } else {
        return false;
    }
}

err_t syscall_process_wait(i64 time) {
    // Early return if we're already past timeout
    if (timestamp_to_tsc(time) <= time_get_tsc())
        return 0;
    spinlock_acquire(&wait_queue_lock);
    // Insert into wait queue
    wait_queue_insert_current_process(time);
    // Update interrupt timer and block until timeout
    update_interrupt_timer();
    process_block(&wait_queue_lock);
    return 0;
}

// Unblock any timed out processes from the wait queue
// Must be called with wait queue lock held.
static void wait_queue_unblock(void) {
    // Get current time
    u64 time = time_get_tsc();
    // Remove and unblock all processes with timeout less than current time
    while (wait_queue_start != NULL && timestamp_to_tsc(wait_queue_start->timeout) <= time) {
        Process *next_process = wait_queue_start->next_process;
        wait_queue_start->timed_out = true;
        wait_queue_start->in_timeout_queue = false;
        process_enqueue(wait_queue_start);
        wait_queue_start = next_process;
        if (next_process == NULL)
            wait_queue_end = NULL;
        else
            next_process->prev_process = NULL;
    }
    // Update interrupt timer
    update_interrupt_timer();
}

void apic_timer_irq_handler(void) {
    apic_eoi();
    // Check that the TSC is actually past the deadline.
    // If it's not, this interrupt is a result of a race condition and we ignore it.
    if (!tsc_past_deadline())
        return;
    // Reset TSC deadline to zero so that later timer interrupts are ignored until the deadline is reset
    cpu_local->tsc_deadline = 0;
    // Delay interrupt if locks are held
    if (cpu_local->preempt_disable != 0 && !cpu_local->idle) {
        cpu_local->timer_interrupt_delayed = true;
        return;
    }
    spinlock_acquire(&wait_queue_lock);
    if (cpu_local->waiting_process == NULL) {
        spinlock_release(&wait_queue_lock);
        // Preempt the current process if there is one
        if (!cpu_local->idle)
            process_switch();
    } else {
        // Unblock timed out processes from the wait queue
        wait_queue_unblock();
        spinlock_release(&wait_queue_lock);
    }
}

void delayed_timer_interrupt_handle(void) {
    spinlock_acquire(&wait_queue_lock);
    if (cpu_local->waiting_process == NULL) {
        spinlock_release(&wait_queue_lock);
        if (!cpu_local->idle)
            process_switch();
    } else {
        wait_queue_unblock();
        spinlock_release(&wait_queue_lock);
    }
}
