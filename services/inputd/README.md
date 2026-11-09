# inputd

The service of one input device. libinput is where the inputs come together; inputd is its
feeder - one instance, one thread, per device.

```
inputd <device name> [<device node>]      # started from inputd@.ini, see below
```

It opens the node (`/dev/<name>` unless given), asks it what it is
(`DMDRVI_IOCTL_INPUT_GET_INFO`), registers it in libinput, then waits on it
(`DMDRVI_IOCTL_INPUT_WAIT_EVENT`) and posts what it reports:

- a **keyboard** (`DMDRVI_INPUT_CAP_KEYS`): the raw key events of `DMDRVI_IOCTL_INPUT_KEY_READ`,
- a **touch panel, a mouse, buttons**: the state of `read()`.

It translates nothing; that is libinput's. It looks for a request to stop every 500 ms.

For keyboards the keymap is the file named by the environment variable `DMINPUT_KEYMAP`
(see [docs/keymap.md](../../docs/keymap.md)); without it the built in one is used.

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
