#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "util.h"

int main(void)
{
    char out[256];
    char method[16];
    char path[64];
    const char *req = "POST /upload HTTP/1.1\r\nHost: amiga\r\nContent-Length: 123\r\nX-AmiDrop-Filename: Game%20Disk.adf\r\n\r\nBODY";
    const char *end;
    const char *token = "ABCDEFGHJKLMNPQRSTUV";

    assert(amidrop_url_decode("Game%20Disk.adf", out, sizeof(out)));
    assert(strcmp(out, "Game Disk.adf") == 0);

    amidrop_sanitize_filename("bad:name/one?.lha", out, sizeof(out));
    assert(strcmp(out, "bad_name_one?.lha") == 0);

    amidrop_fit_amiga_name("This is a very long Amiga filename.lha", out, sizeof(out), 30);
    assert(strlen(out) <= 30);
    assert(strstr(out, ".lha") != NULL);

    assert(amidrop_parse_request_line(req, method, sizeof(method), path, sizeof(path)));
    assert(strcmp(method, "POST") == 0);
    assert(strcmp(path, "/upload") == 0);

    assert(amidrop_header_value(req, "content-length", out, sizeof(out)));
    assert(strcmp(out, "123") == 0);
    assert(amidrop_header_value(req, "X-AmiDrop-Filename", out, sizeof(out)));
    assert(strcmp(out, "Game%20Disk.adf") == 0);

    end = amidrop_find_header_end(req, strlen(req));
    assert(end != NULL);
    assert(strcmp(end, "BODY") == 0);

    assert(amidrop_access_code_matches("123456", "123456"));
    assert(!amidrop_access_code_matches("123457", "123456"));
    assert(!amidrop_access_code_matches("12345", "123456"));
    assert(!amidrop_access_code_matches("12A456", "123456"));

    assert(amidrop_session_token_matches(token, token));
    assert(!amidrop_session_token_matches("ABCDEFGHJKLMNPQRSTUW", token));
    assert(!amidrop_session_token_matches("ABCDEFGHJKLMNPQRSTU", token));
    assert(!amidrop_session_token_matches("", token));

    /* amidrop_percent: never over 100, never wrong by more than one point
       against the exact computation it replaced. */
    {
        unsigned long t, d;
        static const unsigned long totals[] = {
            1UL, 100UL, 524288UL, 52428800UL, 1073741824UL, 4294967295UL };
        int k;
        for (k = 0; k < 6; ++k) {
            t = totals[k];
            for (d = 0; d < t; d += t / 97 + 1) {
                unsigned long got = amidrop_percent(d, t);
                unsigned long want = (unsigned long)(((unsigned long long)d * 100ULL) / t);
                assert(got <= 100);
                assert((got > want ? got - want : want - got) <= 1);
            }
            assert(amidrop_percent(t, t) == 100);
            /* t + 1 would wrap to 0 in the target's 32-bit ULONG, so only
               assert the over-total case where both widths agree. */
            if (t < 4294967295UL)
                assert(amidrop_percent(t + 1, t) == 100);
        }
        assert(amidrop_percent(0, 0) == 0);
        assert(amidrop_percent(7, 0) == 0);
    }

    puts("AmiDrop util tests: OK");
    return 0;
}
