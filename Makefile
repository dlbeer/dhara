# Dhara - NAND flash management layer
# Copyright (C) 2013 Daniel Beer <dlbeer@gmail.com>
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

CC = $(CROSS_COMPILE)gcc
DHARA_CFLAGS = $(CFLAGS) -O1 -Wall -ggdb -I.
TESTS = \
    tests/error.test \
    tests/nand.test \
    tests/journal.test \
    tests/recovery.test \
    tests/jfill.test \
    tests/map.test \
    tests/bch.test \
    tests/hamming.test \
    tests/epoch_roll.test \
    tests/crc32.test
TOOLS = \
    tools/gftool \
    tools/gentab

all: $(TESTS) $(TOOLS)

test: $(TESTS)
	@@for x in $(TESTS); do echo $$x; ./$$x > /dev/null || exit 255; done

%.o: %.c
	$(CC) $(DHARA_CFLAGS) -o $*.o -c $*.c

tests/error.test: dhara/error.o tests/error.o
	$(CC) -o $@ $^

tests/nand.test: dhara/error.o tests/nand.o tests/sim.o tests/util.o
	$(CC) -o $@ $^

tests/journal.test: dhara/journal.o tests/journal.o tests/sim.o tests/util.o \
		    dhara/error.o tests/jtutil.o
	$(CC) -o $@ $^

tests/recovery.test: dhara/journal.o tests/recovery.o tests/sim.o tests/util.o \
		     dhara/error.o tests/jtutil.o
	$(CC) -o $@ $^

tests/jfill.test: dhara/journal.o tests/jfill.o tests/sim.o dhara/error.o \
		  tests/util.o tests/jtutil.o
	$(CC) -o $@ $^

tests/map.test: dhara/map.o dhara/journal.o dhara/error.o tests/map.o \
		tests/sim.o tests/util.o
	$(CC) -o $@ $^

tests/epoch_roll.test: dhara/map.o dhara/journal.o dhara/error.o \
		       tests/epoch_roll.o tests/sim.o tests/util.o
	$(CC) -o $@ $^

tests/bch.test: ecc/bch.o ecc/gf13.o tests/bch.o
	$(CC) -o $@ $^

tests/hamming.test: ecc/hamming.o tests/hamming.o
	$(CC) -o $@ $^

tests/crc32.test: ecc/crc32.o tests/crc32.o
	$(CC) -o $@ $^

tools/gftool: tools/gftool.o
	$(CC) -o $@ $^

tools/gentab: tools/gentab.o
	$(CC) -o $@ $^

clean:
	rm -f */*.o
	rm -f tests/*.test
	rm -f $(TOOLS)
