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

/* Inserts newlines at word boundaries so a message fits a narrow requester.
   Requesters do not word-wrap in either frontend: Intuition clips,
   and MUI Text clips too, so the long diagnostics this program produces were
   unreadable.  Returns dst.  Never splits a word unless it is longer than
   the column itself. */
char *amidrop_wrap_text(const char *src, char *dst, size_t dst_size, size_t columns);

/* Percentage of done/total, clamped to 0..100.  Used in the transfer path
   and by both frontends; avoiding a 64-bit multiply/divide matters on 68000. */
unsigned long amidrop_percent(unsigned long done, unsigned long total);

/* Does an existing QR payload still match this address AND this token?  The
   payload format is "<address>?t=<token>".  Comparing only the address was a
   real bug: a server stop/start regenerates the token while the address stays
   byte-identical, and a QR carrying the old token locks the sender out. */
int amidrop_qr_payload_current(const char *payload, const char *address,
                               const char *token);

/* Composes the link a sender needs: "<address>?t=<token>".  In the MUI
   frontend the QR symbol and the clipboard are both built from this, so those
   two cannot say different things; the GadTools frontend still
   composes its own copy.
   Writes an empty string when either part is missing, and refuses to truncate.
   Returns the length written, 0 if nothing was. */
size_t amidrop_compose_url(char *dst, size_t dst_size, const char *address,
                           const char *token);

/* Builds the smallest legal IFF FTXT clip for `text`, which is what the
   clipboard carries.  clipboard.doc describes only the I/O request, not the
   format; FTXT and CHRS are the IDs in ndk-include/datatypes/textclass.h and
   the layout is EA IFF 85:

     "FORM" <size> "FTXT" "CHRS" <len> <bytes> [pad]

   Both sizes are big-endian, written byte by byte so a little-endian host can
   test this.  IFF pads a chunk to an even length, and that pad byte counts
   towards the FORM's size but NOT towards the chunk's own.  Returns the number
   of bytes written, 0 if the buffer is too small. */
size_t amidrop_build_ftxt(unsigned char *dst, size_t dst_size, const char *text);

#endif
