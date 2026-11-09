#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "libinput.h"
#include <string.h>

/* A keyboard in libinput and a reader of what it posts: every step makes its own */
typedef struct
{
    libinput_feed_t   feed;
    libinput_reader_t reader;
    uint32_t          time;
} rig_t;

static void rig_open(rig_t* rig, const char* group, dmdrvi_input_type_t type)
{
    dmdrvi_input_info_t info;
    memset(&info, 0, sizeof(info));
    info.type = type;
    rig->time = 0;
    rig->feed = libinput_feed_create(group, &info);
    DMOD_TEST_EXPECT_NOT_NULL(rig->feed);
    rig->reader = libinput_reader_open();
    DMOD_TEST_EXPECT_NOT_NULL(rig->reader);
}

static void rig_close(rig_t* rig)
{
    libinput_reader_close(rig->reader);
    libinput_feed_destroy(rig->feed);
    (void)libinput_keymap_load(NULL);                   /* The built in keymap again */
}

static void post(rig_t* rig, uint16_t code, uint8_t action)
{
    dmdrvi_key_event_t event = { ++rig->time, code, action, 0 };
    DMOD_TEST_EXPECT_EQ(libinput_feed_post_key(rig->feed, &event), 0);
}

static void tap(rig_t* rig, uint16_t code)
{
    post(rig, code, DMDRVI_KEY_ACTION_DOWN);
    post(rig, code, DMDRVI_KEY_ACTION_UP);
}

/* The next event of the reader, or a zeroed one */
static libinput_event_t next(rig_t* rig)
{
    libinput_event_t event;
    memset(&event, 0, sizeof(event));
    DMOD_TEST_EXPECT_EQ(libinput_read(rig->reader, &event, 0), 1);
    return event;
}

/* The character the next key down of this code gives (its up is taken too) */
static uint32_t typed(rig_t* rig, uint16_t code)
{
    tap(rig, code);
    libinput_event_t down = next(rig);
    libinput_event_t up = next(rig);
    DMOD_TEST_EXPECT_EQ(down.u.key.action, LIBINPUT_ACTION_DOWN);
    DMOD_TEST_EXPECT_EQ(up.u.key.action, LIBINPUT_ACTION_UP);
    DMOD_TEST_EXPECT_EQ(up.u.key.codepoint, down.u.key.codepoint);      /* up as it went down */
    return down.u.key.codepoint;
}

static void drain(rig_t* rig)
{
    libinput_event_t event;
    while (libinput_read(rig->reader, &event, 0) == 1)
        ;
}

DMOD_TEST_STEP(keys_give_characters)
{
    rig_t rig;
    rig_open(&rig, "panel", DMDRVI_INPUT_TYPE_KEYBOARD);
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_A), (uint32_t)'a');
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_Z), (uint32_t)'z');
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_1), (uint32_t)'1');
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_0), (uint32_t)'0');
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_SPACE), (uint32_t)' ');
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_ENTER), 0u);             /* it gives none: the code is for the application */
    libinput_event_t event;
    tap(&rig, DMDRVI_KEY_UP);
    event = next(&rig);
    DMOD_TEST_EXPECT_EQ(event.u.key.code, DMDRVI_KEY_UP);
    DMOD_TEST_EXPECT_EQ(event.source, LIBINPUT_SOURCE_KEYBOARD);
    DMOD_TEST_EXPECT_EQ(event.type, LIBINPUT_EVENT_KEY);
    DMOD_TEST_EXPECT_EQ(strcmp(event.group, "panel"), 0);
    DMOD_TEST_EXPECT_EQ(event.time_ms, 13u);                              /* the driver's time */
    rig_close(&rig);
}

DMOD_TEST_STEP(a_held_shift_shifts)
{
    rig_t rig;
    rig_open(&rig, "panel", DMDRVI_INPUT_TYPE_KEYBOARD);
    post(&rig, DMDRVI_KEY_LEFT_SHIFT, DMDRVI_KEY_ACTION_DOWN);
    libinput_event_t shift = next(&rig);
    DMOD_TEST_EXPECT_EQ(shift.u.key.codepoint, 0u);
    DMOD_TEST_EXPECT_EQ(shift.u.key.modifiers, LIBINPUT_MOD_SHIFT);     /* its own event: the modifiers now */
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_A), (uint32_t)'A');
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_1), (uint32_t)'!');
    post(&rig, DMDRVI_KEY_LEFT_SHIFT, DMDRVI_KEY_ACTION_UP);
    drain(&rig);
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_A), (uint32_t)'a');       /* it was held with a key between: no tap */
    rig_close(&rig);
}

