#include "private.h"
#include <errno.h>
#include <string.h>

/* Where the files are: <cfg>/keymaps/libinput/<language>.ini, <cfg>/layouts/<module>/<layout>.ini, <cfg>/layouts/libinput/<layout>.ini */

#define MAX_EXTENDS     4       /* A layout extends a layout that extends a layout... this deep */

static bool append(char* out, size_t size, const char* text)
{
    size_t n = strlen(out), m = strlen(text);
    if (n + m + 1U > size)
        return false;
    memcpy(out + n, text, m + 1U);
    return true;
}

/* A name of a file: lower case letters, digits, '_' and '-' - no way out of the directory */
bool layout_name_ok(const char* name)
{
    size_t n = strlen(name);
    if (n == 0U || n >= LIBINPUT_LANGUAGE_MAX)
        return false;
    for (size_t i = 0; i < n; i++)
    {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

/* <cfg>/<kind>/<directory>/<name>.ini */
static bool file_path(char* out, size_t size, const char* kind, const char* directory, const char* name)
{
    const char* cfg = Dmod_GetEnv("DMINPUT_CFG_DIR");
    out[0] = '\0';
    return append(out, size, (cfg != NULL && cfg[0] != '\0') ? cfg : "/cfg") && append(out, size, "/") && append(out, size, kind) &&
           append(out, size, "/") && append(out, size, directory) && append(out, size, "/") && append(out, size, name) &&
           append(out, size, ".ini");
}

bool layout_language_path(char* out, size_t size, const char* language)
{
    return layout_name_ok(language) && file_path(out, size, "keymaps", "libinput", language);
}

static bool exists(const char* path)
{
    void* file = Dmod_FileOpen(path, "r");
    if (file == NULL)
        return false;
    Dmod_FileClose(file);
    return true;
}

/* The file of a layout: the one the module of the driver ships, then the ones libinput ships */
static int find_layout(char* out, size_t size, const char* module, const char* layout)
{
    if (!layout_name_ok(layout))
        return -EINVAL;
    if (module[0] != '\0' && layout_name_ok(module) && file_path(out, size, "layouts", module, layout) && exists(out))
        return 0;
    return (file_path(out, size, "layouts", "libinput", layout) && exists(out)) ? 0 : -ENOENT;
}

/* The layout (the layouts it extends first) on top of `km` */
static int apply_layout(keymap_t* km, const char* module, const char* layout, int depth)
{
    char path[96], parent[LIBINPUT_LANGUAGE_MAX];
    if (depth > MAX_EXTENDS)
        return -ELOOP;
    if (strcmp(layout, "full") == 0)
        return 0;                                       /* An ordinary keyboard: the built in keymap */
    int ret = find_layout(path, sizeof(path), module, layout);
    if (ret == 0 && keymap_file_extends(path, parent, sizeof(parent)))
        ret = apply_layout(km, module, parent, depth + 1);
    return (ret == 0) ? keymap_apply_file(km, path) : ret;
}

keymap_t* layout_build(const char* module, const char* layout, const char* language, const char* file)
{
    keymap_t* km = keymap_create();
    char path[96];
    if (km == NULL)
        return NULL;
    if (layout[0] != '\0' && apply_layout(km, module, layout, 0) != 0)
    {
        DMOD_LOG_WARN("libinput: no layout %s (of the module \"%s\"), an ordinary keyboard is used\n", layout, module);
        keymap_destroy(km);
        km = keymap_create();                           /* A layout that was half read is not used */
        if (km == NULL)
            return NULL;
    }
    if (file[0] != '\0')
        (void)keymap_apply_file(km, file);              /* (it was read once when it was chosen) */
    else if (language[0] != '\0' && layout_language_path(path, sizeof(path), language))
        (void)keymap_apply_file(km, path);
    return km;
}
