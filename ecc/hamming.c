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

#include "hamming.h"

#define HAMMING_LOG2_CHUNK_SIZE		9
#define HAMMING_LOG2_CHUNK_BITS		(HAMMING_LOG2_CHUNK_SIZE + 3)

static const uint8_t bit_count[] = {
	0x00, 0x01, 0x01, 0x02, 0x01, 0x02, 0x02, 0x03,
	0x01, 0x02, 0x02, 0x03, 0x02, 0x03, 0x03, 0x04,
	0x01, 0x02, 0x02, 0x03, 0x02, 0x03, 0x03, 0x04,
	0x02, 0x03, 0x03, 0x04, 0x03, 0x04, 0x04, 0x05,
	0x01, 0x02, 0x02, 0x03, 0x02, 0x03, 0x03, 0x04,
	0x02, 0x03, 0x03, 0x04, 0x03, 0x04, 0x04, 0x05,
	0x02, 0x03, 0x03, 0x04, 0x03, 0x04, 0x04, 0x05,
	0x03, 0x04, 0x04, 0x05, 0x04, 0x05, 0x05, 0x06,
	0x01, 0x02, 0x02, 0x03, 0x02, 0x03, 0x03, 0x04,
	0x02, 0x03, 0x03, 0x04, 0x03, 0x04, 0x04, 0x05,
	0x02, 0x03, 0x03, 0x04, 0x03, 0x04, 0x04, 0x05,
	0x03, 0x04, 0x04, 0x05, 0x04, 0x05, 0x05, 0x06,
	0x02, 0x03, 0x03, 0x04, 0x03, 0x04, 0x04, 0x05,
	0x03, 0x04, 0x04, 0x05, 0x04, 0x05, 0x05, 0x06,
	0x03, 0x04, 0x04, 0x05, 0x04, 0x05, 0x05, 0x06,
	0x04, 0x05, 0x05, 0x06, 0x05, 0x06, 0x06, 0x07,
	0x01, 0x02, 0x02, 0x03, 0x02, 0x03, 0x03, 0x04,
	0x02, 0x03, 0x03, 0x04, 0x03, 0x04, 0x04, 0x05,
	0x02, 0x03, 0x03, 0x04, 0x03, 0x04, 0x04, 0x05,
	0x03, 0x04, 0x04, 0x05, 0x04, 0x05, 0x05, 0x06,
	0x02, 0x03, 0x03, 0x04, 0x03, 0x04, 0x04, 0x05,
	0x03, 0x04, 0x04, 0x05, 0x04, 0x05, 0x05, 0x06,
	0x03, 0x04, 0x04, 0x05, 0x04, 0x05, 0x05, 0x06,
	0x04, 0x05, 0x05, 0x06, 0x05, 0x06, 0x06, 0x07,
	0x02, 0x03, 0x03, 0x04, 0x03, 0x04, 0x04, 0x05,
	0x03, 0x04, 0x04, 0x05, 0x04, 0x05, 0x05, 0x06,
	0x03, 0x04, 0x04, 0x05, 0x04, 0x05, 0x05, 0x06,
	0x04, 0x05, 0x05, 0x06, 0x05, 0x06, 0x06, 0x07,
	0x03, 0x04, 0x04, 0x05, 0x04, 0x05, 0x05, 0x06,
	0x04, 0x05, 0x05, 0x06, 0x05, 0x06, 0x06, 0x07,
	0x04, 0x05, 0x05, 0x06, 0x05, 0x06, 0x06, 0x07,
	0x05, 0x06, 0x06, 0x07, 0x06, 0x07, 0x07, 0x08
};