DMOD_TEST_STEP(the_two_shifts_are_one_modifier)
{
    rig_t rig;
    rig_open(&rig, "panel", DMDRVI_INPUT_TYPE_KEYBOARD);
    post(&rig, DMDRVI_KEY_LEFT_SHIFT, DMDRVI_KEY_ACTION_DOWN);
    post(&rig, DMDRVI_KEY_RIGHT_SHIFT, DMDRVI_KEY_ACTION_DOWN);
    post(&rig, DMDRVI_KEY_LEFT_SHIFT, DMDRVI_KEY_ACTION_UP);
    drain(&rig);
    DMOD_TEST_EXPECT_EQ(typed(&rig, (DMDRVI_KEY_A + 1u)), (uint32_t)'B');   /* still held: the right one */
    post(&rig, DMDRVI_KEY_RIGHT_SHIFT, DMDRVI_KEY_ACTION_UP);
    drain(&rig);
    DMOD_TEST_EXPECT_EQ(typed(&rig, (DMDRVI_KEY_A + 1u)), (uint32_t)'b');
    rig_close(&rig);
}

#define SYM_KEY     0x100u

static void load(const char* dir, const char* file, int expected)
{
    char path[256];
    size_t d = strlen(dir), f = strlen(file);
    DMOD_TEST_EXPECT_TRUE(d + f + 2 < sizeof(path));
    memcpy(path, dir, d);
    path[d] = '/';
    memcpy(path + d + 1, file, f + 1);
    DMOD_TEST_EXPECT_EQ(libinput_keymap_load(path), expected);
}

DMOD_TEST_STEP(a_tap_of_a_sticky_modifier_is_for_the_next_key)
{
    rig_t rig;
    rig_open(&rig, "panel", DMDRVI_INPUT_TYPE_KEYBOARD);
    load(LIBINPUT_FIXTURES_DIR, "sym.ini", 0);
    tap(&rig, DMDRVI_KEY_LEFT_SHIFT);
    libinput_event_t armed;
    armed = next(&rig);                                                  /* down */
    armed = next(&rig);                                                  /* up: armed */
    DMOD_TEST_EXPECT_EQ(armed.u.key.modifiers, LIBINPUT_MOD_SHIFT);
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_A), (uint32_t)'A');       /* the next key... */
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_A), (uint32_t)'a');       /* ... only */
    rig_close(&rig);
}

DMOD_TEST_STEP(a_second_tap_of_shift_locks_the_letters)
{
    rig_t rig;
    rig_open(&rig, "panel", DMDRVI_INPUT_TYPE_KEYBOARD);
    load(LIBINPUT_FIXTURES_DIR, "sym.ini", 0);
    tap(&rig, DMDRVI_KEY_LEFT_SHIFT);
    tap(&rig, DMDRVI_KEY_LEFT_SHIFT);
    drain(&rig);
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_A), (uint32_t)'A');
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_1), (uint32_t)'1');       /* caps: letters only */
    tap(&rig, DMDRVI_KEY_A);
    libinput_event_t event = next(&rig);
    DMOD_TEST_EXPECT_EQ(event.u.key.modifiers, LIBINPUT_MOD_CAPS);
    drain(&rig);
    tap(&rig, DMDRVI_KEY_LEFT_SHIFT);                                   /* a third tap unlocks */
    drain(&rig);
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_A), (uint32_t)'a');
    rig_close(&rig);
}

DMOD_TEST_STEP(a_keymap_file_adds_layers)
{
    rig_t rig;
    rig_open(&rig, "panel", DMDRVI_INPUT_TYPE_KEYBOARD);
    load(LIBINPUT_KEYMAPS_DIR, "pl.ini", 0);
    post(&rig, DMDRVI_KEY_LEFT_ALT, DMDRVI_KEY_ACTION_DOWN);
    drain(&rig);
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_A), 0x0105u);             /* ą */
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_A + 25u), 0x017Cu);       /* ż: z */
    post(&rig, DMDRVI_KEY_LEFT_SHIFT, DMDRVI_KEY_ACTION_DOWN);
    drain(&rig);
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_A), 0x0104u);             /* Ą */
    post(&rig, DMDRVI_KEY_LEFT_SHIFT, DMDRVI_KEY_ACTION_UP);
    post(&rig, DMDRVI_KEY_LEFT_ALT, DMDRVI_KEY_ACTION_UP);
    drain(&rig);
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_A), (uint32_t)'a');       /* the built in layers are still there */
    rig_close(&rig);
}

