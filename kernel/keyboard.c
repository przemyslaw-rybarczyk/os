#include "types.h"
#include "keyboard.h"

#include "alloc.h"
#include "channel.h"
#include "interrupt.h"
#include "process.h"
#include "smp.h"
#include "string.h"

#include <zr/keyboard.h>

Process *keyboard_kernel_thread;
Channel *keyboard_channel;

// The buffer holds up to one KeyEvent.
// It is written to by the interrupt handler.
static bool keyboard_buffer_full = false;

static bool waiting_for_key_event = false;
static KeyEvent keyboard_buffer;

_Noreturn void keyboard_kernel_thread_main(void) {
    err_t err;
    while (1) {
        // Block until a key event occurs and read it
        interrupt_disable();
        if (!keyboard_buffer_full) {
            waiting_for_key_event = true;
            process_block(NULL);
        }
        keyboard_buffer_full = false;
        KeyEvent event = keyboard_buffer;
        interrupt_enable();
        // Send the key event in a message
        Message *message = message_alloc(sizeof(KeyEvent), &event);
        if (message == NULL)
            continue;
        err = channel_call(keyboard_channel, message, NULL);
        if (err)
            message_free(message);
    }
}

// Write an event to the keyboard buffer
static void keyboard_buffer_write(Keycode code, bool pressed) {
    if (!keyboard_buffer_full) {
        // Write to the buffer
        keyboard_buffer = (KeyEvent){code, pressed};
        keyboard_buffer_full = true;
        // If the kernel thread is waiting, unblock it
        if (waiting_for_key_event) {
            waiting_for_key_event = false;
            process_enqueue(keyboard_kernel_thread);
        }
    }
}

#define KEY_NONE (Keycode)(-1)

static const Keycode short_keycodes[] = {
         KEY_NONE,          KEY_F9,       KEY_NONE,            KEY_F5,           KEY_F3,        KEY_F1,          KEY_F2,      KEY_F12,
         KEY_NONE,         KEY_F10,         KEY_F8,            KEY_F6,           KEY_F4,       KEY_TAB,       KEY_GRAVE,     KEY_NONE,
         KEY_NONE,    KEY_LEFT_ALT, KEY_LEFT_SHIFT,          KEY_NONE,    KEY_LEFT_CTRL,         KEY_Q,           KEY_1,     KEY_NONE,
         KEY_NONE,        KEY_NONE,          KEY_Z,             KEY_S,            KEY_A,         KEY_W,           KEY_2,     KEY_NONE,
         KEY_NONE,           KEY_C,          KEY_X,             KEY_D,            KEY_E,         KEY_4,           KEY_3,     KEY_NONE,
         KEY_NONE,       KEY_SPACE,          KEY_V,             KEY_F,            KEY_T,         KEY_R,           KEY_5,     KEY_NONE,
         KEY_NONE,           KEY_N,          KEY_B,             KEY_H,            KEY_G,         KEY_Y,           KEY_6,     KEY_NONE,
         KEY_NONE,        KEY_NONE,          KEY_M,             KEY_J,            KEY_U,         KEY_7,           KEY_8,     KEY_NONE,
         KEY_NONE,       KEY_COMMA,          KEY_K,             KEY_I,            KEY_O,         KEY_0,           KEY_9,     KEY_NONE,
         KEY_NONE,      KEY_PERIOD,      KEY_SLASH,             KEY_L,    KEY_SEMICOLON,         KEY_P,       KEY_MINUS,     KEY_NONE,
         KEY_NONE,        KEY_NONE, KEY_APOSTROPHE,          KEY_NONE, KEY_LEFT_BRACKET,    KEY_EQUALS,        KEY_NONE,     KEY_NONE,
    KEY_CAPS_LOCK, KEY_RIGHT_SHIFT,      KEY_ENTER, KEY_RIGHT_BRACKET,         KEY_NONE, KEY_BACKSLASH,        KEY_NONE,     KEY_NONE,
         KEY_NONE,        KEY_NONE,       KEY_NONE,          KEY_NONE,         KEY_NONE,      KEY_NONE,   KEY_BACKSPACE,     KEY_NONE,
         KEY_NONE,        KEY_KP_1,       KEY_NONE,          KEY_KP_4,         KEY_KP_7,      KEY_NONE,        KEY_NONE,     KEY_NONE,
         KEY_KP_0,   KEY_KP_PERIOD,       KEY_KP_2,          KEY_KP_5,         KEY_KP_6,      KEY_KP_8,      KEY_ESCAPE, KEY_NUM_LOCK,
          KEY_F11,     KEY_KP_PLUS,       KEY_KP_3,      KEY_KP_MINUS,  KEY_KP_ASTERISK,      KEY_KP_9, KEY_SCROLL_LOCK,     KEY_NONE,
         KEY_NONE,        KEY_NONE,       KEY_NONE,            KEY_F7,
};

