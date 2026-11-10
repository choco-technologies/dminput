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

dmod_libinput_api_declaration(1.0, int, _reader_flush, ( libinput_reader_t reader ))
{
    if (reader == NULL)
        return -EINVAL;
    dmosi_mutex_lock(g_lock);
    int dropped = -ECANCELED;
    if (!reader->closed)
    {
        dropped = (int)reader->count;
        for (uint32_t i = 0; i < reader->count; i++)
            (void)dmosi_semaphore_wait(reader->ready, 1, 0);        /* (a unit of `ready` goes with each event) */
        reader->first = 0;
        reader->count = 0;
    }
    dmosi_mutex_unlock(g_lock);
    return dropped;
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

#define MAX_FEEDS       8

struct libinput_feed
{
    char                group[LIBINPUT_GROUP_MAX];
    char                layout[DMDRVI_INPUT_LAYOUT_MAX];    /* Of a keyboard: from its info */
    char                module[DMDRVI_INPUT_MODULE_MAX];
    uint8_t             type;       /* LIBINPUT_SOURCE_* */
    bool                keyboard;
    uint32_t            id;         /* Never twice: a feed that went and one that came are not the same */
    keymap_t*           keymap;     /* Of a keyboard: its layout and the language, rebuilt when the language changes */
    keystate_t          keys;
};

static struct libinput_feed* g_feeds[MAX_FEEDS];
static uint32_t             g_feed_id;
static char                 g_language[LIBINPUT_LANGUAGE_MAX] = "en";  /* of the keymaps: "" when it is a file */
static char                 g_file[96];                                /* the file that replaces the language ("" when none) */
static bool                 g_language_chosen;  /* libinput_keymap_set_language() or _load() was called (or the environment looked at) */

static void copy_name(char* to, size_t size, const char* from, size_t from_size)
{
    size_t n = 0;
    while (from != NULL && n < from_size && from[n] != '\0' && n + 1U < size)
    {
        to[n] = from[n];
        n++;
    }
    to[n] = '\0';
}

static bool feed_register(struct libinput_feed* feed)
{
    bool done = false;
    dmosi_mutex_lock(g_lock);
    feed->id = ++g_feed_id;
    for (int i = 0; i < MAX_FEEDS && !done; i++)
    {
        if (g_feeds[i] == NULL)
        {
            g_feeds[i] = feed;
            done = true;
        }
    }
    dmosi_mutex_unlock(g_lock);
    return done;
}

dmod_libinput_api_declaration(1.0, libinput_feed_t, _feed_create, ( const char* group, const dmdrvi_input_info_t* info ))
{
    if (info == NULL)
        return NULL;
    struct libinput_feed* s = Dmod_Malloc(sizeof(*s));
    if (s == NULL)
        return NULL;
    memset(s, 0, sizeof(*s));
    copy_name(s->group, sizeof(s->group), group, LIBINPUT_GROUP_MAX);
    s->type = (uint8_t)info->type;
    s->keyboard = (info->capabilities & DMDRVI_INPUT_CAP_KEYS) != 0;
    if (s->keyboard)
    {
        copy_name(s->layout, sizeof(s->layout), info->layout, DMDRVI_INPUT_LAYOUT_MAX);
        copy_name(s->module, sizeof(s->module), info->module, DMDRVI_INPUT_MODULE_MAX);
        dmosi_mutex_lock(g_lock);
        char language[LIBINPUT_LANGUAGE_MAX], file[sizeof(g_file)];
        strcpy(language, g_language);
        strcpy(file, g_file);
        dmosi_mutex_unlock(g_lock);
        s->keymap = layout_build(s->module, s->layout, language, file);
    }
    if ((s->keyboard && s->keymap == NULL) || !feed_register(s))
    {
        keymap_destroy(s->keymap);
        Dmod_Free(s);
        return NULL;
    }
    return s;
}

dmod_libinput_api_declaration(1.0, void, _feed_destroy, ( libinput_feed_t feed ))
{
    if (feed == NULL)
        return;
    dmosi_mutex_lock(g_lock);
    for (int i = 0; i < MAX_FEEDS; i++)
    {
        if (g_feeds[i] == feed)
            g_feeds[i] = NULL;
    }
    dmosi_mutex_unlock(g_lock);
    keymap_destroy(feed->keymap);
    Dmod_Free(feed);
}

dmod_libinput_api_declaration(1.0, int, _feed_post_key, ( libinput_feed_t feed, const dmdrvi_key_event_t* event ))
{
    if (feed == NULL || event == NULL || feed->keymap == NULL)
        return -EINVAL;
    libinput_event_t out;
    memset(&out, 0, sizeof(out));
    dmosi_mutex_lock(g_lock);
    if (event->action == DMDRVI_KEY_ACTION_OVERFLOW)
        keystate_reset(&feed->keys);              /* Events were lost: what was held may be up */
    else if (keystate_feed(&feed->keys, feed->keymap, event, &out))
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

/* ---- The language and the file of the keymaps ---- */

/* Every keyboard gets a keymap of the language/file that is chosen now (built outside the lock: it reads files); a feed that is gone
 * meanwhile, or a keymap that cannot be built, leaves things as they are */
static void keymaps_rebuild(void)
{
    for (int i = 0; i < MAX_FEEDS; i++)
    {
        char layout[DMDRVI_INPUT_LAYOUT_MAX], module[DMDRVI_INPUT_MODULE_MAX], language[LIBINPUT_LANGUAGE_MAX], file[sizeof(g_file)];
        uint32_t id = 0;
        dmosi_mutex_lock(g_lock);
        if (g_feeds[i] != NULL && g_feeds[i]->keyboard)
        {
            id = g_feeds[i]->id;
            strcpy(layout, g_feeds[i]->layout);
            strcpy(module, g_feeds[i]->module);
            strcpy(language, g_language);
            strcpy(file, g_file);
        }
        dmosi_mutex_unlock(g_lock);
        keymap_t* keymap = (id != 0U) ? layout_build(module, layout, language, file) : NULL;
        if (keymap == NULL)
            continue;
        dmosi_mutex_lock(g_lock);
        keymap_t* old = NULL;
        if (g_feeds[i] != NULL && g_feeds[i]->id == id)
        {
            old = g_feeds[i]->keymap;
            g_feeds[i]->keymap = keymap;
            keymap = NULL;
        }
        dmosi_mutex_unlock(g_lock);
        keymap_destroy(old);
        keymap_destroy(keymap);
    }
}

static void language_choose(const char* language, const char* file)
{
    dmosi_mutex_lock(g_lock);
    strcpy(g_language, language);
    strcpy(g_file, file);
    g_language_chosen = true;
    dmosi_mutex_unlock(g_lock);
    keymaps_rebuild();
}

/* The file is read once (and refused when it is not a keymap) before any keyboard uses it */
static int file_check(const char* path)
{
    int status = 0;
    keymap_t* keymap = keymap_create_from_file(path, &status);
    keymap_destroy(keymap);
    return status;
}

dmod_libinput_api_declaration(1.0, int, _keymap_load, ( const char* path ))
{
    if (path != NULL && strlen(path) >= sizeof(g_file))
        return -ENAMETOOLONG;
    int ret = (path != NULL) ? file_check(path) : 0;
    if (ret == 0)
        language_choose((path == NULL) ? "en" : "", (path == NULL) ? "" : path);
    return ret;
}

/* The language of a name like "pl", "pl_PL.UTF-8" or "PL": its letters up to the first '_', '.', '@' or '-', in lower case, into `out`.
 * 0: English ("en", "C", "POSIX", none), 1: another language, -EINVAL: not a language */
static int language_of(const char* name, char* out, size_t size)
{
    size_t n = 0;
    while (name != NULL && name[n] != '\0' && name[n] != '_' && name[n] != '.' && name[n] != '@' && name[n] != '-')
    {
        char c = name[n];
        if (n + 1U >= size || c < 'A' || (c > 'Z' && c < 'a') || c > 'z')
            return -EINVAL;
        out[n++] = (c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    out[n] = '\0';
    return (n == 0U || strcmp(out, "en") == 0 || strcmp(out, "c") == 0 || strcmp(out, "posix") == 0) ? 0 : 1;
}

dmod_libinput_api_declaration(1.0, int, _keymap_set_language, ( const char* language ))
{
    char name[LIBINPUT_LANGUAGE_MAX], path[96];
    int kind = language_of(language, name, sizeof(name));
    if (kind < 0)
        return kind;
    if (kind == 0)
    {
        language_choose("en", "");
        return 0;
    }
    int ret = layout_language_path(path, sizeof(path), name) ? file_check(path) : -EINVAL;
    if (ret == 0)
        language_choose(name, "");
    return ret;
}

dmod_libinput_api_declaration(1.0, int, _keymap_language, ( char* language, size_t size ))
{
    if (language == NULL || size == 0U)
        return -EINVAL;
    dmosi_mutex_lock(g_lock);
    size_t length = strlen(g_language);
    int ret = (length < size) ? 0 : -ERANGE;
    if (ret == 0)
        strcpy(language, g_language);
    dmosi_mutex_unlock(g_lock);
    return ret;
}

/* The language nobody chose: the one of the environment variable LANG, English when there is none; once, unless a language or a
 * file was chosen by libinput_keymap_set_language() or libinput_keymap_load() */
void language_from_environment(void)
{
    dmosi_mutex_lock(g_lock);
    bool chosen = g_language_chosen;
    g_language_chosen = true;
    dmosi_mutex_unlock(g_lock);
    const char* lang = Dmod_GetEnv("LANG");
    if (chosen || lang == NULL)
        return;
    int ret = libinput_keymap_set_language(lang);
    if (ret != 0)
        DMOD_LOG_WARN("libinput: no keymap for the language %s (%d), English is used\n", lang, ret);
}

/* ---- Module ---- */

int dmod_init(const Dmod_Config_t* Config)
{
    (void)Config;
    g_lock = dmosi_mutex_create(false);
    return (g_lock != NULL) ? 0 : -ENOMEM;
}

int dmod_deinit(void)
{
    for (int i = 0; i < MAX_READERS; i++)
    {
        if (g_readers[i] != NULL)
            libinput_reader_close(g_readers[i]);
    }
    if (g_lock != NULL)
        dmosi_mutex_destroy(g_lock);
    g_lock = NULL;
    return 0;
}