DMOD_TEST_STEP(a_keymap_file_declares_modifiers_and_symbols)
{
    rig_t rig;
    rig_open(&rig, "panel", DMDRVI_INPUT_TYPE_KEYBOARD);
    load(LIBINPUT_FIXTURES_DIR, "sym.ini", 0);
    tap(&rig, SYM_KEY);
    drain(&rig);
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_A + 16u), (uint32_t)'#');   /* Q */
    tap(&rig, SYM_KEY);
    drain(&rig);
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_A + 22u), (uint32_t)'1');   /* W */
    tap(&rig, SYM_KEY);
    drain(&rig);
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_A + 4u), (uint32_t)';');    /* E: U+003B */
    post(&rig, SYM_KEY, DMDRVI_KEY_ACTION_DOWN);
    post(&rig, DMDRVI_KEY_LEFT_SHIFT, DMDRVI_KEY_ACTION_DOWN);
    drain(&rig);
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_A + 16u), 0x20ACu);         /* sym+shift: € */
    rig_close(&rig);
}

DMOD_TEST_STEP(a_bad_keymap_leaves_the_old_one)
{
    rig_t rig;
    rig_open(&rig, "panel", DMDRVI_INPUT_TYPE_KEYBOARD);
    load(LIBINPUT_FIXTURES_DIR, "sym.ini", 0);
    load(LIBINPUT_FIXTURES_DIR, "bad.ini", -22);                         /* -EINVAL */
    load(LIBINPUT_FIXTURES_DIR, "there_is_no_such.ini", -2);             /* -ENOENT */
    tap(&rig, SYM_KEY);
    drain(&rig);
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_A + 16u), (uint32_t)'#'); /* still the keymap of sym.ini */
    rig_close(&rig);
}

DMOD_TEST_STEP(an_overflow_forgets_what_was_held)
{
    rig_t rig;
    rig_open(&rig, "panel", DMDRVI_INPUT_TYPE_KEYBOARD);
    post(&rig, DMDRVI_KEY_LEFT_SHIFT, DMDRVI_KEY_ACTION_DOWN);
    drain(&rig);
    post(&rig, 0, DMDRVI_KEY_ACTION_OVERFLOW);                           /* the release may have been lost */
    libinput_event_t event;
    DMOD_TEST_EXPECT_EQ(libinput_read(rig.reader, &event, 0), 0);        /* (no event of its own) */
    DMOD_TEST_EXPECT_EQ(typed(&rig, DMDRVI_KEY_A), (uint32_t)'a');
    rig_close(&rig);
}

DMOD_TEST_STEP(a_state_goes_through_as_it_is)
{
    rig_t rig;
    rig_open(&rig, "other", DMDRVI_INPUT_TYPE_TOUCHSCREEN);
    dmdrvi_input_state_t state;
    memset(&state, 0, sizeof(state));
    state.contact_count = 1;
    state.contacts[0].x = 120;
    state.contacts[0].y = 160;
    DMOD_TEST_EXPECT_EQ(libinput_feed_post_state(rig.feed, &state, 42), 0);
    libinput_event_t event = next(&rig);
    DMOD_TEST_EXPECT_EQ(event.type, LIBINPUT_EVENT_STATE);
    DMOD_TEST_EXPECT_EQ(event.source, LIBINPUT_SOURCE_TOUCH);
    DMOD_TEST_EXPECT_EQ(strcmp(event.group, "other"), 0);
    DMOD_TEST_EXPECT_EQ(event.time_ms, 42u);
    DMOD_TEST_EXPECT_TRUE(dmdrvi_input_state_equal(&event.u.state, &state));
    rig_close(&rig);
}

