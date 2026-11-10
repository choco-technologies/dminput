# Keymaps

A keymap says what a key gives: its character in each **layer**, and which keys are the
modifiers that choose the layer. libinput has the English one built in: US letters, digits and
punctuation with shift; shift, ctrl and alt as modifiers; the Sym key of a small keyboard
(`DMDRVI_KEY_SYM`) and the **sym layer** of the symbols printed on its keys; `$` for the
dollar key (`DMDRVI_KEY_DOLLAR`); shift, alt and sym are **sticky** (a tap arms them for the next key,
sym then `N` is a comma) and a second tap of shift locks the capitals. The keys a USB keyboard has
none of have the same codes for every driver (dmdrvi), so a keymap does not map them.

A file (`.ini`) goes **on top** of the built in one: the layers it names are added, the ones it does
not stay. The files are **one per language** (`keymaps/<language>.ini`, as `pl.ini`) and the language is
chosen by the environment variable `LANG`: `pl_PL.UTF-8` or `pl` loads `/cfg/keymaps/libinput/pl.ini`
(`LIBINPUT_KEYMAP_DIR`; a `keymap=` entry of a `.dmd` file puts the file of libinput's package there);
without `LANG`, or with `en`, `C`, `POSIX`, the built in English keymap is used. A language without a
file leaves the English one with a warning. `libinput_keymap_set_language("pl")` changes the language at run time (a settings screen) and
`libinput_keymap_language()` says which is in use. `libinput_keymap_load(path)` loads a file by hand;
a file that is not a keymap is refused (`-EINVAL`) and the old keymap stays.

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

The code of a key is what the driver says (USB HID usages and the codes of dmdrvi for the keys that
have none), so a keymap goes with a language, not with a board or a keyboard.
[`keymaps/pl.ini`](../keymaps/pl.ini) adds the Polish letters on alt.
