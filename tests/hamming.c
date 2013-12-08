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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "ecc/hamming.h"

#define HAMMING_CHUNK_SIZE	512
#define TEST_CHUNK_SIZE		(HAMMING_CHUNK_SIZE + HAMMING_ECC_SIZE)

static void flip_one_bit(uint8_t *b, int size)
{
	const int which = random() % (size * 8);
	const int byte = which >> 3;
	const uint8_t bit = 1 << (which & 7);

	b[byte] ^= bit;
}

static void flip_test(const uint8_t *good)
{
	uint8_t bad[TEST_CHUNK_SIZE];
	int i;
	hamming_ecc_t e;

	memcpy(bad, good, sizeof(bad));
	flip_one_bit(bad, TEST_CHUNK_SIZE);

	e = hamming_syndrome(bad, HAMMING_CHUNK_SIZE,
			     bad + HAMMING_CHUNK_SIZE);
	assert(e);
	hamming_repair(bad, HAMMING_CHUNK_SIZE, e);

	i = memcmp(good, bad, HAMMING_CHUNK_SIZE);
	assert(!i);
}

static void test_properties(const uint8_t *block)
{
	hamming_ecc_t e;
	int i;

	e = hamming_syndrome(block, HAMMING_CHUNK_SIZE,
			     block + HAMMING_CHUNK_SIZE);
	assert(!e);

	for (i = 0; i < 20; i++)
		flip_test(block);
}

static void test_random_block(void)
{
	uint8_t block[TEST_CHUNK_SIZE];
	int i;

	for (i = 0; i < HAMMING_CHUNK_SIZE; i++)
		block[i] = random();

	hamming_generate(block, HAMMING_CHUNK_SIZE,
			 block + HAMMING_CHUNK_SIZE);
	test_properties(block);
}

static void test_code(void)
{
	uint8_t block[TEST_CHUNK_SIZE];
	int i;

	memset(block, 0xff, sizeof(block));
	test_properties(block);

	for (i = 0; i < 10; i++)
		test_random_block();
}

int main(void)
{
	srandom(0);
	test_code();

	return 0;
}