DMOD_TEST_STEP(events_say_the_group_of_their_device)
{
    rig_t keys, touch;
    rig_open(&keys, "panel", DMDRVI_INPUT_TYPE_KEYBOARD);
    rig_open(&touch, "other", DMDRVI_INPUT_TYPE_TOUCHSCREEN);

    tap(&keys, DMDRVI_KEY_A);                                            /* one reader of all: events of both come */
    dmdrvi_input_state_t state;
    memset(&state, 0, sizeof(state));
    DMOD_TEST_EXPECT_EQ(libinput_feed_post_state(touch.feed, &state, 1), 0);
    libinput_event_t a = next(&keys);
    libinput_event_t b = next(&keys);
    libinput_event_t c = next(&keys);
    DMOD_TEST_EXPECT_EQ(strcmp(a.group, "panel"), 0);
    DMOD_TEST_EXPECT_EQ(strcmp(b.group, "panel"), 0);
    DMOD_TEST_EXPECT_EQ(strcmp(c.group, "other"), 0);
    DMOD_TEST_EXPECT_EQ(c.source, LIBINPUT_SOURCE_TOUCH);

    libinput_feed_t none = libinput_feed_create(NULL, &(dmdrvi_input_info_t){ .type = DMDRVI_INPUT_TYPE_BUTTONS });
    DMOD_TEST_EXPECT_EQ(libinput_feed_post_state(none, &state, 2), 0);   /* no group: an empty name */
    libinput_event_t d = next(&keys);
    DMOD_TEST_EXPECT_EQ((int)d.group[0], 0);
    libinput_feed_destroy(none);

    char long_name[64];
    memset(long_name, 'g', sizeof(long_name) - 1U);
    long_name[sizeof(long_name) - 1U] = '\0';
    libinput_feed_t cut = libinput_feed_create(long_name, &(dmdrvi_input_info_t){ .type = DMDRVI_INPUT_TYPE_BUTTONS });
    DMOD_TEST_EXPECT_EQ(libinput_feed_post_state(cut, &state, 3), 0);    /* cut to what fits */
    libinput_event_t e = next(&keys);
    DMOD_TEST_EXPECT_EQ((int)strlen(e.group), (int)LIBINPUT_GROUP_MAX - 1);
    libinput_feed_destroy(cut);

    drain(&touch);
    libinput_reader_close(keys.reader);
    libinput_feed_destroy(keys.feed);
    rig_close(&touch);
}

DMOD_TEST_STEP(a_node_that_is_not_there_is_not_run)
{
    DMOD_TEST_EXPECT_EQ(libinput_run("/dev/there_is_no_such_node", NULL), -2);   /* -ENOENT */
    DMOD_TEST_EXPECT_EQ(libinput_run("", NULL), -22);                            /* -EINVAL */
}

DMOD_TEST_STEP(nothing_is_kept_for_nobody)
{
    rig_t rig;
    rig_open(&rig, "panel", DMDRVI_INPUT_TYPE_KEYBOARD);
    libinput_reader_close(rig.reader);                                   /* no reader */
    tap(&rig, DMDRVI_KEY_A);
    tap(&rig, DMDRVI_KEY_A);
    rig.reader = libinput_reader_open();                                 /* a new one finds nothing of it */
    DMOD_TEST_EXPECT_NOT_NULL(rig.reader);
    libinput_event_t event;
    DMOD_TEST_EXPECT_EQ(libinput_read(rig.reader, &event, 0), 0);
    rig_close(&rig);
}

DMOD_TEST_STEP(a_full_queue_loses_the_oldest_and_readers_have_their_own)
{
    rig_t rig;
    rig_open(&rig, "panel", DMDRVI_INPUT_TYPE_KEYBOARD);
    libinput_reader_t other = libinput_reader_open();
    DMOD_TEST_EXPECT_NOT_NULL(other);
    for (int i = 0; i < 100; i++)
        post(&rig, DMDRVI_KEY_A, DMDRVI_KEY_ACTION_DOWN);                /* times 1 .. 100 */
    libinput_event_t first = next(&rig);
    DMOD_TEST_EXPECT_EQ(first.time_ms, 69u);                             /* the last 32 are left */
    uint32_t count = 1;
    libinput_event_t event;
    while (libinput_read(rig.reader, &event, 0) == 1)
        count++;
    DMOD_TEST_EXPECT_EQ(count, 32u);
    DMOD_TEST_EXPECT_EQ(libinput_read(other, &event, 0), 1);             /* the other reader has them all of its own */
    DMOD_TEST_EXPECT_EQ(event.time_ms, 69u);
    libinput_reader_close(other);
    rig_close(&rig);
}

DMOD_TEST_STEP(reading_waits_up_to_the_timeout)
{
    rig_t rig;
    rig_open(&rig, "panel", DMDRVI_INPUT_TYPE_KEYBOARD);
    libinput_event_t event;
    DMOD_TEST_EXPECT_EQ(libinput_read(rig.reader, &event, 0), 0);        /* does not wait */
    DMOD_TEST_EXPECT_EQ(libinput_read(rig.reader, &event, 20), 0);       /* nothing came */
    post(&rig, DMDRVI_KEY_A, DMDRVI_KEY_ACTION_DOWN);
    DMOD_TEST_EXPECT_EQ(libinput_read(rig.reader, &event, 20), 1);
    DMOD_TEST_EXPECT_EQ(libinput_read(NULL, &event, 0), -22);            /* -EINVAL */
    rig_close(&rig);
}
