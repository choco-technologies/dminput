#include "private.h"
#include "dmini.h"
#include <errno.h>
#include <string.h>

#define LAYERS      (1u << MOD_COUNT)

typedef struct
{
    uint32_t    codepoint;
    uint16_t    code;
    uint8_t     layer;
} entry_t;

struct keymap
{
    entry_t*    entries;
    uint32_t    count;
    uint32_t    capacity;
    uint16_t    mod_codes[MOD_COUNT][MOD_MAX_CODES];    /* 0: not used */
    uint8_t     sticky;                                 /* Modifiers (1 << MOD_*) a tap of arms */
    uint8_t     lockable;                               /* ... and a second tap locks */
};

/* ---- The built in layers ---- */

/* US: the character of a key, shifted or not (0: it gives none - Enter, an arrow, a modifier) */
static uint32_t builtin_char(uint16_t code, bool shift)
{
    static const char digits_shifted[] = ")!@#$%^&*(";
    if (key_is_letter(code))
        return (uint32_t)((shift ? 'A' : 'a') + (code - DMDRVI_KEY_A));
    if (code >= DMDRVI_KEY_1 && code <= DMDRVI_KEY_0)
    {
        uint32_t digit = (code == DMDRVI_KEY_0) ? 0u : (uint32_t)(code - DMDRVI_KEY_1 + 1u);
        return shift ? (uint32_t)digits_shifted[digit] : (uint32_t)('0' + digit);
    }
    switch (code)
    {
        case DMDRVI_KEY_SPACE: return ' ';
        case 0x2D: return shift ? '_' : '-';
        case 0x2E: return shift ? '+' : '=';
        case 0x2F: return shift ? '{' : '[';
        case 0x30: return shift ? '}' : ']';
        case 0x31: return shift ? '|' : '\\';
        case 0x33: return shift ? ':' : ';';
        case 0x34: return shift ? '"' : '\'';
        case 0x35: return shift ? '~' : '`';
        case 0x36: return shift ? '<' : ',';
        case 0x37: return shift ? '>' : '.';
        case 0x38: return shift ? '?' : '/';
        default:   return 0;
    }
}

keymap_t* keymap_create(void)
{
    keymap_t* km = Dmod_Malloc(sizeof(*km));
    if (km == NULL)
        return NULL;
    memset(km, 0, sizeof(*km));
    km->mod_codes[MOD_SHIFT][0] = DMDRVI_KEY_LEFT_SHIFT;
    km->mod_codes[MOD_SHIFT][1] = DMDRVI_KEY_RIGHT_SHIFT;
    km->mod_codes[MOD_CTRL][0] = DMDRVI_KEY_LEFT_CTRL;
    km->mod_codes[MOD_ALT][0] = DMDRVI_KEY_LEFT_ALT;
    return km;
}

void keymap_destroy(keymap_t* km)
{
    if (km == NULL)
        return;
    if (km->entries != NULL)
        Dmod_Free(km->entries);
    Dmod_Free(km);
}

int keymap_modifier(const keymap_t* km, uint16_t code)
{
    for (int m = 0; m < MOD_COUNT; m++)
    {
        for (int i = 0; i < MOD_MAX_CODES; i++)
        {
            if (km->mod_codes[m][i] == code)
                return m;
        }
    }
    return -1;
}

bool keymap_sticky(const keymap_t* km, int m)
{
    return (km->sticky & (1u << m)) != 0;
}

bool keymap_lockable(const keymap_t* km, int m)
{
    return (km->lockable & (1u << m)) != 0;
}

uint32_t keymap_char(const keymap_t* km, uint16_t code, uint8_t layer)
{
    for (uint32_t i = km->count; i > 0; i--)            /* The newest entry of a key wins */
    {
        if (km->entries[i - 1U].code == code && km->entries[i - 1U].layer == layer)
            return km->entries[i - 1U].codepoint;
    }
    if (layer == 0)
        return builtin_char(code, false);
    return (layer == (1u << MOD_SHIFT)) ? builtin_char(code, true) : 0;
}

/* ---- The text of a file ---- */

static bool word_is(const char* text, size_t length, const char* word)
{
    return strlen(word) == length && strncmp(text, word, length) == 0;
}

/* A number: 0x.. or decimal; false when there is anything else in it */
static bool parse_number(const char* text, size_t length, uint32_t* value)
{
    uint32_t v = 0;
    size_t i = 0;
    uint32_t base = 10;
    if (length > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        base = 16;
        i = 2;
    }
    if (i >= length)
        return false;
    for (; i < length; i++)
    {
        char c = text[i];
        uint32_t d = (c >= '0' && c <= '9') ? (uint32_t)(c - '0') :
                     (base == 16 && c >= 'a' && c <= 'f') ? (uint32_t)(c - 'a' + 10) :
                     (base == 16 && c >= 'A' && c <= 'F') ? (uint32_t)(c - 'A' + 10) : 99u;
        if (d >= base)
            return false;
        v = v * base + d;
    }
    *value = v;
    return true;
}

