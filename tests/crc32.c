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
#include <string.h>
#include <assert.h>
#include "ecc/crc32.h"

#define BLOCK_SIZE	512

static void flip_one_bit(uint8_t *b, int size)
{
	const int which = random() % (size * 8);
	const int byte = which >> 3;
	const uint8_t bit = 1 << (which & 7);

	b[byte] ^= bit;
}

static void test_hd(const uint8_t *good, uint32_t good_crc, int hd)
{
	uint8_t bad[BLOCK_SIZE];
	uint32_t bad_crc;
	int i;

	memcpy(bad, good, sizeof(bad));

	for (i = 0; i < hd; i++)
		flip_one_bit(bad, sizeof(bad));

	bad_crc = crc32_nand(bad, sizeof(bad), CRC32_INIT);
	assert(!memcmp(bad, good, BLOCK_SIZE) || (bad_crc != good_crc));
}

static void test_random_block(void)
{
	uint8_t block[BLOCK_SIZE];
	uint32_t crc;
	int i;

	for (i = 0; i < BLOCK_SIZE; i++)
		block[i] = random();

	crc = crc32_nand(block, BLOCK_SIZE, CRC32_INIT);

	for (i = 0; i < 20; i++)
		test_hd(block, crc, 4);
}

int main(void)
{
	uint8_t block[BLOCK_SIZE];
	int i;

	memset(block, 0xff, sizeof(block));
	assert(crc32_nand(block, BLOCK_SIZE, CRC32_INIT) == 0xffffffff);

	srandom(0);
	for (i = 0; i < 10; i++)
		test_random_block();

	return 0;
}
