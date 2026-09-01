#include <ctype.h>
#include <string.h>
#include "util.h"

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

int amidrop_ascii_casecmp_n(const char *left, const char *right, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        unsigned char a = (unsigned char)left[i];
        unsigned char b = (unsigned char)right[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return (int)a - (int)b;
        if (a == 0) return 0;
    }
    return 0;
}

int amidrop_url_decode(const char *src, char *dst, size_t dst_size)
{
    size_t out = 0;
    if (!src || !dst || dst_size == 0) return 0;

    while (*src && out + 1 < dst_size) {
        if (*src == '%' && src[1] && src[2]) {
            int hi = hex_value(src[1]);
            int lo = hex_value(src[2]);
            if (hi >= 0 && lo >= 0) {
                dst[out++] = (char)((hi << 4) | lo);
                src += 3;
                continue;
            }
        }
        dst[out++] = (*src == '+') ? ' ' : *src;
        ++src;
    }
    dst[out] = '\0';
    return *src == '\0';
}

void amidrop_sanitize_filename(const char *src, char *dst, size_t dst_size)
{
    size_t out = 0;
    const unsigned char *p = (const unsigned char *)src;

    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (*p && out + 1 < dst_size) {
        unsigned char c = *p++;
        if (c < 32 || c >= 127 || c == ':' || c == '/' || c == '\\' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
        dst[out++] = (char)c;
    }
    dst[out] = '\0';

    while (out > 0 && (dst[out - 1] == ' ' || dst[out - 1] == '.')) {
        dst[--out] = '\0';
    }
    if (dst[0] == '\0' || strcmp(dst, ".") == 0 || strcmp(dst, "..") == 0) {
        strncpy(dst, "upload.bin", dst_size - 1);
        dst[dst_size - 1] = '\0';
    }
}

void amidrop_fit_amiga_name(const char *src, char *dst, size_t dst_size, size_t max_len)
{
    const char *dot;
    size_t src_len;
    size_t ext_len;
    size_t base_len;

    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src || max_len == 0) return;
    if (max_len >= dst_size) max_len = dst_size - 1;

    src_len = strlen(src);
    if (src_len <= max_len) {
        memcpy(dst, src, src_len + 1);
        return;
    }

    dot = strrchr(src, '.');
    if (!dot || dot == src) {
        memcpy(dst, src, max_len);
        dst[max_len] = '\0';
        return;
    }

    ext_len = strlen(dot);
    if (ext_len >= max_len - 1) {
        memcpy(dst, src, max_len);
        dst[max_len] = '\0';
        return;
    }

    base_len = max_len - ext_len;
    memcpy(dst, src, base_len);
    memcpy(dst + base_len, dot, ext_len);
    dst[max_len] = '\0';
}

int amidrop_header_value(const char *headers, const char *name, char *value, size_t value_size)
{
    const char *line;
    size_t name_len;

    if (!headers || !name || !value || value_size == 0) return 0;
    value[0] = '\0';
    name_len = strlen(name);
    line = strstr(headers, "\r\n");
    if (!line) return 0;
    line += 2;

    while (*line) {
        const char *end = strstr(line, "\r\n");
        const char *start;
        size_t len;
        if (!end || end == line) break;

        if ((size_t)(end - line) > name_len + 1 &&
            amidrop_ascii_casecmp_n(line, name, name_len) == 0 &&
            line[name_len] == ':') {
            start = line + name_len + 1;
            while (start < end && (*start == ' ' || *start == '\t')) ++start;
            len = (size_t)(end - start);
            if (len >= value_size) len = value_size - 1;
            memcpy(value, start, len);
            value[len] = '\0';
            return 1;
        }
        line = end + 2;
    }
    return 0;
}

int amidrop_parse_request_line(const char *headers, char *method, size_t method_size,
                               char *path, size_t path_size)
{
    const char *sp1;
    const char *sp2;
    size_t len1;
    size_t len2;

    if (!headers || !method || !path || method_size == 0 || path_size == 0) return 0;
    sp1 = strchr(headers, ' ');
    if (!sp1) return 0;
    sp2 = strchr(sp1 + 1, ' ');
    if (!sp2) return 0;

    len1 = (size_t)(sp1 - headers);
    len2 = (size_t)(sp2 - (sp1 + 1));
    if (len1 == 0 || len1 >= method_size || len2 == 0 || len2 >= path_size) return 0;

    memcpy(method, headers, len1);
    method[len1] = '\0';
    memcpy(path, sp1 + 1, len2);
    path[len2] = '\0';
    return 1;
}

const char *amidrop_find_header_end(const char *buffer, size_t length)
{
    size_t i;
    if (!buffer || length < 4) return NULL;
    for (i = 0; i + 3 < length; ++i) {
        if (buffer[i] == '\r' && buffer[i + 1] == '\n' &&
            buffer[i + 2] == '\r' && buffer[i + 3] == '\n') {
            return buffer + i + 4;
        }
    }
    return NULL;
}

int amidrop_access_code_matches(const char *candidate, const char *expected)
{
    size_t i;
    unsigned int diff = 0;

    if (!candidate || !expected) return 0;
    if (strlen(candidate) != 6 || strlen(expected) != 6) return 0;

    for (i = 0; i < 6; ++i) {
        unsigned char c = (unsigned char)candidate[i];
        unsigned char e = (unsigned char)expected[i];
        if (c < '0' || c > '9' || e < '0' || e > '9') return 0;
        diff |= (unsigned int)(c ^ e);
    }
    return diff == 0;
}