/* The name of a key: a letter or a digit, ENTER, SPACE, ..., or its code */
static bool parse_key(const char* text, size_t length, uint16_t* code)
{
    uint32_t v;
    if (length == 1 && text[0] >= 'A' && text[0] <= 'Z')
        *code = (uint16_t)(DMDRVI_KEY_A + (text[0] - 'A'));
    else if (length == 1 && text[0] >= '1' && text[0] <= '9')
        *code = (uint16_t)(DMDRVI_KEY_1 + (text[0] - '1'));
    else if (length == 1 && text[0] == '0')
        *code = DMDRVI_KEY_0;
    else if (word_is(text, length, "ENTER"))
        *code = DMDRVI_KEY_ENTER;
    else if (word_is(text, length, "ESC"))
        *code = DMDRVI_KEY_ESCAPE;
    else if (word_is(text, length, "BACKSPACE"))
        *code = DMDRVI_KEY_BACKSPACE;
    else if (word_is(text, length, "TAB"))
        *code = DMDRVI_KEY_TAB;
    else if (word_is(text, length, "SPACE"))
        *code = DMDRVI_KEY_SPACE;
    else if (word_is(text, length, "DEL"))
        *code = DMDRVI_KEY_DELETE;
    else if (word_is(text, length, "RIGHT"))
        *code = DMDRVI_KEY_RIGHT;
    else if (word_is(text, length, "LEFT"))
        *code = DMDRVI_KEY_LEFT;
    else if (word_is(text, length, "DOWN"))
        *code = DMDRVI_KEY_DOWN;
    else if (word_is(text, length, "UP"))
        *code = DMDRVI_KEY_UP;
    else if (parse_number(text, length, &v) && v > 0 && v < 0x10000u)
        *code = (uint16_t)v;
    else
        return false;
    return true;
}

/* A character: U+XXXX, or UTF-8 (the whole text is one character) */
static bool parse_char(const char* text, uint32_t* cp)
{
    size_t n = strlen(text);
    const uint8_t* s = (const uint8_t*)text;
    if (n > 2 && text[0] == 'U' && text[1] == '+')
    {
        uint32_t v;
        char hex[16] = "0x";
        if (n - 2 > 8)
            return false;
        memcpy(hex + 2, text + 2, n - 2);
        return parse_number(hex, n, &v) && v > 0 && v <= 0x10FFFFu && ((*cp = v), true);
    }
    uint32_t len = (s[0] < 0x80) ? 1u : ((s[0] & 0xE0u) == 0xC0u) ? 2u : ((s[0] & 0xF0u) == 0xE0u) ? 3u :
                   ((s[0] & 0xF8u) == 0xF0u) ? 4u : 0u;
    if (len == 0 || n != len)
        return false;
    uint32_t v = (len == 1) ? s[0] : (s[0] & (0xFFu >> (len + 1u)));
    for (uint32_t i = 1; i < len; i++)
    {
        if ((s[i] & 0xC0u) != 0x80u)
            return false;
        v = (v << 6) | (s[i] & 0x3Fu);
    }
    *cp = v;
    return v > 0;
}

/* The layer a section is: base, or modifiers joined with + (alt+shift) */
static bool parse_layer(const char* name, uint8_t* layer)
{
    uint8_t mask = 0;
    size_t length = strlen(name), from = 0;
    if (strcmp(name, "base") == 0)
    {
        *layer = 0;
        return true;
    }
    for (size_t i = 0; i <= length; i++)
    {
        if (i < length && name[i] != '+')
            continue;
        size_t n = i - from;
        if (word_is(name + from, n, "shift"))
            mask |= 1u << MOD_SHIFT;
        else if (word_is(name + from, n, "ctrl"))
            mask |= 1u << MOD_CTRL;
        else if (word_is(name + from, n, "alt"))
            mask |= 1u << MOD_ALT;
        else if (word_is(name + from, n, "sym"))
            mask |= 1u << MOD_SYM;
        else
            return false;
        from = i + 1U;
    }
    *layer = mask;
    return true;
}

static int add_entry(keymap_t* km, uint16_t code, uint8_t layer, uint32_t cp)
{
    if (km->count == km->capacity)
    {
        uint32_t capacity = (km->capacity == 0) ? 32u : km->capacity * 2u;
        entry_t* grown = Dmod_Malloc(capacity * sizeof(entry_t));
        if (grown == NULL)
            return -ENOMEM;
        if (km->entries != NULL)
        {
            memcpy(grown, km->entries, km->count * sizeof(entry_t));
            Dmod_Free(km->entries);
        }
        km->entries = grown;
        km->capacity = capacity;
    }
    km->entries[km->count++] = (entry_t){ cp, code, layer };
    return 0;
}

/* A modifier name (shift, ctrl, alt, sym) -> its index */
static int modifier_named(const char* text, size_t length)
{
    if (word_is(text, length, "shift"))
        return MOD_SHIFT;
    if (word_is(text, length, "ctrl"))
        return MOD_CTRL;
    if (word_is(text, length, "alt"))
        return MOD_ALT;
    return word_is(text, length, "sym") ? MOD_SYM : -1;
}

