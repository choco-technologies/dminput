# Keymaps

A keymap says what a key gives: its character in each **layer**, and which keys are the
modifiers that choose the layer. libinput has one built in (US letters, digits and punctuation
with shift; shift, ctrl and alt as modifiers; no sticky modifiers). A file (`.ini`) goes
**on top** of it: the layers it names are added, the ones it does not stay.

Load one with `libinput_keymap_load(path)`. Without a call `libinput` uses the file of the environment variable
`DMINPUT_KEYMAP`, else the keymap the board ships: `/cfg/keymaps/board/keymap.ini` (in dmod-boot the files of
`configs/board/<board>/keymaps/` go there), when there is one. A file that is not a keymap is refused (`-EINVAL`) and the old keymap stays.

```ini
[modifiers]
shift=0xE1, 0xE5        ; the keys that are shift (USB HID usages, or their names)
sym=0x100               ; a key without a HID usage: a code the driver gives it
sticky=shift, alt, sym  ; a tap arms the modifier for the next key
lock=shift              ; a second tap locks it (caps lock: for letters only)

[base]                  ; the layers: base, or modifiers joined with + (ctrl, alt, shift, sym)
Q=q

[alt]
A=ą
C=ć

[alt+shift]
A=Ą

[sym]                   ; what is printed on the keys of a small keyboard
Q=U+0023
W=1
```

- **Keys** are letters `A`..`Z`, digits `0`..`9`, `ENTER`, `ESC`, `BACKSPACE`, `TAB`, `SPACE`,
  `DEL`, `RIGHT`, `LEFT`, `DOWN`, `UP`, or a code (`0x100`, `57`).
- **Characters** are UTF-8 (one character) or `U+XXXX`. Write `#` and `;` as `U+0023` / `U+003B`:
  they begin a comment in an `.ini` file.
- `[modifiers]` is optional; a list of keys given for a modifier **replaces** the built in one.
- A key that has no entry in a layer gives no character there (except the built in `base` and
  `shift`); the application still gets its code.

The code of a key is what the driver says (its configuration maps the matrix of the keyboard to
USB HID usages), so a keymap goes with the driver's key codes, not with a board.
[`keymaps/pl.ini`](../keymaps/pl.ini) adds the Polish letters on alt.
