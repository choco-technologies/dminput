#define DMOD_ENABLE_REGISTRATION ON
#include "private.h"
#include <errno.h>
#include <string.h>

#define MAX_READERS     4
#define QUEUE_SIZE      32

struct libinput_reader
{
    dmosi_process_id_t  pid;                /* Of its owner: a reader of a process that is gone is dropped */
    libinput_event_t    events[QUEUE_SIZE];
    uint32_t            first, count;
    dmosi_semaphore_t   ready;              /* One unit per queued event (and one more to wake a waiter on close) */
    uint32_t            waiters;
    bool                closed;             /* Out of the list, freed by the last waiter */
};

static dmosi_mutex_t        g_lock;
static struct libinput_reader* g_readers[MAX_READERS];
static keymap_t*            g_keymap;
static bool                 g_keymap_chosen;    /* libinput_keymap_load() was called (or the environment looked at) */

/* ---- Readers: queues ---- */

static void reader_free(struct libinput_reader* r)
{
    if (r->ready != NULL)
        dmosi_semaphore_destroy(r->ready);
    Dmod_Free(r);
}

static dmosi_process_id_t current_pid(void)
{
    dmosi_process_t process = dmosi_process_current();
    return (process != NULL) ? dmosi_process_get_id(process) : 0;
}

/* A reader of a process that is gone (it did not close it): out of the list; under g_lock */
static bool owner_gone(const struct libinput_reader* r)
{
    return r->pid != 0 && dmosi_process_find_by_id(r->pid) == NULL;
}

static void drop_dead_readers(void)
{
    for (int i = 0; i < MAX_READERS; i++)
    {
        struct libinput_reader* r = g_readers[i];
        if (r != NULL && owner_gone(r))
        {
            g_readers[i] = NULL;
            r->closed = true;
            if (r->waiters == 0)
                reader_free(r);
            else
                (void)dmosi_semaphore_post(r->ready, 1);
        }
    }
}

/* An event for every reader; under g_lock. A full queue loses its oldest event. */
static void queue_event(const libinput_event_t* event)
{
    for (int i = 0; i < MAX_READERS; i++)
    {
        struct libinput_reader* r = g_readers[i];
        if (r == NULL)
            continue;
        if (r->count == QUEUE_SIZE && owner_gone(r))
        {
            drop_dead_readers();                /* Nobody reads it: it would only keep the slot */
            continue;
        }
        if (r->count == QUEUE_SIZE)
        {
            r->first = (r->first + 1U) % QUEUE_SIZE;
            r->count--;
            (void)dmosi_semaphore_wait(r->ready, 1, 0);      /* (its unit goes with it) */
        }
        r->events[(r->first + r->count) % QUEUE_SIZE] = *event;
        r->count++;
        (void)dmosi_semaphore_post(r->ready, 1);
    }
}

dmod_libinput_api_declaration(1.0, libinput_reader_t, _reader_open, ( void ))
{
    struct libinput_reader* r = Dmod_Malloc(sizeof(*r));
    if (r == NULL)
        return NULL;
    memset(r, 0, sizeof(*r));
    r->ready = dmosi_semaphore_create(0, QUEUE_SIZE + 1U);
    if (r->ready == NULL)
    {
        Dmod_Free(r);
        return NULL;
    }
    r->pid = current_pid();
    dmosi_mutex_lock(g_lock);
    drop_dead_readers();
    for (int i = 0; i < MAX_READERS; i++)
    {
        if (g_readers[i] == NULL)
        {
            g_readers[i] = r;
            dmosi_mutex_unlock(g_lock);
            return r;
        }
    }
    dmosi_mutex_unlock(g_lock);
    reader_free(r);
    return NULL;
}

dmod_libinput_api_declaration(1.0, void, _reader_close, ( libinput_reader_t reader ))
{
    if (reader == NULL)
        return;
    dmosi_mutex_lock(g_lock);
    for (int i = 0; i < MAX_READERS; i++)
    {
        if (g_readers[i] == reader)
            g_readers[i] = NULL;
    }
    reader->closed = true;
    bool free_now = reader->waiters == 0;
    if (!free_now)
        (void)dmosi_semaphore_post(reader->ready, 1);       /* Wakes the waiter, which frees it */
    dmosi_mutex_unlock(g_lock);
    if (free_now)
        reader_free(reader);
}

/* Take the oldest event; under g_lock, a unit of `ready` taken */
static void take_event(struct libinput_reader* r, libinput_event_t* event)
{
    *event = r->events[r->first];
    r->first = (r->first + 1U) % QUEUE_SIZE;
    r->count--;
}

dmod_libinput_api_declaration(1.0, int, _read, ( libinput_reader_t reader, libinput_event_t* event, uint32_t timeout_ms ))
{
    if (reader == NULL || event == NULL)
        return -EINVAL;
    int32_t wait = (timeout_ms == LIBINPUT_FOREVER) ? -1 : (int32_t)timeout_ms;
    dmosi_mutex_lock(g_lock);
    if (reader->closed)
    {
        dmosi_mutex_unlock(g_lock);
        return -ECANCELED;
    }
    reader->waiters++;
    dmosi_mutex_unlock(g_lock);

    int got = dmosi_semaphore_wait(reader->ready, 1, wait);

    dmosi_mutex_lock(g_lock);
    reader->waiters--;
    int ret = 0;
    if (reader->closed)
        ret = -ECANCELED;
    else if (got == 0 && reader->count > 0)
    {
        take_event(reader, event);
        ret = 1;
    }
    bool free_now = reader->closed && reader->waiters == 0;
    dmosi_mutex_unlock(g_lock);
    if (free_now)
        reader_free(reader);
    return ret;
}

