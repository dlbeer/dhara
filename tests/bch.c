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
#include "ecc/bch.h"

#define BCH_CHUNK_SIZE		512
#define TEST_CHUNK_SIZE		(BCH_CHUNK_SIZE + 8)

static void flip_one_bit(uint8_t *b, int size)
{
	const int which = random() % (size * 8);
	const int byte = which >> 3;
	const uint8_t bit = 1 << (which & 7);

	b[byte] ^= bit;
}

static void flip_test(const struct bch_def *def,
		      const uint8_t *good)
{
	uint8_t bad[TEST_CHUNK_SIZE];
	int i;

	memcpy(bad, good, sizeof(bad));

	for (i = 0; i < def->syns; i += 2)
		flip_one_bit(bad, TEST_CHUNK_SIZE);

	if (bch_verify(def, bad, BCH_CHUNK_SIZE, bad + BCH_CHUNK_SIZE) < 0) {
		bch_repair(def, bad, BCH_CHUNK_SIZE, bad + BCH_CHUNK_SIZE);
		i = bch_verify(def, bad, BCH_CHUNK_SIZE, bad + BCH_CHUNK_SIZE);
		assert(!i);
	}

	i = memcmp(good, bad, BCH_CHUNK_SIZE);
	assert(!i);
}

static void test_properties(const struct bch_def *def,
			    const uint8_t *block)
{
	int i;

	i = bch_verify(def, block, BCH_CHUNK_SIZE, block + BCH_CHUNK_SIZE);
	assert(!i);

	for (i = 0; i < 20; i++)
		flip_test(def, block);
}

static void test_random_block(const struct bch_def *def)
{
	uint8_t block[TEST_CHUNK_SIZE];
	int i;

	for (i = 0; i < BCH_CHUNK_SIZE; i++)
		block[i] = random();

	bch_generate(def, block, BCH_CHUNK_SIZE, block + BCH_CHUNK_SIZE);
	test_properties(def, block);
}

static void test_code(const struct bch_def *def)
{
	uint8_t block[TEST_CHUNK_SIZE];
	int i;

	printf("nbits: %d\n", def->syns / 2);

	memset(block, 0xff, sizeof(block));
	test_properties(def, block);

	for (i = 0; i < 10; i++)
		test_random_block(def);
}

int main(void)
{
	srandom(0);
	test_code(&bch_1bit);
	test_code(&bch_2bit);
	test_code(&bch_3bit);
	test_code(&bch_4bit);

	return 0;
}
