#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "qrcode.h"

#define TEST_QR_VERSION 3
#define TEST_QR_BUFFER_SIZE 128
#define TEST_QR_MAX_PAYLOAD 52

int main(void)
{
    QRCode qr;
    unsigned char modules[TEST_QR_BUFFER_SIZE];
    const char *max_url = "http://255.255.255.255:65535/?t=ABCDEFGHJKLMNPQRSTUV";
    const char *too_long = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    assert(strlen(max_url) == TEST_QR_MAX_PAYLOAD);
    assert(qrcode_getBufferSize(TEST_QR_VERSION) <= sizeof(modules));
    assert(qrcode_initText(&qr, modules, TEST_QR_VERSION, ECC_LOW, max_url) == 0);
    assert(qr.size == 29);

    /* QR version 3-L accepts at most 53 bytes in byte mode; 54 must be rejected safely. */
    assert(strlen(too_long) == 54);
    assert(qrcode_initText(&qr, modules, TEST_QR_VERSION, ECC_LOW, too_long) != 0);

    puts("AmiDrop QR tests: OK");
    return 0;
}
