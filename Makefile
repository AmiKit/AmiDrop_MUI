CC := m68k-amigaos-gcc

CLASSIC_TARGET := AmiDrop
REACTION_TARGET := AmiDrop_ReAction

CFLAGS := -m68000 -Os -Wall -Wextra -Wshadow -Wpointer-arith -Wframe-larger-than=1024 -DNO_INLINE_STDARG -Iinclude
LDFLAGS := -noixemul

COMMON_SOURCES := src/server.c src/prefs.c src/util.c src/webpage.c src/qrcode.c
COMMON_OBJECTS := $(COMMON_SOURCES:.c=.o)

CLASSIC_SOURCES := src/main.c src/gui.c
CLASSIC_OBJECTS := $(CLASSIC_SOURCES:.c=.o) $(COMMON_OBJECTS)

REACTION_SOURCES := src/main_reaction.c src/gui_reaction.c
REACTION_OBJECTS := $(REACTION_SOURCES:.c=.o) $(COMMON_OBJECTS)

HOST_TEST_UTIL := tests/test_util
HOST_TEST_QR := tests/test_qr

.PHONY: all classic reaction both clean host-test

# Preserve the existing workflow: plain "make" still builds the stable Classic frontend.
all: classic

classic: $(CLASSIC_TARGET)

reaction: $(REACTION_TARGET)

both: $(CLASSIC_TARGET) $(REACTION_TARGET)

$(CLASSIC_TARGET): $(CLASSIC_OBJECTS)
	$(CC) -o $@ $(CLASSIC_OBJECTS) $(LDFLAGS)

$(REACTION_TARGET): $(REACTION_OBJECTS)
	$(CC) -o $@ $(REACTION_OBJECTS) $(LDFLAGS)

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
	rm -f src/*.o $(CLASSIC_TARGET) $(REACTION_TARGET) $(HOST_TEST_UTIL) $(HOST_TEST_QR)
