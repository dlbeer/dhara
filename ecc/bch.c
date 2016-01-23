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

#include <string.h>
#include "bch.h"
#include "gf13.h"

#define BCH_MAX_SYNS		8

const struct bch_def bch_1bit = {
	.syns		= 2,
	.generator	= 0x201b,
	.degree		= 13,
	.ecc_bytes	= 2
};

const struct bch_def bch_2bit = {
	.syns		= 4,
	.generator	= 0x4d5154b,
	.degree		= 26,
	.ecc_bytes	= 4
};

const struct bch_def bch_3bit = {
	.syns		= 6,
	.generator	= 0xbaf5b2bded,
	.degree		= 39,
	.ecc_bytes	= 5
};

const struct bch_def bch_4bit = {
	.syns		= 8,
	.generator	= 0x14523043ab86ab,
	.degree		= 52,
	.ecc_bytes	= 7
};

static bch_poly_t chunk_remainder(const struct bch_def *def,
				  const uint8_t *chunk, size_t len)
{
	bch_poly_t remainder = 0;
	int i;

	for (i = 0; i < len; i++) {
		int j;

		remainder ^= chunk[i] ^ 0xff;

		for (j = 0; j < 8; j++) {
			if (remainder & 1)
				remainder ^= def->generator;
			remainder >>= 1;
		}
	}

	return remainder;
}

static void pack_poly(const struct bch_def *def, bch_poly_t poly, uint8_t *ecc)
{
	int i;

	for (i = 0; i < def->ecc_bytes; i++) {
		ecc[i] = ~poly;
		poly >>= 8;
	}
}

static bch_poly_t unpack_poly(const struct bch_def *def, const uint8_t *ecc)
{
	bch_poly_t poly = 0;
	int i;

	for (i = def->ecc_bytes - 1; i >= 0; i--) {
		poly <<= 8;
		poly |= ecc[i] ^ 0xff;
	}

	poly &= ((1LL << def->degree) - 1);
	return poly;
}

void bch_generate(const struct bch_def *bch,
		  const uint8_t *chunk, size_t len, uint8_t *ecc)
{
	pack_poly(bch, chunk_remainder(bch, chunk, len), ecc);
}

int bch_verify(const struct bch_def *bch,
	       const uint8_t *chunk, size_t len, const uint8_t *ecc)
{
	return (chunk_remainder(bch, chunk, len) == unpack_poly(bch, ecc)) ?
		0 : -1;
}

/************************************************************************
 * Polynomials over GF(2^13)
 */

#define MAX_POLY	(BCH_MAX_SYNS * 2)

static void poly_add(gf13_elem_t *dst, const gf13_elem_t *src,
		     gf13_elem_t c, int shift)
{
	int i;

	for (i = 0; i < MAX_POLY; i++) {
		int p = i + shift;
		gf13_elem_t v = src[i];

		if (p < 0 || p >= MAX_POLY)
			continue;
		if (!v)
			continue;

		dst[p] ^= gf13_mul(v, c);
	}
}

static gf13_elem_t poly_eval(const gf13_elem_t *s, gf13_elem_t x)
{
	int i;
	gf13_elem_t sum = 0;
	gf13_elem_t t = x;

	for (i = 0; i < MAX_POLY; i++) {
		const gf13_elem_t c = s[i];

		if (c)
			sum ^= gf13_mul(c, t);

		t = gf13_mul(t, x);
	}

	return sum;
}

/************************************************************************
 * Error correction
 */

static gf13_elem_t syndrome(const struct bch_def *bch,
			    const uint8_t *chunk,
			    size_t len,
			    bch_poly_t remainder,
			    gf13_elem_t x)
{
	gf13_elem_t y = 0;
	gf13_elem_t t = 1;
	int i;

	for (i = 0; i < len; i++) {
		uint8_t c = chunk[i] ^ 0xff;
		int j;

		for (j = 0; j < 8; j++) {
			if (c & 1)
				y ^= t;

			c >>= 1;
			t = gf13_mul(t, x);
		}
	}

	for (i = 0; i < bch->degree; i++) {
		if (remainder & 1)
			y ^= t;

		remainder >>= 1;
		t = gf13_mul(t, x);
	}

	return y;
}

static void berlekamp_massey(const gf13_elem_t *s, int N,
			     gf13_elem_t *sigma)
{
	gf13_elem_t C[MAX_POLY];
	gf13_elem_t B[MAX_POLY];
	int L = 0;
	int m = 1;
	gf13_elem_t b = 1;
	int n;

	memset(sigma, 0, MAX_POLY * sizeof(sigma[0]));
	memset(B, 0, sizeof(B));
	memset(C, 0, sizeof(C));
	B[0] = 1;
	C[0] = 1;

	for (n = 0; n < N; n++) {
		gf13_elem_t d = s[n];
		gf13_elem_t mult;
		int i;

		for (i = 1; i <= L; i++) {
			if (!(C[i] && s[n - i]))
				continue;

			d ^= gf13_mul(C[i], s[n - i]);
		}

		mult = gf13_div(d, b);

		if (!d) {
			m++;
		} else if (L * 2 <= n) {
			gf13_elem_t T[MAX_POLY];

			memcpy(T, C, sizeof(T));
			poly_add(C, B, mult, m);
			memcpy(B, T, sizeof(B));
			L = n + 1 - L;
			b = d;
			m = 1;
		} else {
			poly_add(C, B, mult, m);
			m++;
		}
	}

	memcpy(sigma, C, MAX_POLY);
}

void bch_repair(const struct bch_def *bch,
		uint8_t *chunk, size_t len, uint8_t *ecc)
{
	const bch_poly_t remainder = unpack_poly(bch, ecc);
	const int chunk_bits = len << 3;
	gf13_elem_t syns[BCH_MAX_SYNS];
	gf13_elem_t sigma[MAX_POLY];
	gf13_elem_t x;
	int i;

	/* Compute syndrome vector */
	x = 2;
	for (i = 0; i < bch->syns; i++) {
		syns[i] = syndrome(bch, chunk, len, remainder, x);
		x = gf13_mulx(x);
	}

	/* Compute sigma */
	berlekamp_massey(syns, bch->syns, sigma);

	/* Each root of sigma corresponds to an error location. Correct
	 * errors in the chunk data first.
	 */
	x = 1;
	for (i = 0; i < chunk_bits; i++) {
		if (!poly_eval(sigma, x))
			chunk[i >> 3] ^= 1 << (i & 7);
		x = gf13_divx(x);
	}

	/* Then correct errors in the ECC data */
	for (i = 0; i < bch->degree; i++) {
		if (!poly_eval(sigma, x))
			ecc[i >> 3] ^= 1 << (i & 7);
		x = gf13_divx(x);
	}
}
