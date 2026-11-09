# libinput

```c
#include "libinput.h"
```

## What goes through it

- **inputd** (one per input device) calls `libinput_run(path, should_stop)` with the path of its
  device's node. libinput opens the node, asks what it is, and reads what the device reports -
  raw keys (`DMDRVI_IOCTL_INPUT_KEY_READ`) or the state of a touch panel / mouse / buttons - until
  `should_stop` says so. The call is the thread of the device; nothing else of the device is kept
  anywhere, so a service that crashes leaves nothing behind in libinput.
- **libinput** translates the keys into characters and queues every event for every reader.
- A **reader** (dmview, a console) opens itself (`libinput_reader_open`) and takes the events
  (`libinput_read`).

## The event

```c
typedef struct {
    uint32_t          time_ms;
    libinput_device_t device;   /* libinput_device_find(path) tells which is which */
    uint8_t           source;   /* LIBINPUT_SOURCE_TOUCH / _MOUSE / _BUTTONS / _KEYBOARD */
    uint8_t           type;     /* LIBINPUT_EVENT_KEY / _STATE */
    union {
        struct { uint32_t codepoint; uint16_t code; uint8_t action; uint8_t modifiers; } key;
        dmdrvi_input_state_t state;
    } u;
} libinput_event_t;
```

A **key** event: `code` is the key (a USB HID usage, as the driver gave it), `codepoint` the
Unicode character it gave in the layer in effect (0 when it gives none - Enter, an arrow, a
modifier key), `action` `LIBINPUT_ACTION_DOWN` / `_UP`, `modifiers` the `LIBINPUT_MOD_*` in effect
(the event of a modifier key says what they are now; `LIBINPUT_MOD_CAPS`: shift is locked). A key
goes up as the character it went down as, whatever the modifiers did between.

A **state** event is the state of a device that reports one, as the driver gave it.

## Functions

| Function | |
|----------|-|
| `libinput_run(path, should_stop)` | Read the node at `path` until `should_stop()` is true (`NULL`: never). 0 after a stop, `-ENOENT` / `-ENOTTY` / `-ENOMEM` when the node cannot be used |
| `libinput_device_id(path)` | What the events of that node carry as `device` (a hash of the path: the same path, the same id; no list of devices exists) |
| `libinput_keymap_load(path)` | The keymap of keyboards: [docs/keymap.md](keymap.md); `NULL`: the built in one. Without a call the file of `$DMINPUT_KEYMAP`, if any |
| `libinput_reader_open()` / `_close(reader)` | A reader of the events (one of a process that is gone is dropped) |
| `libinput_read(reader, &event, timeout_ms)` | The oldest event. Waits up to `timeout_ms` (0: no wait, `LIBINPUT_FOREVER`). 1: an event, 0: none in time, `< 0`: an error (`-ECANCELED`: the reader was closed) |
| `libinput_feed_create(path, &info)` / `_destroy` / `_post_key` / `_post_state` | What `libinput_run` is made of: the state of one device (the modifiers of a keyboard), owned by its creator. For tests and for feeding libinput without a node |

A reader that has something due (an animation, a timer) reads with the time that is left; after
an event it drains the rest with timeout 0. To tell which device an event is from, compare its
`device` with `libinput_device_id(path)` of the nodes it is interested in.

## The queues

- **Nothing is kept for nobody.** With no reader an event is dropped; the queue of a reader is
  empty when it is opened, so it never finds the keys of the minutes before.
- **Every reader has a queue of its own** (32 events). When it is full the oldest event is
  dropped. A reader that is slow loses events; it never holds back the others or the devices.
- A raw `DMDRVI_KEY_ACTION_OVERFLOW` of a driver (events were lost, a key seen down may be up)
  makes libinput forget what the keyboard held: the modifiers and the keys that are down.
- A reader whose process is gone without closing it is dropped, so a crash of a reader does not
  keep its slot.

## Keyboards

The driver says *which key* and nothing of what it means (see dmdrvi's "Keyboards"). libinput
keeps for each keyboard the state of the modifiers - shift, ctrl, alt, sym: **held** while the
key is down, **armed** for the next key after a tap (sticky modifiers), **locked** after a second
tap of shift (caps lock: shift for letters only) - and translates a key by the layer of the
modifiers in effect. The layers and which keys are the modifiers are the
[keymap](keymap.md).
