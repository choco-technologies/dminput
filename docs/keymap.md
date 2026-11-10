# Keymaps and layouts

What a key gives is the sum of three layers, put one over the other:

1. **The built in keymap** of an ordinary keyboard: US letters, digits and punctuation with shift; shift, ctrl and alt as modifiers.
2. **The layout** of the keyboard: which keys it has and how they behave. A keypad (`numeric`) has digits and four
   operations. A small keyboard with a Sym key and no AltGr has sticky modifiers (a tap arms the modifier for the next key: sym, then `N` is a
   comma), a locked shift and the symbols printed on its keys as the **sym** layer - but that is the layout of *that* keyboard, so it is a file
   of its driver (the T-Deck Pro: `dmtca8418`, `layouts/t-deck-pro.ini`). An ordinary PC keyboard (`full`, the default) needs none.
3. **The language**: the letters with accents and the like (`pl`: Polish letters on alt), the same for every keyboard.

## The layout of a keyboard

The driver says it in its info (`dmdrvi_input_info_t`): `layout` (a name) and `module` (the module of the driver). libinput looks for
`<layout>.ini` first among the files the module ships (`/cfg/layouts/<module>/`), then among its own (`/cfg/layouts/libinput/`: `numeric`). A driver with a keyboard nobody else has brings its file with it - a `layout=` entry of the `.dmd` file puts it there - and
can **extend** another layout (its own, or one of libinput) instead of writing everything:

```ini
[layout]
extends=t-deck-pro      ; the layouts it extends are read first (4 deep)

[sym]
0x110=%                 ; a key of its own (the driver gives it a code from DMDRVI_KEY_VENDOR_BASE + 0x10)
```

Without a layout in its info (or `full`) a keyboard is an ordinary one. A layout that is not found is a warning in the log and the
keyboard is an ordinary one. The keys a USB keyboard has none of (`SYM`, `MIC`, `DOLLAR`) have the same codes for every driver
(dmdrvi: `DMDRVI_KEY_SYM` 0x100, `DMDRVI_KEY_MIC` 0x101, `DMDRVI_KEY_DOLLAR` 0x102), so a layout only says what they are.

## The language

One per language: `configs/keymaps/<language>.ini` (as `pl.ini`), put in `/cfg/keymaps/libinput` by a `keymap=` entry of the `.dmd` file. The
language is chosen by the environment variable `LANG` at start: `pl_PL.UTF-8` or `pl` is `pl.ini`; without `LANG`, or with `en`, `C`,
`POSIX`, nothing is added (English). A language without a file leaves English with a warning. `libinput_keymap_set_language("pl")` changes it
at run time (a settings screen) for **every** keyboard at once, `libinput_keymap_language()` says which is in use. `libinput_keymap_load(path)`
puts a file by hand on top of the layouts instead of the language; a file that is not a keymap is refused (`-EINVAL`) and the old keymap stays.

`/cfg` is the directory of the system; the environment variable `DMINPUT_CFG_DIR` puts it elsewhere (a host without `/cfg`, the tests).

## The file

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
have none). [`configs/keymaps/pl.ini`](../configs/keymaps/pl.ini) adds the Polish letters on alt,
[`configs/layouts/numeric.ini`](../configs/layouts/numeric.ini) is the layout of a keypad; the layout of a small keyboard with a Sym key
is that of the T-Deck Pro in dmtca8418 (`configs/layouts/t-deck-pro.ini`).
