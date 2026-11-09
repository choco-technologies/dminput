#define DMOD_ENABLE_REGISTRATION ON
#include "private.h"
#include <errno.h>
#include <string.h>

#define MAX_DEVICES     16
#define MAX_READERS     4
#define QUEUE_SIZE      32

typedef struct
{
    libinput_device_t   id;                 /* 0: the slot is free */
    char*               path;
    uint8_t             source;
    keystate_t          keys;
} device_t;

struct libinput_reader
{
    bool                used;
    libinput_event_t    events[QUEUE_SIZE];
    uint32_t            first, count;
    dmosi_semaphore_t   ready;              /* One unit per queued event (and one more to wake a waiter on close) */
    uint32_t            waiters;
    bool                closed;             /* Out of the list, freed by the last waiter */
};

static dmosi_mutex_t        g_lock;
static device_t             g_devices[MAX_DEVICES];
static struct libinput_reader* g_readers[MAX_READERS];
static keymap_t*            g_keymap;
static libinput_device_t    g_next_device = 1;

/* ---- Readers: queues ---- */

static void reader_free(struct libinput_reader* r)
{
    if (r->ready != NULL)
        dmosi_semaphore_destroy(r->ready);
    Dmod_Free(r);
}

/* An event for every reader; under g_lock. A full queue loses its oldest event. */
static void queue_event(const libinput_event_t* event)
{
    for (int i = 0; i < MAX_READERS; i++)
    {
        struct libinput_reader* r = g_readers[i];
        if (r == NULL)
            continue;
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
    dmosi_mutex_lock(g_lock);
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

/* ---- Devices ---- */

static device_t* device_at(libinput_device_t id)
{
    for (int i = 0; id != 0 && i < MAX_DEVICES; i++)
    {
        if (g_devices[i].id == id)
            return &g_devices[i];
    }
    return NULL;
}

dmod_libinput_api_declaration(1.0, int, _device_add, ( const char* path, const dmdrvi_input_info_t* info, libinput_device_t* device ))
{
    if (path == NULL || path[0] == '\0' || info == NULL)
        return -EINVAL;
    int ret = -ENOMEM;
    dmosi_mutex_lock(g_lock);
    device_t* free_slot = NULL;
    for (int i = 0; i < MAX_DEVICES; i++)
    {
        if (g_devices[i].id != 0 && strcmp(g_devices[i].path, path) == 0)
            ret = -EEXIST;
        else if (g_devices[i].id == 0 && free_slot == NULL)
            free_slot = &g_devices[i];
    }
    if (ret != -EEXIST && free_slot != NULL)
    {
        free_slot->path = Dmod_StrDup(path);
        if (free_slot->path != NULL)
        {
            free_slot->source = (uint8_t)info->type;
            keystate_reset(&free_slot->keys);
            free_slot->id = g_next_device++;
            if (g_next_device == 0)
                g_next_device = 1;
            if (device != NULL)
                *device = free_slot->id;
            ret = 0;
        }
    }
    dmosi_mutex_unlock(g_lock);
    return ret;
}

dmod_libinput_api_declaration(1.0, void, _device_remove, ( libinput_device_t device ))
{
    dmosi_mutex_lock(g_lock);
    device_t* d = device_at(device);
    if (d != NULL)
    {
        Dmod_Free(d->path);
        memset(d, 0, sizeof(*d));
    }
    dmosi_mutex_unlock(g_lock);
}

dmod_libinput_api_declaration(1.0, libinput_device_t, _device_find, ( const char* path ))
{
    libinput_device_t found = 0;
    if (path == NULL)
        return 0;
    dmosi_mutex_lock(g_lock);
    for (int i = 0; i < MAX_DEVICES; i++)
    {
        if (g_devices[i].id != 0 && strcmp(g_devices[i].path, path) == 0)
            found = g_devices[i].id;
    }
    dmosi_mutex_unlock(g_lock);
    return found;
}

/* ---- What the devices post ---- */

dmod_libinput_api_declaration(1.0, int, _post_key, ( libinput_device_t device, const dmdrvi_key_event_t* event ))
{
    if (event == NULL)
        return -EINVAL;
    int ret = 0;
    dmosi_mutex_lock(g_lock);
    device_t* d = device_at(device);
    libinput_event_t out;
    memset(&out, 0, sizeof(out));
    if (d == NULL)
        ret = -ENODEV;
    else if (event->action == DMDRVI_KEY_ACTION_OVERFLOW)
        keystate_reset(&d->keys);               /* Events were lost: what was held may be up */
    else if (keystate_feed(&d->keys, g_keymap, event, &out))
    {
        out.device = d->id;
        queue_event(&out);
    }
    dmosi_mutex_unlock(g_lock);
    return ret;
}

dmod_libinput_api_declaration(1.0, int, _post_state, ( libinput_device_t device, const dmdrvi_input_state_t* state, uint32_t now_ms ))
{
    if (state == NULL)
        return -EINVAL;
    int ret = 0;
    dmosi_mutex_lock(g_lock);
    device_t* d = device_at(device);
    if (d == NULL)
        ret = -ENODEV;
    else
    {
        libinput_event_t out;
        memset(&out, 0, sizeof(out));
        out.time_ms = now_ms;
        out.device = d->id;
        out.source = d->source;
        out.type = LIBINPUT_EVENT_STATE;
        out.u.state = *state;
        queue_event(&out);
    }
    dmosi_mutex_unlock(g_lock);
    return ret;
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
    for (int i = 0; i < MAX_DEVICES; i++)
        keystate_reset(&g_devices[i].keys);     /* (what they held was by the old keymap) */
    dmosi_mutex_unlock(g_lock);
    keymap_destroy(old);
    return 0;
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
    for (int i = 0; i < MAX_DEVICES; i++)
        libinput_device_remove(g_devices[i].id);
    keymap_destroy(g_keymap);
    g_keymap = NULL;
    if (g_lock != NULL)
        dmosi_mutex_destroy(g_lock);
    g_lock = NULL;
    return 0;
}
