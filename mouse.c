#include "types.h"
#include "mouse.h"

#define MOUSE_PACKET_LEFT_BUTTON (1 << 0)
#define MOUSE_PACKET_RIGHT_BUTTON (1 << 1)
#define MOUSE_PACKET_MIDDLE_BUTTON (1 << 2)
#define MOUSE_PACKET_VALID (1 << 3)
#define MOUSE_PACKET_X_SIGN_BIT (1 << 4)
#define MOUSE_PACKET_Y_SIGN_BIT (1 << 5)

// Contains cumulative changes in mouse position since the last time the mouse was polled.
static volatile MouseUpdate mouse_update;

MouseUpdate mouse_get_update(void) {
    // We disable interrupts while accessing mouse update data to avoid conflict with the interrupt handler.
    asm volatile ("cli");
    MouseUpdate r = mouse_update;
    // Reset movement change
    mouse_update.diff_x = 0;
    mouse_update.diff_y = 0;
    mouse_update.diff_scroll = 0;
    asm volatile ("sti");
    return r;
}

// If the mouse has a scroll wheel, we will receive an additional byte in each packet.
// This variable is set by the mouse initialization function.
bool mouse_has_scroll_wheel;

// Since each byte of a mouse event packet comes in a separate IRQ,
// we keep track of how many bytes we have received so far.
static u32 bytes_received = 0;

// Used to store incoming the incoming mouse packet by interrupts
// The data is then used to update `mouse_update` after the mouse packet is fully received.
static u8 mouse_packet[4];

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
    // If we got the full packet, we update `mouse_update` and prepare for receiving the next one
    // We add x but subtract y because the coordinates used by the mouse have the y axis pointing down,
    // unlike the one we use, which points up.
    if (bytes_received == packet_size) {
        i32 diff_x = mouse_packet[1];
        if (mouse_packet[0] & MOUSE_PACKET_X_SIGN_BIT)
            diff_x -= 256;
        mouse_update.diff_x += diff_x;
        i32 diff_y = mouse_packet[2];
        if (mouse_packet[0] & MOUSE_PACKET_Y_SIGN_BIT)
            diff_y -= 256;
        mouse_update.diff_y -= diff_y;
        mouse_update.left_button_pressed = (bool)(mouse_packet[0] & MOUSE_PACKET_LEFT_BUTTON);
        mouse_update.right_button_pressed = (bool)(mouse_packet[0] & MOUSE_PACKET_RIGHT_BUTTON);
        mouse_update.middle_button_pressed = (bool)(mouse_packet[0] & MOUSE_PACKET_MIDDLE_BUTTON);
        if (mouse_has_scroll_wheel) {
            switch (mouse_packet[3] & 0x0F) {
            case 0x01:
                mouse_update.diff_scroll += 1;
                break;
            case 0x0F:
                mouse_update.diff_scroll -= 1;
                break;
            }
        }
        bytes_received = 0;
    }
    // Write EOI to slave and master PIC data ports
    asm volatile (
        "out 0xA0, al;"
        "out 0x20, al;"
        : : "a"(0x20)
    );
}
