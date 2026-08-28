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

    puts("AmiDrop util tests: OK");
    return 0;
}
