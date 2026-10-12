CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?= -Iinclude

.PHONY: all test clean
all: hammingcube

hammingcube: src/main.c src/parity3d.c include/parity3d.h
	$(CC) $(CPPFLAGS) $(CFLAGS) src/main.c src/parity3d.c -o $@

test_parity3d: tests/test_parity3d.c src/parity3d.c include/parity3d.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_parity3d.c src/parity3d.c -o $@

test: test_parity3d
	./test_parity3d

clean:
	rm -f hammingcube test_parity3d
