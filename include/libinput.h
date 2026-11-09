#ifndef LIBINPUT_H
#define LIBINPUT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmod.h"
#include "libinput_defs.h"
#include "dmdrvi_ioctl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * libinput - the one place all the inputs of the system come together.
 *
 *   driver --(node)--> inputd (one per device) --> libinput --> reader (dmview, ...)
 *
 * inputd reads the raw events of its device and posts them here. libinput
 *  - translates the raw keys of a keyboard into characters (a keymap, the layers
 *    of the modifier keys: shift, alt, sym, ctrl, caps),
 *  - mixes what every device reports into one stream, each event saying where it came from,
 *  - queues it for every reader.
 * A reader asks for events (libinput_read) and nothing else: it does not know what
 * devices there are, how they report or what a keyboard looks like.
 *
 * Nothing is kept for nobody: with no reader an event is dropped, a reader's queue is
 * empty when it is opened (it never finds the keys of the minutes before), and when it
 * is full the oldest event goes.
 */

/** Where an event comes from. The values are those of dmdrvi_input_type_t. */
typedef enum
{
    LIBINPUT_SOURCE_TOUCH = 0,
    LIBINPUT_SOURCE_MOUSE,
    LIBINPUT_SOURCE_BUTTONS,
    LIBINPUT_SOURCE_KEYBOARD,
} libinput_source_t;

/** What an event is. */
typedef enum
{
    LIBINPUT_EVENT_KEY = 0,         /**< A key of a keyboard: `u.key` */
    LIBINPUT_EVENT_STATE,           /**< The state of a device that reports one (touch, mouse, buttons): `u.state`, as the driver gave it */
} libinput_event_type_t;

/** `u.key.action` */
#define LIBINPUT_ACTION_DOWN            0u
#define LIBINPUT_ACTION_UP              1u

/** `u.key.modifiers`: the modifiers in effect for the key (CAPS: shift is locked). A modifier key's own event carries its new state. */
#define LIBINPUT_MOD_SHIFT              (1u << 0)
#define LIBINPUT_MOD_CTRL               (1u << 1)
#define LIBINPUT_MOD_ALT                (1u << 2)
#define LIBINPUT_MOD_SYM                (1u << 3)
#define LIBINPUT_MOD_CAPS               (1u << 4)

/** A device registered in libinput; 0 is none. */
typedef uint16_t libinput_device_t;

/** A reader of the events. */
typedef struct libinput_reader* libinput_reader_t;

/** One event. */
typedef struct
{
    uint32_t            time_ms;    /**< When it happened (the driver's clock for a key) */
    libinput_device_t   device;     /**< Where from: see libinput_device_find() */
    uint8_t             source;     /**< libinput_source_t */
    uint8_t             type;       /**< libinput_event_type_t */
    union
    {
        struct
        {
            uint32_t    codepoint;  /**< The character (Unicode) the key gave, 0: it gives none (a modifier, Enter, an arrow) */
            uint16_t    code;       /**< The key itself: a USB HID usage (see dmdrvi_ioctl.h) */
            uint8_t     action;     /**< LIBINPUT_ACTION_* */
            uint8_t     modifiers;  /**< LIBINPUT_MOD_* */
        } key;
        dmdrvi_input_state_t state;
    } u;
} libinput_event_t;

/* ---- Devices (what inputd does) -------------------------------------------------------------------------------------- */

/**
 * Register a device by the path of its node. -EEXIST when the path is registered, -ENOMEM when there are too many.
 */
dmod_libinput_api(1.0, int, _device_add, ( const char* path, const dmdrvi_input_info_t* info, libinput_device_t* device ));

/** Remove a device: what it was holding is forgotten. */
dmod_libinput_api(1.0, void, _device_remove, ( libinput_device_t device ));

/** The device registered under this path, 0 when there is none (yet). */
dmod_libinput_api(1.0, libinput_device_t, _device_find, ( const char* path ));

/** A raw key event of a keyboard: it is translated and queued for the readers. */
dmod_libinput_api(1.0, int, _post_key, ( libinput_device_t device, const dmdrvi_key_event_t* event ));

/** The state of a device that reports one: queued for the readers as it is. */
dmod_libinput_api(1.0, int, _post_state, ( libinput_device_t device, const dmdrvi_input_state_t* state, uint32_t now_ms ));

/**
 * The keymap of the keyboards: built in layers (US letters, digits, shift) with the ones
 * of the file on top (see docs/keymap.md). NULL: only the built in one.
 * -ENOENT without the file, -EINVAL for a file that is not a keymap (the old keymap stays).
 */
dmod_libinput_api(1.0, int, _keymap_load, ( const char* path ));

/* ---- Readers (what dmview does) -------------------------------------------------------------------------------------- */

/** A reader: its queue starts empty. NULL: out of memory or too many readers. */
dmod_libinput_api(1.0, libinput_reader_t, _reader_open, ( void ));

/** Close a reader. A thread waiting in libinput_read() of it is woken and gets -ECANCELED. */
dmod_libinput_api(1.0, void, _reader_close, ( libinput_reader_t reader ));

/**
 * The oldest event of the reader. Waits for one up to `timeout_ms` (0: does not wait,
 * LIBINPUT_FOREVER: no limit). Returns 1 with the event, 0 when there was none in time,
 * a negative errno otherwise.
 */
dmod_libinput_api(1.0, int, _read, ( libinput_reader_t reader, libinput_event_t* event, uint32_t timeout_ms ));

/** The timeout that does not end. */
#define LIBINPUT_FOREVER                0xFFFFFFFFu

#ifdef __cplusplus
}
#endif

#endif /* LIBINPUT_H */
