# inputd

The service of one input device. libinput is where the inputs come together; inputd is its
feeder - one instance, one thread, per device.

```
inputd <device name> [<device node>]      # started from inputd@.ini, see below
```

It is one call: `libinput_run(<device node>, libsystemd_stop_requested)`. libinput opens the node,
asks it what it is, waits on it and posts what it reports (the raw keys of a keyboard, the state of
a touch panel, a mouse or buttons) until libsystemd asks the service to stop - see
[docs/libinput.md](../../docs/libinput.md). inputd registers nothing and keeps nothing: when it
ends, or crashes, nothing of the device is left in libinput.

For keyboards the keymap is that of the language of the environment variable `LANG`
(see [docs/keymap.md](../../docs/keymap.md)); without it the built in English one is used.

## How it is started

Nothing here follows devices. dmdevfs reports every node that answers
`DMDRVI_IOCTL_INPUT_GET_INFO` to libsystemd as a device of class `input`, and
[`configs/inputd.rules`](configs/inputd.rules) says what to do:

```ini
[class=input]
start=inputd@%name
```

libsystemd instantiates [`configs/inputd@.ini`](configs/inputd@.ini) for each, with the name
and the path of the node. In a dmod-boot image: `inputd service=inputd@.ini` and
`inputd rules=inputd.rules` in `flash.dmd`.
