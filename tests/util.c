/* Dhara - NAND flash management layer
 * Copyright (C) 2013 Daniel Beer <dlbeer@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "dhara/bytes.h"
#include "util.h"

void seq_gen(unsigned int seed, uint8_t *buf, size_t length)
{
	size_t i;

	srandom(seed);
	for (i = 0; i < length; i++)
		buf[i] = random();
}

void seq_assert(unsigned int seed, const uint8_t *buf, size_t length)
{
	size_t i;

	srandom(seed);
	for (i = 0; i < length; i++) {
		const uint8_t expect = random();

		if (buf[i] != expect) {
			fprintf(stderr, "seq_assert: mismatch at %ld in "
				"sequence %d: 0x%02x (expected 0x%02x)\n",
				i, seed, buf[i], expect);
			abort();
		}
	}
}

void dabort(const char *message, dhara_error_t err)
{
	fprintf(stderr, "%s: dhara_error_t => %s\n",
		message, dhara_strerror(err));
	abort();
}
