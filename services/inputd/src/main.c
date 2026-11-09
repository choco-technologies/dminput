#include "dmod.h"
#include "dmosi.h"
#include "libinput.h"
#include "libsystemd.h"
#include "dmvfs.h"
#include <errno.h>
#include <string.h>

/**
 * @brief inputd - the service of one input device.
 *
 * Started per device by the rule `[class=input] start=inputd@%name` (configs/inputd.rules; dmdevfs
 * reports every node answering DMDRVI_IOCTL_INPUT_GET_INFO) with the device's name and node path.
 *
 * It registers the device in libinput, waits on the node for what the device reports and posts it
 * to libinput: the raw keys of a keyboard (DMDRVI_IOCTL_INPUT_KEY_READ), the state of a touch panel, a
 * mouse or buttons. It translates nothing - that is libinput's - and it is the one thread the device
 * has. The keymap of keyboards is $DMINPUT_KEYMAP (a file, see docs/keymap.md); without it, the built in one.
 */

#define WAIT_MS         500u    /* A stop request is looked for this often */
#define RETRY_MS        50      /* A device that fails to wait: not a busy loop */

typedef struct
{
    const char*         name;
    const char*         path;
    void*               node;
    dmdrvi_input_info_t info;
    libinput_device_t   device;
    dmosi_semaphore_t   wakeup;     /* The stop request, and a pause */
} service_t;

static bool open_node(service_t* s)
{
    s->node = Dmod_FileOpen(s->path, "r");
    if (s->node == NULL)
    {
        DMOD_LOG_ERROR("inputd: cannot open %s\n", s->path);
        return false;
    }
    if (Dmod_Ioctl(s->node, DMDRVI_IOCTL_INPUT_GET_INFO, &s->info) != 0)
    {
        DMOD_LOG_ERROR("inputd: %s is not an input device\n", s->path);
        return false;
    }
    return true;
}

/* What is waiting at the node goes to libinput */
static void drain(service_t* s)
{
    if ((s->info.capabilities & DMDRVI_INPUT_CAP_KEYS) != 0)
    {
        dmdrvi_key_events_t block;
        if (Dmod_FileRead(&block, 1, sizeof(block), s->node) != sizeof(block))
            return;
        for (uint32_t i = 0; i < block.count && i < DMDRVI_KEY_MAX_EVENTS; i++)
            (void)libinput_post_key(s->device, &block.events[i]);
        return;
    }
    dmdrvi_input_state_t state;
    if (Dmod_FileRead(&state, 1, sizeof(state), s->node) == sizeof(state))
        (void)libinput_post_state(s->device, &state, dmosi_get_tick_count());
}

static void run(service_t* s)
{
    while (!libsystemd_stop_requested())
    {
        uint32_t wait = WAIT_MS;
        int ret = Dmod_Ioctl(s->node, DMDRVI_IOCTL_INPUT_WAIT_EVENT, &wait);
        if (ret == 0)
            drain(s);
        else if (ret != -ETIMEDOUT)
            (void)dmosi_semaphore_wait(s->wakeup, 1, RETRY_MS);
    }
}

static int setup(service_t* s)
{
    if (!open_node(s))
        return -ENODEV;
    int ret = libinput_device_add(s->path, &s->info, &s->device);
    if (ret != 0)
    {
        DMOD_LOG_ERROR("inputd: %s is not registered (%d)\n", s->path, ret);
        return ret;
    }
    const char* keymap = Dmod_GetEnv("DMINPUT_KEYMAP");
    if ((s->info.capabilities & DMDRVI_INPUT_CAP_KEYS) != 0 && keymap != NULL && keymap[0] != '\0' &&
        libinput_keymap_load(keymap) != 0)
        DMOD_LOG_WARN("inputd: keymap %s is not used\n", keymap);
    DMOD_LOG_INFO("inputd: %s (%s) goes to libinput\n", s->path, s->info.name);
    return 0;
}

int main(int argc, char* argv[])
{
    service_t s;
    memset(&s, 0, sizeof(s));
    if (argc < 2 || argv[1][0] == '\0')
    {
        Dmod_Printf("Usage: inputd <device name> [<device node>]\n");
        Dmod_Printf("Started per device from inputd@.ini - see inputd.rules\n");
        return -EINVAL;
    }
    s.name = argv[1];
    char* default_path = Dmod_Malloc(strlen(s.name) + 6U);
    if (default_path == NULL)
        return -ENOMEM;
    strcpy(default_path, "/dev/");
    strcat(default_path, s.name);
    s.path = (argc > 2 && argv[2][0] != '\0') ? argv[2] : default_path;

    s.wakeup = dmosi_semaphore_create(0, 1);
    int ret = (s.wakeup != NULL) ? setup(&s) : -ENOMEM;
    if (ret == 0)
    {
        libsystemd_set_stop_semaphore(s.wakeup);
        run(&s);
        libsystemd_set_stop_semaphore(NULL);
        libinput_device_remove(s.device);
    }
    if (s.node != NULL)
        Dmod_FileClose(s.node);
    if (s.wakeup != NULL)
        dmosi_semaphore_destroy(s.wakeup);
    Dmod_Free(default_path);
    return ret;
}
