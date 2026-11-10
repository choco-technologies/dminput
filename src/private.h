#ifndef LIBINPUT_PRIVATE_H
#define LIBINPUT_PRIVATE_H

#include "libinput.h"
#include "dmosi.h"

/* ---- Modifiers ---- */

/** The modifiers of a keymap, by index; a layer is a mask of 1 << index. */
#define MOD_SHIFT       0
#define MOD_CTRL        1
#define MOD_ALT         2
#define MOD_SYM         3
#define MOD_COUNT       4

/** Keys that may be one modifier. */
#define MOD_MAX_CODES   4

/** A letter of the USB HID usages: CAPS (shift locked) acts on these only. */
static inline bool key_is_letter(uint16_t code)
{
    return code >= DMDRVI_KEY_A && code <= DMDRVI_KEY_Z;
}

/* ---- keymap.c ---- */

typedef struct keymap keymap_t;

/** The built in keymap: US letters, digits and punctuation with shift; shift, ctrl, alt as modifiers. */
keymap_t* keymap_create(void);

/** The built in keymap with the layers of a file on top: NULL and *status when it is not a keymap. */
keymap_t* keymap_create_from_file(const char* path, int* status);

void keymap_destroy(keymap_t* keymap);

/** The modifier (MOD_*) a key is, -1: it is not one. */
int keymap_modifier(const keymap_t* keymap, uint16_t code);

/** Whether a tap of the modifier arms it for the next key / a second tap locks it. */
bool keymap_sticky(const keymap_t* keymap, int modifier);
bool keymap_lockable(const keymap_t* keymap, int modifier);

/** The character (Unicode) of a key in a layer (mask of 1 << MOD_*), 0: none. */
uint32_t keymap_char(const keymap_t* keymap, uint16_t code, uint8_t layer);

/* ---- libinput.c ---- */

/** The keymap of the language of $LANG, once, unless one was chosen by libinput_keymap_load(). */
void keymap_from_environment(void);

/* ---- keys.c ---- */

/** Most keys one device holds down that are remembered (so a key goes up as the character it went down as). */
#define KEYS_MAX_DOWN   8

typedef struct
{
    uint16_t code;
    uint8_t  modifiers;
    uint32_t codepoint;
} key_down_t;

/** What a keyboard holds: the modifiers (held, armed for the next key, locked) and the keys that are down. */
typedef struct
{
    uint8_t     held[MOD_COUNT];        /* Keys of the modifier that are down */
    bool        clean[MOD_COUNT];       /* Held and no key pressed meanwhile: a tap when it goes up */
    bool        armed[MOD_COUNT];       /* For the next key */
    bool        locked[MOD_COUNT];
    key_down_t  down[KEYS_MAX_DOWN];
    uint8_t     down_count;
} keystate_t;

void keystate_reset(keystate_t* state);

/** Feed a raw key event; true when `out` (key fields, time, action) is to be queued. */
bool keystate_feed(keystate_t* state, const keymap_t* keymap, const dmdrvi_key_event_t* in, libinput_event_t* out);

#endif /* LIBINPUT_PRIVATE_H */