/* Walk "a,b,c": each item is passed to `fn` (false from it: the list is bad) */
static bool for_each_item(const char* list, bool (*fn)(void* arg, const char* item, size_t length), void* arg)
{
    size_t from = 0, length = strlen(list);
    for (size_t i = 0; i <= length; i++)
    {
        if (i < length && list[i] != ',')
            continue;
        size_t a = from, b = i;
        while (a < b && list[a] == ' ')
            a++;
        while (b > a && list[b - 1U] == ' ')
            b--;
        if (b > a && !fn(arg, list + a, b - a))
            return false;
        from = i + 1U;
    }
    return true;
}

typedef struct
{
    keymap_t*   km;
    int         modifier;
    uint8_t     taken;
    uint8_t*    mask;
} list_arg_t;

static bool item_key(void* arg, const char* item, size_t length)
{
    list_arg_t* a = arg;
    uint16_t code;
    if (!parse_key(item, length, &code) || a->taken >= MOD_MAX_CODES)
        return false;
    a->km->mod_codes[a->modifier][a->taken++] = code;
    return true;
}

static bool item_modifier(void* arg, const char* item, size_t length)
{
    list_arg_t* a = arg;
    int m = modifier_named(item, length);
    if (m < 0)
        return false;
    *a->mask |= (uint8_t)(1u << m);
    return true;
}

/* [modifiers]: which keys are the modifiers, which of them are sticky (a tap arms them) and lock (a second tap) */
static int load_modifiers(keymap_t* km, dmini_context_t ini)
{
    static const char* const none = "";
    for (int m = 0; m < MOD_COUNT; m++)
    {
        const char* name = (m == MOD_SHIFT) ? "shift" : (m == MOD_CTRL) ? "ctrl" : (m == MOD_ALT) ? "alt" : "sym";
        const char* list = dmini_get_string(ini, "modifiers", name, none);
        list_arg_t a = { km, m, 0, NULL };
        if (list[0] == '\0')
            continue;
        memset(km->mod_codes[m], 0, sizeof(km->mod_codes[m]));
        if (!for_each_item(list, item_key, &a))
            return -EINVAL;
    }
    list_arg_t sticky = { km, 0, 0, &km->sticky }, lock = { km, 0, 0, &km->lockable };
    if (!for_each_item(dmini_get_string(ini, "modifiers", "sticky", none), item_modifier, &sticky) ||
        !for_each_item(dmini_get_string(ini, "modifiers", "lock", none), item_modifier, &lock))
        return -EINVAL;
    return 0;
}

/* A section of a layer: key=character */
static int load_layer(keymap_t* km, dmini_context_t ini, const char* section, uint8_t layer)
{
    for (int i = 0; i < dmini_key_count(ini, section); i++)
    {
        const char* key = dmini_key_name(ini, section, i);
        const char* value = (key != NULL) ? dmini_get_string(ini, section, key, "") : NULL;
        uint16_t code;
        uint32_t cp;
        if (key == NULL || !parse_key(key, strlen(key), &code) || !parse_char(value, &cp))
            return -EINVAL;
        int ret = add_entry(km, code, layer, cp);
        if (ret != 0)
            return ret;
    }
    return 0;
}

static int load_ini(keymap_t* km, dmini_context_t ini)
{
    int ret = load_modifiers(km, ini);
    for (int s = 0; ret == 0 && s < dmini_section_count(ini); s++)
    {
        const char* section = dmini_section_name(ini, s);
        uint8_t layer;
        if (section == NULL || strcmp(section, "modifiers") == 0 || strcmp(section, "layout") == 0)
            continue;
        ret = parse_layer(section, &layer) ? load_layer(km, ini, section, layer) : -EINVAL;
    }
    return ret;
}

/* The layers of a file on top of `km` */
int keymap_apply_file(keymap_t* km, const char* path)
{
    dmini_context_t ini = dmini_create();
    int ret = (ini != NULL) ? dmini_parse_file(ini, path) : -ENOMEM;
    if (ret != 0)
        ret = (ret == -ENOMEM) ? ret : -ENOENT;
    else
        ret = load_ini(km, ini);
    if (ini != NULL)
        dmini_destroy(ini);
    return ret;
}

bool keymap_file_extends(const char* path, char* name, size_t size)
{
    dmini_context_t ini = dmini_create();
    bool found = false;
    if (ini != NULL && dmini_parse_file(ini, path) == 0)
    {
        const char* value = dmini_get_string(ini, "layout", "extends", "");
        size_t length = strlen(value);
        if (length != 0U && length < size)
        {
            memcpy(name, value, length + 1U);
            found = true;
        }
    }
    if (ini != NULL)
        dmini_destroy(ini);
    return found;
}

keymap_t* keymap_create_from_file(const char* path, int* status)
{
    keymap_t* km = keymap_create();
    int ret = (km != NULL) ? keymap_apply_file(km, path) : -ENOMEM;
    if (ret != 0)
    {
        keymap_destroy(km);
        km = NULL;
    }
    if (status != NULL)
        *status = ret;
    return km;
}
