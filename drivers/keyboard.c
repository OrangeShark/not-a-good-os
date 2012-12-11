#include <kernel/bitops.h>
#include <kernel/ctype.h>
#include <kernel/keyboard.h>
#include <kernel/panic.h>
#include <kernel/stdio.h>
#include <kernel/string.h>

#include <asm/interrupt.h>

#define KEYBOARD_IRQ    IRQ1

#define KEY_RELEASED_BIT    7
#define KEY_RELEASED        (1 << KEY_RELEASED_BIT)

#define KEYBOARD_DATA_PORT      0x60
#define KEYBOARD_CMD_PORT       0x64
#define KEYBOARD_BUSY           0x02
#define KEYBOARD_SET_LEDS       0xED
#define KEYBOARD_DISABLE        0xAD
#define KEYBOARD_ENABLE         0xAE
#define KEYBOARD_ACKNOWLEDGE    0xFA
#define KEYBOARD_RESEND         0xFE
#define KEY_EXTEND_BYTE         0xE0

static char scancode_table[] =
{
    [0x02] = '1',
    [0x03] = '2',
    [0x04] = '3',
    [0x05] = '4',
    [0x06] = '5',
    [0x07] = '6',
    [0x08] = '7',
    [0x09] = '8',
    [0x0A] = '9',
    [0x0B] = '0',
    [0x0C] = '-',
    [0x0D] = '=',
    [0x0E] = '\b',
    [0x0F] = '\t',

    [0x10] = 'q',
    [0x11] = 'w',
    [0x12] = 'e',
    [0x13] = 'r',
    [0x14] = 't',
    [0x15] = 'y',
    [0x16] = 'u',
    [0x17] = 'i',
    [0x18] = 'o',
    [0x19] = 'p',
    [0x1A] = '[',
    [0x1B] = ']',
    [0x1C] = '\n',

    [0x1E] = 'a',
    [0x1F] = 's',
    [0x20] = 'd',
    [0x21] = 'f',
    [0x22] = 'g',
    [0x23] = 'h',
    [0x24] = 'j',
    [0x25] = 'k',
    [0x26] = 'l',
    [0x27] = ';',
    [0x28] = '\'',
    [0x29] = '`',

    [0x2B] = '\\',
    [0x2C] = 'z',
    [0x2D] = 'x',
    [0x2E] = 'c',
    [0x2F] = 'v',
    [0x30] = 'b',
    [0x31] = 'n',
    [0x32] = 'm',
    [0x33] = ',',
    [0x34] = '.',
    [0x35] = '/',

    [0x37] = '*', /* KEYPAD */
    [0x39] = ' ',

    /* KEYPAD */
    [0x47] = '7',
    [0x48] = '8',
    [0x49] = '9',
    [0x4A] = '-',
    [0x4B] = '4',
    [0x4C] = '5',
    [0x4D] = '6',
    [0x4E] = '+',
    [0x4F] = '1',
    [0x50] = '2',
    [0x51] = '3',
    [0x52] = '0',
    [0x53] = '.'
};

static char numeric_symbols[] = { '~', '!', '@', '#', '$', '%', '^', '&', '*',
                                  '(', ')', '_', '+' };

struct keyboard_state kb_state;

static keyboard_handler_t keyboard_handler;

static inline void keyboard_wait()
{
    while (inportb(KEYBOARD_DATA_PORT) & KEYBOARD_BUSY);
}

static inline void keyboard_wait_ack()
{
    while (inportb(KEYBOARD_DATA_PORT) != KEYBOARD_ACKNOWLEDGE);
}

static inline void disable_keyboard()
{
    outportb(KEYBOARD_CMD_PORT, KEYBOARD_DISABLE);
}

static inline void enable_keyboard()
{
    outportb(KEYBOARD_CMD_PORT, KEYBOARD_ENABLE);
}

static inline int is_lock_key(uint8_t scancode)
{
    switch (scancode)
    {
        case CAPS_LOCK:
        case SCROLL_LOCK:
        case NUM_LOCK:
            return 1;
        default:
            return 0;
    }
}