int amidrop_session_token_matches(const char *candidate, const char *expected)
{
    size_t i;
    unsigned int diff = 0;

    if (!candidate || !expected) return 0;
    if (strlen(candidate) != 20 || strlen(expected) != 20) return 0;

    for (i = 0; i < 20; ++i) {
        unsigned char c = (unsigned char)candidate[i];
        unsigned char e = (unsigned char)expected[i];
        diff |= (unsigned int)(c ^ e);
    }
    return diff == 0;
}

size_t amidrop_compose_url(char *dst, size_t dst_size, const char *address,
                           const char *token)
{
    size_t alen, tlen, need;

    if (!dst || dst_size == 0) return 0;
    dst[0] = '\0';
    if (!address || !token || !address[0] || !token[0]) return 0;

    alen = strlen(address);
    tlen = strlen(token);
    need = alen + 3 + tlen;             /* "?t=" */
    if (need + 1 > dst_size) return 0;

    memcpy(dst, address, alen);
    dst[alen]     = '?';
    dst[alen + 1] = 't';
    dst[alen + 2] = '=';
    memcpy(dst + alen + 3, token, tlen);
    dst[need] = '\0';
    return need;
}

static void put_be32(unsigned char *dst, unsigned long value)
{
    dst[0] = (unsigned char)((value >> 24) & 0xFF);
    dst[1] = (unsigned char)((value >> 16) & 0xFF);
    dst[2] = (unsigned char)((value >> 8) & 0xFF);
    dst[3] = (unsigned char)(value & 0xFF);
}

size_t amidrop_build_ftxt(unsigned char *dst, size_t dst_size, const char *text)
{
    size_t len, pad, total;

    if (!dst || !text) return 0;
    len = strlen(text);
    if (len == 0) return 0;
    pad = len & 1;
    total = 20 + len + pad;
    if (total > dst_size) return 0;

    memcpy(dst, "FORM", 4);
    put_be32(dst + 4, (unsigned long)(4 + 8 + len + pad));
    memcpy(dst + 8, "FTXT", 4);
    memcpy(dst + 12, "CHRS", 4);
    put_be32(dst + 16, (unsigned long)len);
    memcpy(dst + 20, text, len);
    if (pad) dst[20 + len] = 0;
    return total;
}

int amidrop_qr_payload_current(const char *payload, const char *address,
                               const char *token)
{
    size_t alen;

    if (!payload || !address || !token) return 0;
    if (!payload[0] || !address[0] || !token[0]) return 0;

    alen = strlen(address);
    if (strncmp(payload, address, alen) != 0) return 0;
    if (payload[alen] != '?' || payload[alen + 1] != 't' ||
        payload[alen + 2] != '=') return 0;
    return strcmp(payload + alen + 3, token) == 0;
}

unsigned long amidrop_percent(unsigned long done, unsigned long total)
{
    if (!total) return 0;
    if (done >= total) return 100;

    /* done * 100 overflows 32 bits above ~42 MB, and a 64-bit multiply and
       divide are software routines on this target.  Scaling both sides down
       until the multiply is safe keeps it in 32-bit arithmetic; the loss is
       at most one part in 100, which a percentage cannot show. */
    while (done > 42949672UL) {
        done >>= 4;
        total >>= 4;
    }
    if (!total) return 100;
    return (done * 100UL) / total;
}

/* Requesters do not word-wrap.  Intuition clips at the window edge and MUI
   Text clips too, so the 140-200 character diagnostics this program produces
   arrived unreadable in the MUI and ReAction builds.  Those two use this;
   the GadTools frontend keeps its own wrap_requester_text(), which measures
   with the actual screen font instead of counting characters, and is better
   for that reason. */
char *amidrop_wrap_text(const char *src, char *dst, size_t dst_size, size_t columns)
{
    size_t out = 0;
    size_t line = 0;

    if (!dst || dst_size == 0) return dst;
    dst[0] = '\0';
    if (!src) return dst;
    if (columns < 8) columns = 8;

    while (*src && out + 1 < dst_size) {
        size_t word;
        size_t i;

        if (*src == '\n') {          /* honour breaks the caller put in */
            /* A blank run just before it would otherwise be left at the end
               of the line, the same way the wrap point already guards. */
            while (out > 0 && dst[out - 1] == ' ') --out;
            dst[out++] = '\n';
            line = 0;
            ++src;
            continue;
        }
        if (*src == ' ' || *src == '\t') {
            /* A whole run collapses to one blank.  Emitting each of them and
               only stripping one at the wrap point let a line run past the
               column count and end in a space (found by fuzzing). */
            while (*src == ' ' || *src == '\t') ++src;
            /* Never at a line start, so a wrapped line has no leading blank. */
            if (line > 0 && out + 1 < dst_size) {
                dst[out++] = ' ';
                ++line;
            }
            continue;
        }

        for (word = 0; src[word] && src[word] != ' ' &&
                       src[word] != '\t' && src[word] != '\n'; ++word)
            ;

        /* Break before a word that would overflow - unless it is alone on
           the line, in which case it has to be let through and clipped. */
        if (line + word > columns && line > 0) {
            while (out > 0 && dst[out - 1] == ' ') --out;
            if (out + 1 >= dst_size) break;
            dst[out++] = '\n';
            line = 0;
        }

        for (i = 0; i < word && out + 1 < dst_size; ++i) {
            dst[out++] = src[i];
            ++line;
        }
        src += word;
    }

    /* A trailing space would show as a ragged line end. */
    while (out > 0 && dst[out - 1] == ' ') --out;
    dst[out] = '\0';
    return dst;
}