static hamming_ecc_t parity_scan(const uint8_t *chunk, size_t len)
{
	uint8_t column = 0;
	uint16_t line = 0;
	uint16_t line_bar = 0;
	hamming_ecc_t out = 0;
	int i;

	/* Let the bits in chunk be partitioned into the following
	 * subsets:
	 *
	 *    P0 : bits  0,  2,  4,  6,  8, 10, 12, 14...
	 *    P0': bits  1,  3,  5,  7,  9, 11, 13, 15...
	 *    P1 : bits  0,  1,  4,  5,  8,  9, 12, 13...
	 *    P1': bits  2,  3,  6,  7, 10, 11, 14, 15...
	 *    P2 : bits  0,  1,  2,  3,  8,  9, 10, 11...
	 *    P2': bits  4,  5,  6,  7, 12, 13, 14, 15...
         *    P3 : bits  0,  1,  2,  3,  4,  5,  6,  7...
	 *    P3': bits  8,  9, 10, 11, 12, 13, 14, 15...
	 *
	 * That is, given a bit position i, and a pair of sets (Pm,
	 * Pm'), then i belongs to Pm if bit m in i is clear. Otherwise,
	 * it belongs to Pm'.
	 */
	for (i = 0; i < len; i++) {
		const uint8_t c = chunk[i];

		column ^= c;

		if (bit_count[c] & 1) {
			line ^= i;
			line_bar ^= ~i;
		}
	}

	/* The output checksum is the parity of the sets, in the
	 * following order:
	 *
	 *     ...P3', P3, P2', P2, P1', P1, P0', P0
	 *
	 * This is a linear code: the parity of the difference of two
	 * blocks is equal to the difference of their parities.
	 *
	 * If the bit at position i is flipped, then it flips the parity
	 * of exactly one of each pair of sets (position i belongs to
	 * one of each pair of sets).
	 *
	 * By observing which of each pair has changed parity, we can
	 * determine each bit of i.
	 */
	for (i = 0; i < HAMMING_LOG2_CHUNK_SIZE; i++) {
		out <<= 1;
		out |= (line_bar >> 8) & 1;

		out <<= 1;
		out |= (line >> 8) & 1;

		line <<= 1;
		line_bar <<= 1;
	}

	out = (out << 1) | (bit_count[column & 0x0f] & 1);
	out = (out << 1) | (bit_count[column & 0xf0] & 1);
	out = (out << 1) | (bit_count[column & 0x33] & 1);
	out = (out << 1) | (bit_count[column & 0xcc] & 1);
	out = (out << 1) | (bit_count[column & 0x55] & 1);
	out = (out << 1) | (bit_count[column & 0xaa] & 1);

	return out ^ 0xffffff;
}

void hamming_generate(const uint8_t *chunk, size_t len, uint8_t *ecc)
{
	hamming_ecc_t p = parity_scan(chunk, len);

	ecc[0] = p;
	p >>= 8;
	ecc[1] = p;
	p >>= 8;
	ecc[2] = p;
}

hamming_ecc_t hamming_syndrome(const uint8_t *chunk, size_t len,
			       const uint8_t *ecc)
{
	const hamming_ecc_t p = parity_scan(chunk, len);
	hamming_ecc_t q = 0;

	q |= ecc[2];
	q <<= 8;
	q |= ecc[1];
	q <<= 8;
	q |= ecc[0];

	return p ^ q;
}

int hamming_repair(uint8_t *chunk, size_t len, hamming_ecc_t syndrome)
{
	int pos = 0;
	int pos_bit = 1;
	int i;

	/* There might be no error */
	if (!syndrome)
		return 0;

	/* The single-bit error might be in the ECC data itself. */
	if (!(syndrome & (syndrome - 1)))
		return 0;

	/* Otherwise, go on the assumption that there's a single-bit
	 * error in the chunk. If this is true, then exactly one out of
	 * every complementary pair of syndrome bits should be set.
	 */
	for (i = 0; i < HAMMING_LOG2_CHUNK_BITS; i++) {
		const int s = syndrome & 3;

		if (s == 1)
			pos |= pos_bit;
		else if (s != 2)
			return -1;

		syndrome >>= 2;
		pos_bit <<= 1;
	}

	/* Flip the bit back */
	if ((pos >> 3) < len)
		chunk[pos >> 3] ^= 1 << (pos & 7);

	return 0;
}
