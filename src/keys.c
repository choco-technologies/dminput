#include "private.h"
#include <string.h>

void keystate_reset(keystate_t* state)
{
    memset(state, 0, sizeof(*state));
}

/* The modifiers in effect, as LIBINPUT_MOD_*: held, armed or locked (a locked shift is CAPS) */
static uint8_t modifiers_in_effect(const keystate_t* state)
{
    static const uint8_t bits[MOD_COUNT] = { LIBINPUT_MOD_SHIFT, LIBINPUT_MOD_CTRL, LIBINPUT_MOD_ALT, LIBINPUT_MOD_SYM };
    uint8_t mods = 0;
    for (int m = 0; m < MOD_COUNT; m++)
    {
        if (state->held[m] > 0 || state->armed[m])
            mods |= bits[m];
        else if (state->locked[m])
            mods |= (m == MOD_SHIFT) ? LIBINPUT_MOD_CAPS : bits[m];
    }
    return mods;
}

/* The layer of a key: a mask of 1 << MOD_*. A locked shift is a shift for letters only. */
static uint8_t layer_of(const keystate_t* state, uint16_t code)
{
    uint8_t layer = 0;
    for (int m = 0; m < MOD_COUNT; m++)
    {
        bool on = state->held[m] > 0 || state->armed[m] ||
                  (state->locked[m] && (m != MOD_SHIFT || key_is_letter(code)));
        if (on)
            layer |= (uint8_t)(1u << m);
    }
    return layer;
}

/* A modifier key went up: if nothing was pressed meanwhile it is a tap - arm, lock, unlock */
static void modifier_tap(keystate_t* state, const keymap_t* keymap, int m)
{
    if (!keymap_sticky(keymap, m))
        return;
    if (state->locked[m])
        state->locked[m] = false;
    else if (state->armed[m])
    {
        state->armed[m] = false;
        state->locked[m] = keymap_lockable(keymap, m);
    }
    else
        state->armed[m] = true;
}

static bool feed_modifier(keystate_t* state, const keymap_t* keymap, int m, bool down)
{
    if (down)
    {
        if (state->held[m] < 255)
            state->held[m]++;
        state->clean[m] = true;
        return true;
    }
    if (state->held[m] > 0)
        state->held[m]--;
    if (state->held[m] == 0 && state->clean[m])
        modifier_tap(state, keymap, m);
    return true;
}

static void remember_down(keystate_t* state, uint16_t code, uint8_t mods, uint32_t cp)
{
    for (uint8_t i = 0; i < state->down_count; i++)
    {
        if (state->down[i].code == code)
        {
            state->down[i].modifiers = mods;
            state->down[i].codepoint = cp;
            return;
        }
    }
    if (state->down_count == KEYS_MAX_DOWN)
    {
        memmove(&state->down[0], &state->down[1], (KEYS_MAX_DOWN - 1U) * sizeof(key_down_t));
        state->down_count--;
    }
    state->down[state->down_count++] = (key_down_t){ code, mods, cp };
}

/* A key that is not a modifier went down: its character in the layer now, and the modifiers that were armed are used up */
static void feed_key_down(keystate_t* state, const keymap_t* keymap, const dmdrvi_key_event_t* in, libinput_event_t* out)
{
    out->u.key.modifiers = modifiers_in_effect(state);
    out->u.key.codepoint = keymap_char(keymap, in->code, layer_of(state, in->code));
    remember_down(state, in->code, out->u.key.modifiers, out->u.key.codepoint);
    for (int m = 0; m < MOD_COUNT; m++)
    {
        state->armed[m] = false;
        state->clean[m] = false;
    }
}

/* A key went up as it went down; one not seen going down (its event was lost) gives no character */
static void feed_key_up(keystate_t* state, const dmdrvi_key_event_t* in, libinput_event_t* out)
{
    out->u.key.modifiers = modifiers_in_effect(state);
    out->u.key.codepoint = 0;
    for (uint8_t i = 0; i < state->down_count; i++)
    {
        if (state->down[i].code != in->code)
            continue;
        out->u.key.modifiers = state->down[i].modifiers;
        out->u.key.codepoint = state->down[i].codepoint;
        memmove(&state->down[i], &state->down[i + 1], (state->down_count - i - 1U) * sizeof(key_down_t));
        state->down_count--;
        return;
    }
}

bool keystate_feed(keystate_t* state, const keymap_t* keymap, const dmdrvi_key_event_t* in, libinput_event_t* out)
{
    bool down = in->action == DMDRVI_KEY_ACTION_DOWN;
    if (!down && in->action != DMDRVI_KEY_ACTION_UP)
        return false;                                   /* (OVERFLOW is for the caller) */
    out->time_ms = in->time_ms;
    out->source = LIBINPUT_SOURCE_KEYBOARD;
    out->type = LIBINPUT_EVENT_KEY;
    out->u.key.code = in->code;
    out->u.key.action = down ? LIBINPUT_ACTION_DOWN : LIBINPUT_ACTION_UP;
    out->u.key.codepoint = 0;

    int m = keymap_modifier(keymap, in->code);
    if (m >= 0)
    {
        feed_modifier(state, keymap, m, down);
        out->u.key.modifiers = modifiers_in_effect(state);  /* Its own event says what the modifiers are now */
    }
    else if (down)
        feed_key_down(state, keymap, in, out);
    else
        feed_key_up(state, in, out);
    return true;
}
