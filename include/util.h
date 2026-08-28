#ifndef AMIDROP_UTIL_H
#define AMIDROP_UTIL_H

#include <stddef.h>

int amidrop_ascii_casecmp_n(const char *left, const char *right, size_t count);
int amidrop_url_decode(const char *src, char *dst, size_t dst_size);
void amidrop_sanitize_filename(const char *src, char *dst, size_t dst_size);
void amidrop_fit_amiga_name(const char *src, char *dst, size_t dst_size, size_t max_len);
int amidrop_header_value(const char *headers, const char *name, char *value, size_t value_size);
int amidrop_parse_request_line(const char *headers, char *method, size_t method_size,
                               char *path, size_t path_size);
const char *amidrop_find_header_end(const char *buffer, size_t length);
int amidrop_access_code_matches(const char *candidate, const char *expected);
int amidrop_session_token_matches(const char *candidate, const char *expected);

#endif
