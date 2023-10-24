#include "types.h"

#include "alloc.h"
#include "interrupt.h"
#include "input.h"
#include "process.h"
#include "smp.h"
#include "string.h"

#define MOUSE_PACKET_LEFT_BUTTON (1 << 0)
#define MOUSE_PACKET_RIGHT_BUTTON (1 << 1)
#define MOUSE_PACKET_MIDDLE_BUTTON (1 << 2)
#define MOUSE_PACKET_VALID (1 << 3)
#define MOUSE_PACKET_X_SIGN_BIT (1 << 4)
#define MOUSE_PACKET_Y_SIGN_BIT (1 << 5)

// If the mouse has a scroll wheel, we will receive an additional byte in each packet.
// This variable is set by the mouse initialization function.
bool mouse_has_scroll_wheel;

// Since each byte of a mouse event packet comes in a separate IRQ,
// we keep track of how many bytes we have received so far.
static u32 bytes_received = 0;

// Used to store incoming the incoming mouse packet by interrupts
// The data is then used for event message contents after the mouse packet is fully received.
static u8 mouse_packet[4];

// We need to keep track of which buttons are held so we can emit events when the state changes
static bool left_button_pressed;
static bool right_button_pressed;
static bool middle_button_pressed;

void mouse_irq_handler(void) {
    // Get the next byte of the mouse packet
    asm volatile ("in al, 0x60" : "=a"(mouse_packet[bytes_received]));
    bytes_received++;
    // If the first byte has its bit 3 clear, we discard it
    if (bytes_received == 1 && !(mouse_packet[0] & MOUSE_PACKET_VALID)) {
        bytes_received = 0;
    }
    // If the mouse has a scroll wheel, we expect an additional byte.
    u32 packet_size = mouse_has_scroll_wheel ? 4 : 3;
    // If we got the full packet, we send the events and prepare for receiving the next one
    // We add x but subtract y because the coordinates used by the mouse have the y axis pointing down,
    // unlike the one we use, which points up.
    if (bytes_received == packet_size) {
        i32 diff_x = mouse_packet[1];
        if (mouse_packet[0] & MOUSE_PACKET_X_SIGN_BIT)
            diff_x -= 256;
        i32 diff_y = mouse_packet[2];
        if (mouse_packet[0] & MOUSE_PACKET_Y_SIGN_BIT)
            diff_y -= 256;
        diff_y *= -1;
        if (diff_x != 0 || diff_y != 0)
            add_input_event((InputEvent){INPUT_EVENT_MOUSE_MOVE, .mouse_move_event = {diff_x, diff_y, 0, 0}});
        bool new_left_button_pressed = (bool)(mouse_packet[0] & MOUSE_PACKET_LEFT_BUTTON);
        bool new_right_button_pressed = (bool)(mouse_packet[0] & MOUSE_PACKET_RIGHT_BUTTON);
        bool new_middle_button_pressed = (bool)(mouse_packet[0] & MOUSE_PACKET_MIDDLE_BUTTON);
        if (new_left_button_pressed != left_button_pressed)
            add_input_event((InputEvent){INPUT_EVENT_MOUSE_BUTTON, .mouse_button_event = {MOUSE_BUTTON_LEFT, new_left_button_pressed}});
        if (new_right_button_pressed != right_button_pressed)
            add_input_event((InputEvent){INPUT_EVENT_MOUSE_BUTTON, .mouse_button_event = {MOUSE_BUTTON_RIGHT, new_right_button_pressed}});
        if (new_middle_button_pressed != middle_button_pressed)
            add_input_event((InputEvent){INPUT_EVENT_MOUSE_BUTTON, .mouse_button_event = {MOUSE_BUTTON_MIDDLE, new_middle_button_pressed}});
        if (mouse_has_scroll_wheel) {
            switch (mouse_packet[3] & 0x0F) {
            case 0x01:
                add_input_event((InputEvent){INPUT_EVENT_MOUSE_SCROLL, .mouse_scroll_event = {1}});
                break;
            case 0x0F:
                add_input_event((InputEvent){INPUT_EVENT_MOUSE_SCROLL, .mouse_scroll_event = {-1}});
                break;
            }
        }
        bytes_received = 0;
    }
    apic_eoi();
}