static const Keycode long_keycodes[] = {
      KEY_NONE,      KEY_NONE,      KEY_NONE, KEY_NONE,       KEY_NONE,    KEY_NONE, KEY_NONE,       KEY_NONE,
      KEY_NONE,      KEY_NONE,      KEY_NONE, KEY_NONE,       KEY_NONE,    KEY_NONE, KEY_NONE,       KEY_NONE,
      KEY_NONE, KEY_RIGHT_ALT,      KEY_NONE, KEY_NONE, KEY_RIGHT_CTRL,    KEY_NONE, KEY_NONE,       KEY_NONE,
      KEY_NONE,      KEY_NONE,      KEY_NONE, KEY_NONE,       KEY_NONE,    KEY_NONE, KEY_NONE,  KEY_LEFT_META,
      KEY_NONE,      KEY_NONE,      KEY_NONE, KEY_NONE,       KEY_NONE,    KEY_NONE, KEY_NONE, KEY_RIGHT_META,
      KEY_NONE,      KEY_NONE,      KEY_NONE, KEY_NONE,       KEY_NONE,    KEY_NONE, KEY_NONE,       KEY_MENU,
      KEY_NONE,      KEY_NONE,      KEY_NONE, KEY_NONE,       KEY_NONE,    KEY_NONE, KEY_NONE,       KEY_NONE,
      KEY_NONE,      KEY_NONE,      KEY_NONE, KEY_NONE,       KEY_NONE,    KEY_NONE, KEY_NONE,       KEY_NONE,
      KEY_NONE,      KEY_NONE,      KEY_NONE, KEY_NONE,       KEY_NONE,    KEY_NONE, KEY_NONE,       KEY_NONE,
      KEY_NONE,      KEY_NONE,  KEY_KP_SLASH, KEY_NONE,       KEY_NONE,    KEY_NONE, KEY_NONE,       KEY_NONE,
      KEY_NONE,      KEY_NONE,      KEY_NONE, KEY_NONE,       KEY_NONE,    KEY_NONE, KEY_NONE,       KEY_NONE,
      KEY_NONE,      KEY_NONE,  KEY_KP_ENTER, KEY_NONE,       KEY_NONE,    KEY_NONE, KEY_NONE,       KEY_NONE,
      KEY_NONE,      KEY_NONE,      KEY_NONE, KEY_NONE,       KEY_NONE,    KEY_NONE, KEY_NONE,       KEY_NONE,
      KEY_NONE,       KEY_END,      KEY_NONE, KEY_LEFT,       KEY_HOME,    KEY_NONE, KEY_NONE,       KEY_NONE,
    KEY_INSERT,    KEY_DELETE,      KEY_DOWN, KEY_NONE,      KEY_RIGHT,      KEY_UP, KEY_NONE,       KEY_NONE,
      KEY_NONE,      KEY_NONE, KEY_PAGE_DOWN, KEY_NONE,       KEY_NONE, KEY_PAGE_UP,
};

// Since each key event produces a scan code that may consist of several bytes,
// and each byte is sent through a separate interrupt, the keyboard driver is implemented
// as a state machine where each interrupt updates the state based on the byte received.
// Each state then corresponds to a partially received scan code.
// Most keys have either "short" or "long" keycodes. Short keycodes are:
//   [byte] for key press,
//   F0 [byte] for key release.
// Long keycodes are:
//   E0 [byte] for key press,
//   E0 F0 [byte] for key release.
// The mappings from final bytes to keycodes are found in the `short_keycodes` and `long_keycodes` arrays.
// Bytes past the size of the arrays are all `KEY_NONE`. `KEY_NONE` means that the scan code is not recognized.
// Is that case, the byte is ignored and the driver returns to the initial state.
// Two keys have special scan codes:
//   E0 12 E0 7C for print screen key press,
//   E0 F0 7C E0 F0 12 for print screen key release,
//   E1 14 77 E1 F0 14 F0 77 for pause key press.
// There is no scan code for pause key release.
typedef enum KeyboardState {
    KBST_START,
    KBST_RELEASE,
    KBST_LONG,
    KBST_LONG_RELEASE,
    KBST_PRINT_SCREEN_2,
    KBST_PRINT_SCREEN_3,
    KBST_PRINT_SCREEN_RELEASE_3,
    KBST_PRINT_SCREEN_RELEASE_4,
    KBST_PRINT_SCREEN_RELEASE_5,
    KBST_PAUSE_1,
    KBST_PAUSE_2,
    KBST_PAUSE_3,
    KBST_PAUSE_4,
    KBST_PAUSE_5,
    KBST_PAUSE_6,
    KBST_PAUSE_7,
} KeyboardState;

