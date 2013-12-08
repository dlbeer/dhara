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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/************************************************************************
 * Polynomial functions
 */

typedef uint64_t poly_t;

#define MAX_DEGREE (sizeof(poly_t) * 8 - 1)

static poly_t reciprocal(poly_t p)
{
	poly_t r = 0;

	while (p) {
		r <<= 1;
		r |= (p & 1);
		p >>= 1;
	}

	return r;
}

static poly_t mul_poly(poly_t a, poly_t b)
{
	poly_t r = 0;

	while (a) {
		if (a & 1)
			r ^= b;

		a >>= 1;
		b <<= 1;
	}

	return r;
}

static int degree(poly_t p)
{
	int d = 0;

	while (p > 1) {
		p >>= 1;
		d++;
	}

	return d;
}

static void print_poly(char var, poly_t p)
{
	int have_term = 0;
	int i;

	printf("0x%lx [0x%lx] -> ", p, reciprocal(p));

	for (i = MAX_DEGREE; i >= 0; i--) {
		if (!((p >> i) & 1))
			continue;

		if (have_term)
			printf(" + ");

		if (!i)
			printf("1");
		else if (i == 1)
			printf("%c", var);
		else
			printf("%c^%d", var, i);

		have_term = 1;
	}
}

/************************************************************************
 * Galois field exponential tables
 */

typedef uint16_t gf_elem_t;

static gf_elem_t gf_order;
static gf_elem_t gf_degree;
static gf_elem_t *gf_exp;
static gf_elem_t *gf_log;

static int gf_init(poly_t generator)
{
	const int deg = degree(generator);
	const int tab_size = 1 << deg;
	poly_t a = 1;
	int i;

	if (deg > sizeof(gf_elem_t) * 8) {
		fprintf(stderr, "gf_init: generator degree out of bounds\n");
		return -1;
	}

	gf_order = tab_size - 1;
	gf_degree = deg;

	gf_exp = malloc(tab_size * 2 * sizeof(gf_exp[0]));
	if (!gf_exp) {
		perror("gf_init: malloc");
		return -1;
	}

	gf_log = gf_exp + tab_size;

	for (i = 0; i < tab_size; i++) {
		gf_exp[i] = a;
		gf_log[a] = i;

		a <<= 1;
		if (a >> deg)
			a ^= generator;
	}

	return 0;
}

static void gf_exit(void)
{
	free(gf_exp);
}

static void gf_dump(const gf_elem_t *tab)
{
	const int tab_size = 1 << gf_degree;
	const int digits = (gf_degree + 3) >> 2;
	const int per_line = 72 / (digits + 5);
	char fmt[16];
	int i;

	snprintf(fmt, sizeof(fmt), "0x%%0%dx,", digits);

	for (i = 0; i < tab_size; i++) {
		if (i % per_line)
			printf(" ");

		printf(fmt, tab[i]);

		if (!((i + 1) % per_line))
			printf("\n");
	}

	if (i % per_line)
		printf("\n");
}

static inline gf_elem_t mod_s(gf_elem_t x)
{
	return (x >= gf_order) ? (x - gf_order) : x;
}

/************************************************************************
 * Polynomial search
 */

static inline void bit_clear(uint8_t *map, poly_t i)
{
	const uint8_t mask = 1 << (i & 7);
	const size_t pos = i >> 3;

	map[pos] &= ~mask;
}

static inline void bit_set(uint8_t *map, poly_t i)
{
	const uint8_t mask = 1 << (i & 7);
	const size_t pos = i >> 3;

	map[pos] |= mask;
}

static inline int bit_test(const uint8_t *map, poly_t i)
{
	const uint8_t mask = 1 << (i & 7);
	const size_t pos = i >> 3;

	return map[pos] & mask;
}

static inline size_t bit_memsz(poly_t nbits)
{
	return (nbits + 7) >> 3;
}

