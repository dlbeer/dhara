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

#include <stdint.h>
#include <stdio.h>

static uint32_t table[256];

static void fill_table(uint32_t poly)
{
	int i;

	for (i = 0; i < 256; i++) {
		uint32_t r = i;
		int j;

		for (j = 0; j < 8; j++)
			r = (r >> 1) ^ ((r & 1) ? poly : 0);

		table[i] = r;
	}
}

static void print_table(void)
{
	int i;

	for (i = 0; i < 256; i++)
		printf("0x%08x,%c", table[i], ((i & 3) == 3) ? '\n' : ' ');
}

static int parse_poly(const char *text, uint32_t *out)
{
	uint32_t r = 0;

	while ((*text == '0') || (*text == 'x'))
		text++;

	while (*text) {
		char c = *(text++);

		r <<= 4;
		if ((c >= '0') && (c <= '9')) {
			r |= (c - '0');
		} else if ((c >= 'A') && (c <= 'F')) {
			r |= (c - 'A' + 10);
		} else if ((c >= 'a') && (c <= 'f')) {
			r |= (c - 'a' + 10);
		} else {
			fprintf(stderr, "parse_poly: invalid character: %s\n",
				text);
			return -1;
		}
	}

	*out = r;
	return 0;
}

int main(int argc, char **argv)
{
	uint32_t p;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <polynomial>\n", argv[0]);
		return -1;
	}

	if (parse_poly(argv[1], &p) < 0)
		return -1;

	fill_table(p);
	print_table();
	return 0;
}