static KeyboardState keyboard_state = KBST_START;

void keyboard_irq_handler(void) {
    // Read code byte from PS/2 data port
    u8 byte;
    asm volatile ("in al, 0x60" : "=a"(byte));
    // Change the state of the keyboard and put the keycode in the buffer if a complete scan code is received
    switch (keyboard_state) {
    case KBST_START:
        switch (byte) {
        case 0xF0:
            keyboard_state = KBST_RELEASE;
            break;
        case 0xE0:
            keyboard_state = KBST_LONG;
            break;
        case 0xE1:
            keyboard_state = KBST_PAUSE_1;
            break;
        default:
            if (byte < sizeof(short_keycodes) / sizeof(short_keycodes[0]) && short_keycodes[byte] != KEY_NONE)
                keyboard_buffer_write(short_keycodes[byte], true);
            keyboard_state = KBST_START;
            break;
        }
        break;
    case KBST_RELEASE:
        if (byte < sizeof(short_keycodes) / sizeof(short_keycodes[0]) && short_keycodes[byte] != KEY_NONE)
            keyboard_buffer_write(short_keycodes[byte], false);
        keyboard_state = KBST_START;
        break;
    case KBST_LONG:
        switch (byte) {
        case 0xF0:
            keyboard_state = KBST_LONG_RELEASE;
            break;
        case 0x12:
            keyboard_state = KBST_PRINT_SCREEN_2;
            break;
        default:
            if (byte < sizeof(long_keycodes) / sizeof(long_keycodes[0]) && long_keycodes[byte] != KEY_NONE)
                keyboard_buffer_write(long_keycodes[byte], true);
            keyboard_state = KBST_START;
            break;
        }
        break;
    case KBST_LONG_RELEASE:
        switch (byte) {
        case 0x7C:
            keyboard_state = KBST_PRINT_SCREEN_RELEASE_3;
            break;
        default:
            if (byte < sizeof(long_keycodes) / sizeof(long_keycodes[0]) && long_keycodes[byte] != KEY_NONE)
                keyboard_buffer_write(long_keycodes[byte], false);
            keyboard_state = KBST_START;
            break;
        }
        break;
    case KBST_PRINT_SCREEN_2:
        keyboard_state = byte == 0xE0 ? KBST_PRINT_SCREEN_3 : KBST_START;
        break;
    case KBST_PRINT_SCREEN_3:
        if (byte == 0x7C)
            keyboard_buffer_write(KEY_PRINT_SCREEN, true);
        keyboard_state = KBST_START;
        break;
    case KBST_PRINT_SCREEN_RELEASE_3:
        keyboard_state = byte == 0xE0 ? KBST_PRINT_SCREEN_RELEASE_4 : KBST_START;
        break;
    case KBST_PRINT_SCREEN_RELEASE_4:
        keyboard_state = byte == 0xF0 ? KBST_PRINT_SCREEN_RELEASE_5 : KBST_START;
        break;
    case KBST_PRINT_SCREEN_RELEASE_5:
        if (byte == 0x12)
            keyboard_buffer_write(KEY_PRINT_SCREEN, false);
        keyboard_state = KBST_START;
        break;
    case KBST_PAUSE_1:
        keyboard_state = byte == 0x14 ? KBST_PAUSE_2 : KBST_START;
        break;
    case KBST_PAUSE_2:
        keyboard_state = byte == 0x77 ? KBST_PAUSE_3 : KBST_START;
        break;
    case KBST_PAUSE_3:
        keyboard_state = byte == 0xE1 ? KBST_PAUSE_4 : KBST_START;
        break;
    case KBST_PAUSE_4:
        keyboard_state = byte == 0xF0 ? KBST_PAUSE_5 : KBST_START;
        break;
    case KBST_PAUSE_5:
        keyboard_state = byte == 0x14 ? KBST_PAUSE_6 : KBST_START;
        break;
    case KBST_PAUSE_6:
        keyboard_state = byte == 0xF0 ? KBST_PAUSE_7 : KBST_START;
        break;
    case KBST_PAUSE_7:
        if (byte == 0x77)
            keyboard_buffer_write(KEY_PAUSE, true);
        keyboard_state = KBST_START;
        break;
    }
    apic_eoi();
}
