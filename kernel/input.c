#include "types.h"
#include "input.h"

#include "ahci.h"
#include "channel.h"
#include "interrupt.h"
#include "smp.h"
#include "spinlock.h"
#include "percpu.h"

#include <stdatomic.h>

#define INPUT_EVENT_QUEUE_SIZE 16

Channel *keyboard_key_channel;
Channel *mouse_button_channel;
Channel *mouse_move_channel;
Channel *mouse_scroll_channel;

// Set if there are events in the queue waiting to be sent or drive IRQ waiting to be processed
volatile atomic_bool send_input_delayed;

// Set if there is a drive IRQ waiting to be processed
static volatile atomic_bool drive_event_delayed = false;

// Queue holding unsent input events
static InputEvent input_event_queue[INPUT_EVENT_QUEUE_SIZE];
static volatile atomic_size_t input_event_queue_size = 0;
static spinlock_t input_event_queue_lock;

// Add an input event to the queue and send it if possible
// May be called from an interrupt handler.
void add_input_event(InputEvent event) {
    // Skip the event if the queue is already full
    if (input_event_queue_size >= INPUT_EVENT_QUEUE_SIZE)
        return;
    // Add the event ot the queue
    interrupt_disable();
    spinlock_acquire(&input_event_queue_lock);
    input_event_queue[input_event_queue_size++] = event;
    spinlock_release(&input_event_queue_lock);
    interrupt_enable();
    // If no locks are held, send all events
    // Otherwise, set flag so they are sent later to avoid deadlock
    if (cpu_local->idle || cpu_local->preempt_disable == 0)
        send_input_events();
    else
        send_input_delayed = true;
}

void ahci_irq_handler(void) {
    if (cpu_local->idle || cpu_local->preempt_disable == 0) {
        drive_process_irq();
    } else {
        drive_event_delayed = true;
        send_input_delayed = true;
    }
    apic_eoi();
}

// Send all input events in the queue
void send_input_events(void) {
    err_t err;
    send_input_delayed = false;
    if (drive_event_delayed) {
        drive_event_delayed = false;
        drive_process_irq();
    }
    // Early return when queue is empty to avoid contesting the queue lock
    if (input_event_queue_size == 0)
        return;
    // Send all the events
    interrupt_disable();
    spinlock_acquire(&input_event_queue_lock);
    size_t old_input_event_queue_size = input_event_queue_size;
    input_event_queue_size = 0;
    for (size_t i = 0; i < old_input_event_queue_size; i++) {
        Channel *channel = NULL;
        Message *message = NULL;
        switch (input_event_queue[i].type) {
        case INPUT_EVENT_KEY:
            channel = keyboard_key_channel;
            message = message_alloc_copy(sizeof(KeyEvent), &input_event_queue[i].key_event);
            break;
        case INPUT_EVENT_MOUSE_BUTTON:
            channel = mouse_button_channel;
            message = message_alloc_copy(sizeof(MouseButtonEvent), &input_event_queue[i].mouse_button_event);
            break;
        case INPUT_EVENT_MOUSE_MOVE:
            channel = mouse_move_channel;
            message = message_alloc_copy(sizeof(MouseMoveEvent), &input_event_queue[i].mouse_move_event);
            break;
        case INPUT_EVENT_MOUSE_SCROLL:
            channel = mouse_scroll_channel;
            message = message_alloc_copy(sizeof(MouseScrollEvent), &input_event_queue[i].mouse_scroll_event);
            break;
        }
        if (message == NULL)
            continue;
        err = channel_send(channel, message, true);
        if (err)
            message_free(message);
    }
    spinlock_release(&input_event_queue_lock);
    interrupt_enable();
}