static inline void set_keyboard_leds(uint8_t lock_keys)
{
    outportb(KEYBOARD_DATA_PORT, KEYBOARD_SET_LEDS);
    keyboard_wait_ack();
    outportb(KEYBOARD_DATA_PORT, lock_keys);
}

static inline void toggle_lock_key(uint8_t scancode)
{
    uint8_t key;

    switch (scancode)
    {
        case CAPS_LOCK:
            key = CAPS_LOCK_BIT;
            break;
        case SCROLL_LOCK:
            key = SCROLL_LOCK_BIT;
            break;
        case NUM_LOCK:
            key = NUM_LOCK_BIT;
            break;
        default:
            BUG("INVALID SCANCODE FOR LOCK KEY");
            return;
    }

    toggle_bitb(&kb_state.lock_keys, key);

    set_keyboard_leds(kb_state.lock_keys);
}

static inline uint8_t get_modifier_key_flag(uint8_t scancode)
{
    uint8_t bit;

    switch (scancode)
    {
        case CTRL:
            bit = MODKEY_CTRL;
            break;
        case ALT:
            bit = MODKEY_ALT;
            break;
        case RIGHT_SHIFT:
        case LEFT_SHIFT:
            bit = MODKEY_SHIFT;
            break;
        default:
            BUG("INVALID MODIFIER KEY");
            return 0;
    }

    return bit;
}

static inline void set_modifier_key_flag(uint8_t scancode)
{
    uint8_t bit;

    bit = get_modifier_key_flag(scancode);
    set_bitb(&kb_state.modifier_keys, bit);
}

static inline void unset_modifier_key_flag(uint8_t scancode)
{
    uint8_t bit;

    bit = get_modifier_key_flag(scancode);
    unset_bitb(&kb_state.modifier_keys, bit);
}

static inline char numeric_row_apply_shift(uint8_t scancode)
{
    int index;

    index = (scancode == 0x29) ? 0 : scancode - 1;

    return numeric_symbols[index];
}

static void handle_keyboard(struct registers *regs)
{
    struct keyevent_data keyevent;

    send_eoi();

    if (!keyboard_handler)
        return;

    keyevent.scancode = inportb(KEYBOARD_DATA_PORT);
    keyevent.released = keyevent.scancode & KEY_RELEASED;
    if (keyevent.released)
        unset_bitb(&keyevent.scancode, KEY_RELEASED_BIT);
    keyevent.key = scancode_table[keyevent.scancode];

    if (!keyevent.released)
    {
        if (keyevent.key)
        {
            if (isalpha(keyevent.key) &&
                ((kb_state.modifier_keys & SHIFT_SET) ||
                 (kb_state.lock_keys & CAPS_LOCK_ON)))
                keyevent.key = toupper(keyevent.key);
            else if (is_numeric_row_key(keyevent.scancode) &&
                     !is_keypad_key(keyevent.scancode) &&
                     (kb_state.modifier_keys & SHIFT_SET))
                keyevent.key = numeric_row_apply_shift(keyevent.scancode);
            else if (isdigit(keyevent.key) &&
                     is_keypad_key(keyevent.scancode) &&
                     !(kb_state.lock_keys & NUM_LOCK_ON))
                keyevent.key = '\0';
        }
        else if (is_lock_key(keyevent.scancode))
            toggle_lock_key(keyevent.scancode);
        else if (is_modifier_key(keyevent.scancode))
            set_modifier_key_flag(keyevent.scancode);
    }
    else if (is_modifier_key(keyevent.scancode))
        unset_modifier_key_flag(keyevent.scancode);

    keyevent.kb_state = kb_state;

    keyboard_handler(&keyevent);
}

void init_keyboard()
{
    memset(&kb_state, 0, sizeof(struct keyboard_state));

    register_interrupt_handler(KEYBOARD_IRQ, handle_keyboard);
}

void register_keyboard_handler(keyboard_handler_t handler)
{
    keyboard_handler = handler;
}