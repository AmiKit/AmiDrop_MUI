#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prefs.h"

static void trim_line(char *text)
{
    size_t len;
    if (!text) return;
    len = strlen(text);
    while (len > 0 && (text[len - 1] == '\r' || text[len - 1] == '\n' ||
                       text[len - 1] == ' ' || text[len - 1] == '\t')) {
        text[--len] = '\0';
    }
}

static int text_equal_ci(const char *left, const char *right)
{
    if (!left || !right) return 0;
    while (*left && *right) {
        unsigned char a = (unsigned char)*left++;
        unsigned char b = (unsigned char)*right++;
        a = (unsigned char)toupper(a);
        b = (unsigned char)toupper(b);
        if (a != b) return 0;
    }
    return *left == '\0' && *right == '\0';
}

BOOL prefs_valid_port(ULONG port)
{
    return port >= 1024UL && port <= 65535UL;
}

BOOL prefs_valid_max_file_kb(ULONG max_file_kb)
{
    return max_file_kb >= AMIDROP_MIN_LIMIT_KB &&
           max_file_kb <= AMIDROP_SAFE_MAX_LIMIT_KB;
}

BOOL prefs_valid_max_file_mb(ULONG max_file_mb)
{
    return max_file_mb >= 1UL && max_file_mb <= 1024UL;
}

BOOL prefs_parse_yes_no(const char *text, BOOL *value)
{
    if (!text || !value) return FALSE;
    if (text_equal_ci(text, "YES") || text_equal_ci(text, "TRUE") ||
        text_equal_ci(text, "ON") || strcmp(text, "1") == 0) {
        *value = TRUE;
        return TRUE;
    }
    if (text_equal_ci(text, "NO") || text_equal_ci(text, "FALSE") ||
        text_equal_ci(text, "OFF") || strcmp(text, "0") == 0) {
        *value = FALSE;
        return TRUE;
    }
    return FALSE;
}

void prefs_defaults(struct AmiDropPrefs *prefs)
{
    if (!prefs) return;
    strncpy(prefs->receive_dir, AMIDROP_DEFAULT_DIR, sizeof(prefs->receive_dir) - 1);
    prefs->receive_dir[sizeof(prefs->receive_dir) - 1] = '\0';
    prefs->port = AMIDROP_DEFAULT_PORT;
    prefs->max_file_kb = AMIDROP_DEFAULT_LIMIT_KB;
    prefs->start_server = AMIDROP_DEFAULT_START_SERVER;
    prefs->ignore_free_space = FALSE;
}

static BOOL load_from_file(struct AmiDropPrefs *prefs, const char *path)
{
    FILE *file;
    char line[384];

    file = fopen(path, "r");
    if (!file) return FALSE;

    while (fgets(line, sizeof(line), file)) {
        char *eq;
        char *key;
        char *value;
        unsigned long number;
        BOOL boolean_value;

        trim_line(line);
        key = line;
        while (*key == ' ' || *key == '\t') ++key;
        if (*key == '\0' || *key == '#' || *key == ';') continue;
        eq = strchr(key, '=');
        if (!eq) continue;
        *eq = '\0';
        value = eq + 1;
        while (*value == ' ' || *value == '\t') ++value;
        trim_line(key);
        trim_line(value);

        if (strcmp(key, "ReceiveDir") == 0) {
            if (*value) {
                strncpy(prefs->receive_dir, value, sizeof(prefs->receive_dir) - 1);
                prefs->receive_dir[sizeof(prefs->receive_dir) - 1] = '\0';
            }
        } else if (strcmp(key, "Port") == 0) {
            number = strtoul(value, NULL, 10);
            if (prefs_valid_port(number)) prefs->port = (UWORD)number;
        } else if (strcmp(key, "MaxFileKB") == 0) {
            number = strtoul(value, NULL, 10);
            if (prefs_valid_max_file_kb(number)) prefs->max_file_kb = number;
        } else if (strcmp(key, "MaxFileMB") == 0) {
            /* Backward compatibility with AmiDrop RC4 and earlier. */
            number = strtoul(value, NULL, 10);
            if (prefs_valid_max_file_mb(number)) prefs->max_file_kb = number * 1024UL;
        } else if (strcmp(key, "StartServer") == 0) {
            if (prefs_parse_yes_no(value, &boolean_value)) prefs->start_server = boolean_value;
        } else if (strcmp(key, "IgnoreFreeSpace") == 0) {
            if (prefs_parse_yes_no(value, &boolean_value)) prefs->ignore_free_space = boolean_value;
        }
    }

    fclose(file);
    return TRUE;
}

BOOL prefs_load(struct AmiDropPrefs *prefs)
{
    if (!prefs) return FALSE;
    prefs_defaults(prefs);
    if (load_from_file(prefs, AMIDROP_PREFS_FILE)) return TRUE;
    return load_from_file(prefs, AMIDROP_FALLBACK_PREFS);
}

BOOL prefs_save(const struct AmiDropPrefs *prefs)
{
    FILE *file;
    if (!prefs) return FALSE;

    file = fopen(AMIDROP_PREFS_FILE, "w");
    if (!file) file = fopen(AMIDROP_FALLBACK_PREFS, "w");
    if (!file) return FALSE;

    fprintf(file, "# AmiDrop %s preferences\n", AMIDROP_VERSION);
    fprintf(file, "ReceiveDir=%s\n", prefs->receive_dir);
    fprintf(file, "Port=%u\n", (unsigned)prefs->port);
    fprintf(file, "MaxFileKB=%lu\n", (unsigned long)prefs->max_file_kb);
    fprintf(file, "StartServer=%s\n", prefs->start_server ? "YES" : "NO");
    fprintf(file, "IgnoreFreeSpace=%s\n", prefs->ignore_free_space ? "YES" : "NO");
    fclose(file);
    return TRUE;
}