/* ---- Feeds: the state of one device, kept by whoever reads it ---- */

struct libinput_feed
{
    char                group[LIBINPUT_GROUP_MAX];
    uint8_t             type;       /* LIBINPUT_SOURCE_* */
    keystate_t          keys;
};

dmod_libinput_api_declaration(1.0, libinput_feed_t, _feed_create, ( const char* group, const dmdrvi_input_info_t* info ))
{
    if (info == NULL)
        return NULL;
    struct libinput_feed* s = Dmod_Malloc(sizeof(*s));
    if (s == NULL)
        return NULL;
    memset(s, 0, sizeof(*s));
    if (group != NULL)
    {
        strncpy(s->group, group, sizeof(s->group) - 1U);
        s->group[sizeof(s->group) - 1U] = '\0';
    }
    s->type = (uint8_t)info->type;
    return s;
}

dmod_libinput_api_declaration(1.0, void, _feed_destroy, ( libinput_feed_t feed ))
{
    if (feed != NULL)
        Dmod_Free(feed);
}

dmod_libinput_api_declaration(1.0, int, _feed_post_key, ( libinput_feed_t feed, const dmdrvi_key_event_t* event ))
{
    if (feed == NULL || event == NULL)
        return -EINVAL;
    libinput_event_t out;
    memset(&out, 0, sizeof(out));
    dmosi_mutex_lock(g_lock);
    if (event->action == DMDRVI_KEY_ACTION_OVERFLOW)
        keystate_reset(&feed->keys);              /* Events were lost: what was held may be up */
    else if (keystate_feed(&feed->keys, g_keymap, event, &out))
    {
        memcpy(out.group, feed->group, sizeof(out.group));
        queue_event(&out);
    }
    dmosi_mutex_unlock(g_lock);
    return 0;
}

dmod_libinput_api_declaration(1.0, int, _feed_post_state, ( libinput_feed_t feed, const dmdrvi_input_state_t* state, uint32_t now_ms ))
{
    if (feed == NULL || state == NULL)
        return -EINVAL;
    libinput_event_t out;
    memset(&out, 0, sizeof(out));
    out.time_ms = now_ms;
    memcpy(out.group, feed->group, sizeof(out.group));
    out.source = feed->type;
    out.type = LIBINPUT_EVENT_STATE;
    out.u.state = *state;
    dmosi_mutex_lock(g_lock);
    queue_event(&out);
    dmosi_mutex_unlock(g_lock);
    return 0;
}

dmod_libinput_api_declaration(1.0, int, _keymap_load, ( const char* path ))
{
    int status = 0;
    keymap_t* keymap = (path == NULL) ? keymap_create() : keymap_create_from_file(path, &status);
    if (keymap == NULL)
        return (status != 0) ? status : -ENOMEM;
    dmosi_mutex_lock(g_lock);
    keymap_t* old = g_keymap;
    g_keymap = keymap;
    g_keymap_chosen = true;
    dmosi_mutex_unlock(g_lock);
    keymap_destroy(old);
    return 0;
}

/* The keymap nobody chose: the file of $DMINPUT_KEYMAP, else the one a board ships (LIBINPUT_BOARD_KEYMAP) - once, unless a keymap
 * was chosen by libinput_keymap_load() */
void keymap_from_environment(void)
{
    dmosi_mutex_lock(g_lock);
    bool chosen = g_keymap_chosen;
    g_keymap_chosen = true;
    dmosi_mutex_unlock(g_lock);
    if (chosen)
        return;
    const char* path = Dmod_GetEnv("DMINPUT_KEYMAP");
    bool given = path != NULL && path[0] != '\0';
    int ret = libinput_keymap_load(given ? path : LIBINPUT_BOARD_KEYMAP);
    if (ret != 0 && (given || ret != -ENOENT))
        DMOD_LOG_WARN("libinput: the keymap %s is not used (%d)\n", given ? path : LIBINPUT_BOARD_KEYMAP, ret);
}

/* ---- Module ---- */

int dmod_init(const Dmod_Config_t* Config)
{
    (void)Config;
    g_lock = dmosi_mutex_create(false);
    g_keymap = keymap_create();
    return (g_lock != NULL && g_keymap != NULL) ? 0 : -ENOMEM;
}

int dmod_deinit(void)
{
    for (int i = 0; i < MAX_READERS; i++)
    {
        if (g_readers[i] != NULL)
            libinput_reader_close(g_readers[i]);
    }
    keymap_destroy(g_keymap);
    g_keymap = NULL;
    if (g_lock != NULL)
        dmosi_mutex_destroy(g_lock);
    g_lock = NULL;
    return 0;
}
