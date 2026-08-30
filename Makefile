CC := m68k-amigaos-gcc

TARGET := AmiDrop

CFLAGS := -m68000 -Os -Wall -Wextra -Wshadow -Wpointer-arith -Wframe-larger-than=1024 -DNO_INLINE_STDARG -Iinclude
LDFLAGS := -noixemul

SOURCES := src/main.c src/gui.c src/server.c src/prefs.c src/util.c src/webpage.c src/qrcode.c
OBJECTS := $(SOURCES:.c=.o)

HOST_TEST_UTIL := tests/test_util
HOST_TEST_QR := tests/test_qr

.PHONY: all clean host-test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $< $(LDFLAGS)

host-test: $(HOST_TEST_UTIL) $(HOST_TEST_QR)
	./$(HOST_TEST_UTIL)
	./$(HOST_TEST_QR)

$(HOST_TEST_UTIL): tests/test_util.c src/util.c include/util.h include/amidrop.h
	gcc -std=c99 -O2 -Wall -Wextra -Iinclude -o $@ tests/test_util.c src/util.c

$(HOST_TEST_QR): tests/test_qr.c src/qrcode.c include/qrcode.h include/amidrop.h
	gcc -std=c99 -O2 -Wall -Wextra -Iinclude -o $@ tests/test_qr.c src/qrcode.c

clean:
	rm -f src/*.o $(TARGET) $(HOST_TEST_UTIL) $(HOST_TEST_QR)
