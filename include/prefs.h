#ifndef AMIDROP_PREFS_H
#define AMIDROP_PREFS_H

#include "amidrop.h"

void prefs_defaults(struct AmiDropPrefs *prefs);
BOOL prefs_load(struct AmiDropPrefs *prefs);
BOOL prefs_save(const struct AmiDropPrefs *prefs);
BOOL prefs_valid_port(ULONG port);
BOOL prefs_valid_max_file_kb(ULONG max_file_kb);
BOOL prefs_valid_max_file_mb(ULONG max_file_mb);
BOOL prefs_parse_yes_no(const char *text, BOOL *value);

#endif