static int is_primitive(poly_t p)
{
	const int d = degree(p);
	const int max_order = (1 << d) - 1;
	poly_t r = 1;
	poly_t start;
	int i;

	/* Choose a starting element */
	for (i = 0; i < max_order; i++) {
		r <<= 1;
		if (r >> d)
			r ^= p;
	}

	/* Keep going until we hit the same element again */
	start = r;
	i = 0;
	do {
		r <<= 1;
		if (r >> d)
			r ^= p;
		i++;
	} while (r != start);

	return i == max_order;
}

static int poly_search(int degree)
{
	poly_t i;

	for (i = 1 << degree; (i >> degree) <= 1; i++)
		if (is_primitive(i)) {
			print_poly('x', i);
			printf("\n");
		}

	return 0;
}

/************************************************************************
 * BCH utilities
 */

/* Substitute an element into a polynomial */
static gf_elem_t eval_poly(poly_t p, gf_elem_t x)
{
	const gf_elem_t log_x = gf_log[x];
	gf_elem_t r = 0;
	gf_elem_t log_t = 1;

	p = reciprocal(p);

	while (p) {
		if (p & 1)
			r ^= gf_exp[log_t];

		p >>= 1;
		log_t = mod_s(log_t + log_x);
	}

	return r;
}

/* Find the minimal polynomial of the given element */
static gf_elem_t minimal(gf_elem_t x)
{
	poly_t i;

	for (i = 1; (i >> gf_degree) <= 1; i++)
		if (!eval_poly(i, x))
			break;

	return i;
}

/* Find the product of minimal polynomials of the first N powers of
 * alpha.
 */
static int bch_generator(gf_elem_t n, poly_t *out)
{
	const size_t bm_memsz = bit_memsz(1 << (gf_degree + 1));
	uint8_t *bm;
	poly_t r = 1;
	gf_elem_t i;

	if (n >= gf_order) {
		fprintf(stderr, "bch_generator: syndrome count "
			"out of bounds\n");
		return -1;
	}

	bm = malloc(bm_memsz);
	if (!bm) {
		perror("bch_generator: malloc");
		return -1;
	}

	memset(bm, 0, bm_memsz);

	printf("BCH generator, %d roots:\n", n);

	for (i = 1; i <= n; i += 2) {
		const poly_t m = minimal(gf_exp[i]);

		printf("    min(alpha^%d): ", i);
		print_poly('x', m);

		if (!bit_test(bm, m)) {
			if (degree(m) + degree(r) > MAX_DEGREE) {
				fprintf(stderr, "bch_generator: overflow\n");
				return -1;
			}

			r = mul_poly(r, m);
			bit_set(bm, m);
			printf("\n");
		} else {
			printf(" [dup]\n");
		}
	}

	printf("    generator: ");
	print_poly('x', r);
	printf("\n");

	if (out)
		*out = r;

	free(bm);
	return 0;
}

/************************************************************************
 * User interface
 */

static void usage(const char *progname)
{
	printf("usage:\n"
"    %s search <degree>\n"
"        Search for primitive polynomials.\n"
"    %s <generator> exp\n"
"        Print GF(2^m) exponential table.\n"
"    %s <generator> log\n"
"        Print GF(2^m) logarithm table.\n"
"    %s <generator> bch <syndrome count>\n"
"        Find roots and product BCH generator.\n",
	progname, progname, progname, progname);
}

static int parse_poly(const char *text, poly_t *out)
{
	poly_t r = 0;

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
	poly_t p;
	int r = 0;

	if (argc < 3) {
		usage(argv[0]);
		return 1;
	}

	if (!strcasecmp(argv[1], "search"))
		return poly_search(atoi(argv[2]));

	if (parse_poly(argv[1], &p) < 0)
		return -1;

	if (gf_init(p) < 0)
		return -1;

	if (!strcasecmp(argv[2], "exp")) {
		gf_dump(gf_exp);
	} else if (!strcasecmp(argv[2], "log")) {
		gf_dump(gf_log);
	} else if (!strcasecmp(argv[2], "bch")) {
		poly_t gen;

		if (argc < 4) {
			usage(argv[0]);
			return 1;
		}

		if (bch_generator(atoi(argv[3]), &gen) < 0)
			r = -1;
	} else {
		fprintf(stderr, "unknown operation: %s\n", argv[2]);
		r = -1;
	}

	gf_exit();
	return r;
}
