#include "private.h"
#include "dmvfs.h"
#include <errno.h>
#include <string.h>

#define WAIT_MS         500u    /* A request to stop is looked for this often */
#define RETRY_MS        50u     /* A node that fails to wait: not a busy loop */

/* What is waiting at the node goes to the readers */
static void drain(void* node, const dmdrvi_input_info_t* info, libinput_feed_t feed)
{
    if ((info->capabilities & DMDRVI_INPUT_CAP_KEYS) != 0)
    {
        dmdrvi_key_events_t block;
        if (Dmod_FileRead(&block, 1, sizeof(block), node) != sizeof(block))
            return;
        for (uint32_t i = 0; i < block.count && i < DMDRVI_KEY_MAX_EVENTS; i++)
            (void)libinput_feed_post_key(feed, &block.events[i]);
        return;
    }
    dmdrvi_input_state_t state;
    if (Dmod_FileRead(&state, 1, sizeof(state), node) == sizeof(state))
        (void)libinput_feed_post_state(feed, &state, dmosi_get_tick_count());
}

static void loop(void* node, const dmdrvi_input_info_t* info, libinput_feed_t feed, libinput_stop_t should_stop)
{
    while (should_stop == NULL || !should_stop())
    {
        uint32_t wait = WAIT_MS;
        int ret = Dmod_Ioctl(node, DMDRVI_IOCTL_INPUT_WAIT_EVENT, &wait);
        if (ret == 0)
            drain(node, info, feed);
        else if (ret != -ETIMEDOUT)
            dmosi_thread_sleep(RETRY_MS);
    }
}

dmod_libinput_api_declaration(1.0, int, _run, ( const char* path, libinput_stop_t should_stop ))
{
    if (path == NULL || path[0] == '\0')
        return -EINVAL;
    void* node = Dmod_FileOpen(path, "r");
    if (node == NULL)
        return -ENOENT;
    dmdrvi_input_info_t info;
    memset(&info, 0, sizeof(info));              /* (a driver that does not know the layout leaves it empty) */
    if (Dmod_Ioctl(node, DMDRVI_IOCTL_INPUT_GET_INFO, &info) != 0)
    {
        Dmod_FileClose(node);
        return -ENOTTY;                         /* Not an input device */
    }
    dmdrvi_devfs_group_t group;
    if (Dmod_Ioctl(node, DMDRVI_IOCTL_DEVFS_GET_GROUP, &group) != 0)
        group.name[0] = '\0';                  /* (no group, or a dmdevfs that does not tell it) */
    if ((info.capabilities & DMDRVI_INPUT_CAP_KEYS) != 0)
        language_from_environment();                /* (before the keyboard gets its keymap) */
    libinput_feed_t feed = libinput_feed_create(group.name, &info);
    if (feed == NULL)
    {
        Dmod_FileClose(node);
        return -ENOMEM;
    }
    DMOD_LOG_INFO("libinput: %s (%s, group \"%s\") is read\n", path, info.name, group.name);
    loop(node, &info, feed, should_stop);
    libinput_feed_destroy(feed);
    Dmod_FileClose(node);
    return 0;
}
