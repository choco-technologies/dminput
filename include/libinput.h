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
 * inputd calls libinput_run() with the path of its device's node; libinput reads the raw
 * events of the node itself. It
 *  - translates the raw keys of a keyboard into characters (a keymap, the layers
 *    of the modifier keys: shift, alt, sym, ctrl, caps),
 *  - mixes what every device reports into one stream, each event saying where it came from
 *    (its source - touch, keyboard, ... - and the friends_group of the device, so a display
 *    service takes the events of the inputs of its display by comparing the group's name),
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

/** Longest group name of an event, with the terminator (that of dmdrvi's DMDRVI_IOCTL_DEVFS_GET_GROUP). */
#define LIBINPUT_GROUP_MAX              32u

/** A reader of the events. */
typedef struct libinput_reader* libinput_reader_t;

/** One event. */
typedef struct
{
    uint32_t            time_ms;    /**< When it happened (the driver's clock for a key) */
    char                group[LIBINPUT_GROUP_MAX];  /**< The friends_group of the device: what a display service compares with its display's ("" when it has none) */
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

/* ---- The service (what inputd does) ---------------------------------------------------------------------------------- */

/** Asked again and again while libinput_run() waits: true ends it. */
typedef bool (*libinput_stop_t)(void);

/**
 * Read an input device: opens the node, finds out what it is and posts what it reports to
 * the readers until `should_stop` says so (or NULL: never) or the node fails. That is all a
 * service does - one call, the path of the node; it is the thread of the device.
 *
 * Nothing of the device is kept anywhere else: when this returns, or the process of the
 * service is gone, there is nothing of it left in libinput.
 *
 * Returns 0 after a stop, a negative errno when the node cannot be used.
 */
dmod_libinput_api(1.0, int, _run, ( const char* path, libinput_stop_t should_stop ));

/**
 * The keymap of the keyboards: built in layers (US letters, digits, shift) with the ones
 * of the file on top (see docs/keymap.md). NULL: only the built in one. Without a call the
 * file of $DMINPUT_KEYMAP is used, else LIBINPUT_BOARD_KEYMAP, if there is one.
 * -ENOENT without the file, -EINVAL for a file that is not a keymap (the old keymap stays).
 */
dmod_libinput_api(1.0, int, _keymap_load, ( const char* path ));

/* ---- Sources (what libinput_run is made of) ------------------------------------------------------------------------ */

/**
 * The state of one device that libinput_run() keeps while it reads it (the modifiers of a
 * keyboard) - and the way to feed libinput without a node: tests, a driver that is not
 * behind one. It belongs to whoever created it and to nobody else.
 */
typedef struct libinput_feed* libinput_feed_t;

/** `group`: the friends_group the events of this device carry (NULL: none). */
dmod_libinput_api(1.0, libinput_feed_t, _feed_create, ( const char* group, const dmdrvi_input_info_t* info ));
dmod_libinput_api(1.0, void, _feed_destroy, ( libinput_feed_t feed ));

/** A raw key event of a keyboard: translated and queued for the readers. */
dmod_libinput_api(1.0, int, _feed_post_key, ( libinput_feed_t feed, const dmdrvi_key_event_t* event ));

/** The state of a device that reports one: queued for the readers as it is. */
dmod_libinput_api(1.0, int, _feed_post_state, ( libinput_feed_t feed, const dmdrvi_input_state_t* state, uint32_t now_ms ));

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

/** The keymap a board ships: its files of configs/board/<board>/keymaps go to /cfg/keymaps/board (dmod-boot). */
#define LIBINPUT_BOARD_KEYMAP           "/cfg/keymaps/board/keymap.ini"

/** The timeout that does not end. */
#define LIBINPUT_FOREVER                0xFFFFFFFFu

#ifdef __cplusplus
}
#endif

#endif /* LIBINPUT_H */
