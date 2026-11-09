# dminput

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dminput/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dminput/actions/workflows/ci.yml)

Where the inputs of the system come together.

```
driver --(node)--> inputd (one per device) --> libinput --> reader (dmview, ...)
```

| Module | What it is |
|--------|-----------|
| [`libinput`](docs/libinput.md) | The library: devices, the translation of keys into characters (a [keymap](docs/keymap.md)), one queue of all the events for every reader. |
| [`inputd`](services/inputd/README.md) | The service, one instance per input device, started by a libsystemd rule for the `input` class dmdevfs reports. It is the thread of its device: it calls `libinput_run()` with the path of the node, and libinput does the rest. |

A driver reports raw events (which key went down or up, the state of a touch panel) and nothing of
what they mean. libinput interprets them and mixes the devices into one stream; a reader only
asks for events and gets each with its source (touch, keyboard, buttons, mouse).

See [docs/libinput.md](docs/libinput.md) for the API and the rules of the queues,
[docs/keymap.md](docs/keymap.md) for the layouts of keyboards.

## Not here yet

- **Touch** is passed through as the state the driver gives (`LIBINPUT_EVENT_STATE`): how it is
  turned into events is still to be decided.
- **Auto-repeat** of a held key.
- Gestures, mouse acceleration, a keymap per device.
