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

    /* amidrop_wrap_text: the behaviour the requesters depend on. */
    {
        char wrapped[512];
        const char *longmsg =
            "AmiDrop could not create a network socket. Check that your TCP/IP "
            "stack is online and bsdsocket.library is available.";
        const char *p;
        size_t longest;

        /* no line may exceed the column count */
        amidrop_wrap_text(longmsg, wrapped, sizeof(wrapped), 40);
        longest = 0;
        for (p = wrapped; *p; ) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            if (len > longest) longest = len;
            if (!nl) break;
            p = nl + 1;
        }
        assert(longest <= 40);
        assert(strchr(wrapped, '\n') != NULL);          /* it did wrap */

        /* every word survives, in order, and no word is split */
        {
            char rebuilt[512];
            size_t i, j = 0;
            for (i = 0; wrapped[i]; ++i)
                rebuilt[j++] = (wrapped[i] == '\n') ? ' ' : wrapped[i];
            rebuilt[j] = '\0';
            assert(strcmp(rebuilt, longmsg) == 0);
        }

        /* a short message is returned unchanged and unwrapped */
        amidrop_wrap_text("Short one.", wrapped, sizeof(wrapped), 40);
        assert(strcmp(wrapped, "Short one.") == 0);

        /* newlines the caller supplied are preserved */
        amidrop_wrap_text("one\ntwo", wrapped, sizeof(wrapped), 40);
        assert(strcmp(wrapped, "one\ntwo") == 0);

        /* a word longer than the column is let through, not lost */
        amidrop_wrap_text("aaaaaaaaaaaaaaaaaaaaaaaa b", wrapped, sizeof(wrapped), 10);
        assert(strstr(wrapped, "aaaaaaaaaaaaaaaaaaaaaaaa") != NULL);
        assert(strchr(wrapped, 'b') != NULL);

        /* no line starts with a blank and none ends with one */
        amidrop_wrap_text(longmsg, wrapped, sizeof(wrapped), 24);
        assert(wrapped[0] != ' ');
        for (p = strchr(wrapped, '\n'); p; p = strchr(p + 1, '\n')) {
            assert(p[1] != ' ');
            assert(p == wrapped || p[-1] != ' ');
        }
        assert(wrapped[strlen(wrapped) - 1] != ' ');

        /* runs of blanks must not push a line past the column count, and
           must not be left at a line end */
        amidrop_wrap_text("aaa   bbb   ccc   ddd   eee", wrapped, sizeof(wrapped), 10);
        longest = 0;
        for (p = wrapped; *p; ) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            if (len > longest) longest = len;
            assert(len == 0 || p[len - 1] != ' ');
            if (!nl) break;
            p = nl + 1;
        }
        assert(longest <= 10);
        assert(strstr(wrapped, "  ") == NULL);

        /* tabs count as blanks too */
        amidrop_wrap_text("aaa\t\tbbb", wrapped, sizeof(wrapped), 40);
        assert(strcmp(wrapped, "aaa bbb") == 0);

        /* degenerate inputs must not write out of bounds */
        amidrop_wrap_text(NULL, wrapped, sizeof(wrapped), 40);
        assert(wrapped[0] == '\0');
        amidrop_wrap_text(longmsg, wrapped, 8, 40);
        assert(strlen(wrapped) < 8);
    }

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

    /* amidrop_qr_payload_current: the stop/start property.  A restart
       regenerates the token while the address stays identical; the check
       must force a rebuild then, and only then. */
    {
        const char *addr = "http://192.168.0.81:8080/";
        const char *tok1 = "ABCDEFGHJKLMNPQRSTUV";
        const char *tok2 = "ZYXWVUTSRQPNMLKJHGFE";
        char pay[96];

        snprintf(pay, sizeof(pay), "%s?t=%s", addr, tok1);
        assert(amidrop_qr_payload_current(pay, addr, tok1));      /* unchanged */
        assert(!amidrop_qr_payload_current(pay, addr, tok2));     /* new token */
        assert(!amidrop_qr_payload_current(pay, "http://192.168.0.82:8080/", tok1));
        /* the cached address must not be a mere prefix of the new one */
        assert(!amidrop_qr_payload_current(pay, "http://192.168.0.8", tok1));
        assert(!amidrop_qr_payload_current("", addr, tok1));
        assert(!amidrop_qr_payload_current(pay, "", tok1));
        assert(!amidrop_qr_payload_current(pay, addr, ""));
        assert(!amidrop_qr_payload_current(NULL, addr, tok1));
    }

    /* amidrop_compose_url: the same string the QR symbol carries, and it
       must refuse rather than truncate - a half link is worse than none. */
    {
        char url[64];
        assert(amidrop_compose_url(url, sizeof(url), "http://10.0.0.5:8080/",
                                   "ABCDEFGHJKLMNPQRSTUV") == 44);
        assert(strcmp(url, "http://10.0.0.5:8080/?t=ABCDEFGHJKLMNPQRSTUV") == 0);
        /* what it produces is exactly what the QR check accepts */
        assert(amidrop_qr_payload_current(url, "http://10.0.0.5:8080/",
                                          "ABCDEFGHJKLMNPQRSTUV"));
        assert(amidrop_compose_url(url, sizeof(url), "", "TOKEN") == 0);
        assert(url[0] == '\0');
        assert(amidrop_compose_url(url, sizeof(url), "http://a/", "") == 0);
        assert(url[0] == '\0');
        {   /* one byte too small for the terminator */
            char tight[25];
            assert(amidrop_compose_url(tight, sizeof(tight), "http://10.0.0.5/",
                                       "ABCDEF") == 0);
            assert(tight[0] == '\0');
            assert(amidrop_compose_url(tight, sizeof(tight), "http://10.0.0.5/",
                                       "ABCDE") == 24);
        }
        /* The guards themselves, which nothing else exercises.  The
           zero-size case needs a sentinel: without one, dropping the guard
           writes a terminator into a buffer of no bytes and no assertion
           above can see it. */
        assert(amidrop_compose_url(NULL, 64, "http://a/", "T") == 0);
        url[0] = 0x5A;
        assert(amidrop_compose_url(url, 0, "http://a/", "T") == 0);
        assert(url[0] == 0x5A);
        assert(amidrop_compose_url(url, sizeof(url), NULL, "T") == 0);
        assert(amidrop_compose_url(url, sizeof(url), "http://a/", NULL) == 0);
        assert(url[0] == '\0');
    }

    /* amidrop_build_ftxt: the byte layout clipboard.device is handed.  The
       even/odd case is the one that matters - IFF pads the chunk, the pad
       counts in the FORM size but not in the chunk's own. */
    {
        unsigned char clip[64];
        size_t n;

        n = amidrop_build_ftxt(clip, sizeof(clip), "abcd");   /* even */
        assert(n == 24);
        assert(memcmp(clip, "FORM", 4) == 0);
        assert(clip[4] == 0 && clip[5] == 0 && clip[6] == 0 && clip[7] == 16);
        assert(memcmp(clip + 8, "FTXT", 4) == 0);
        assert(memcmp(clip + 12, "CHRS", 4) == 0);
        assert(clip[16] == 0 && clip[17] == 0 && clip[18] == 0 && clip[19] == 4);
        assert(memcmp(clip + 20, "abcd", 4) == 0);

        n = amidrop_build_ftxt(clip, sizeof(clip), "abc");    /* odd -> pad */
        assert(n == 24);
        assert(clip[7] == 16);          /* FORM size counts the pad */
        assert(clip[19] == 3);          /* the chunk's own size does not */
        assert(clip[23] == 0);          /* and the pad byte is zero */

        assert(amidrop_build_ftxt(clip, sizeof(clip), "") == 0);
        assert(amidrop_build_ftxt(clip, 20, "abcd") == 0);    /* no room */
        assert(amidrop_build_ftxt(clip, 24, "abcd") == 24);   /* exactly */

        /* The capacity boundary on an ODD length, where the pad byte decides
           it.  Without this an implementation that checks 20 + len instead of
           20 + len + pad passes everything above and then writes one byte off
           the end of the caller's buffer. */
        assert(amidrop_build_ftxt(clip, 23, "abc") == 0);
        assert(amidrop_build_ftxt(clip, 24, "abc") == 24);

        /* And nothing may be written past what was returned - an unconditional
           pad would land here on an even-length text. */
        clip[24] = 0xA5;
        assert(amidrop_build_ftxt(clip, sizeof(clip), "abcd") == 24);
        assert(clip[24] == 0xA5);

        assert(amidrop_build_ftxt(NULL, 64, "abcd") == 0);
        assert(amidrop_build_ftxt(clip, sizeof(clip), NULL) == 0);
    }

    puts("AmiDrop util tests: OK");
    return 0;
}
