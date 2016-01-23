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

#ifndef ECC_GF13_H_
#define ECC_GF13_H_

#include <stdint.h>

/* Galois field tables of order 2^13-1 */
#define GF13_ORDER	8191

typedef uint16_t	gf13_elem_t;

/* If you need to reduce the code size, you can define GF13_NO_TABLES.
 *
 * This results in much smaller (but also much slower) code.
 */

#ifdef GF13_NO_TABLES

gf13_elem_t gf13_mul(gf13_elem_t a, gf13_elem_t b);
gf13_elem_t gf13_div(gf13_elem_t a, gf13_elem_t b);

static inline gf13_elem_t gf13_divx(gf13_elem_t a)
{
	return gf13_mul(a, 0x100d);
}

static inline gf13_elem_t gf13_mulx(gf13_elem_t a)
{
	gf13_elem_t r = a << 1;

	if (r & 8192)
		r ^= 0x201b;

	return r;
}

#else

extern const gf13_elem_t gf13_exp[8192];
extern const gf13_elem_t gf13_log[8192];

/* Wrappers for field arithmetic */
static inline gf13_elem_t gf13_wrap(gf13_elem_t s)
{
	return (s >= GF13_ORDER) ? (s - GF13_ORDER) : s;
}

static inline gf13_elem_t gf13_mul(gf13_elem_t a, gf13_elem_t b)
{
	return gf13_exp[gf13_wrap(gf13_log[a] + gf13_log[b])];
}

static inline gf13_elem_t gf13_div(gf13_elem_t a, gf13_elem_t b)
{
	return gf13_exp[gf13_wrap(gf13_log[a] + GF13_ORDER - gf13_log[b])];
}

static inline gf13_elem_t gf13_divx(gf13_elem_t a)
{
	return gf13_exp[gf13_wrap(gf13_log[a] + GF13_ORDER - 1)];
}

static inline gf13_elem_t gf13_mulx(gf13_elem_t a)
{
	return gf13_exp[gf13_wrap(gf13_log[a] + 1)];
}

#endif

#endif
