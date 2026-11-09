#include "dmod.h"
#include "libinput.h"
#include "libsystemd.h"
#include <errno.h>
#include <string.h>

/**
 * @brief inputd - the service of one input device.
 *
 * Started per device by the rule `[class=input] start=inputd@%name` (configs/inputd.rules; dmdevfs
 * reports every node answering DMDRVI_IOCTL_INPUT_GET_INFO) with the device's name and node path.
 * It is one call: libinput_run() reads the node - it is the one thread of the device - until
 * libsystemd asks the service to stop. Everything else is libinput's.
 */
int main(int argc, char* argv[])
{
    if (argc < 2 || argv[1][0] == '\0')
    {
        Dmod_Printf("Usage: inputd <device name> [<device node>]\n");
        Dmod_Printf("Started per device from inputd@.ini - see inputd.rules\n");
        return -EINVAL;
    }
    char* default_path = Dmod_Malloc(strlen(argv[1]) + 6U);
    if (default_path == NULL)
        return -ENOMEM;
    strcpy(default_path, "/dev/");
    strcat(default_path, argv[1]);

    int ret = libinput_run((argc > 2 && argv[2][0] != '\0') ? argv[2] : default_path, libsystemd_stop_requested);
    if (ret != 0)
        DMOD_LOG_ERROR("inputd: %s is not read (%d)\n", argv[1], ret);
    Dmod_Free(default_path);
    return ret;
}
