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

#ifndef ECC_BCH_H_
#define ECC_BCH_H_

#include <stdint.h>
#include <stddef.h>

/* Bose-Chaudhuri-Hocquenghem error correcting codes, as described in:
 *
 * Hocquenghem, A. (September 1959), "Codes correcteurs d'erreurs",
 * Chiffres (in French) (Paris) 2: 147–156
 *
 * Bose, R. C.; Ray-Chaudhuri, D. K. (March 1960), "On A Class of Error
 * Correcting Binary Group Codes", Information and Control 3 (1): 68–79,
 * ISSN 0890-5401
 */
typedef uint64_t bch_poly_t;

struct bch_def {
	/* Number of syndromes to compute when decoding */
	int		syns;

	/* Generator polynominal, in reciprocal form: LSB is
	 * highest-order term.
	 */
	bch_poly_t	generator;

	/* Generator degree */
	int		degree;

	/* Number of ECC bytes */
	int		ecc_bytes;
};

/* Maximum number of ECC bytes (required for 4-bit codes). Some codes
 * require less than this.
 */
#define BCH_MAX_ECC		7

/* This is fixed. We must have that the number of bits in a chunk, plus
 * the number of bits of ECC, is less than the Galois field order. You
 * can supply chunks smaller than this.
 */
#define BCH_MAX_CHUNK_SIZE	(1023 - BCH_MAX_ECC)

/* BCH codes for 1, 2, 3 and 4-bit ECC */
extern const struct bch_def bch_1bit;
extern const struct bch_def bch_2bit;
extern const struct bch_def bch_3bit;
extern const struct bch_def bch_4bit;

/* Generate ECC bytes for the given page. */
void bch_generate(const struct bch_def *bch,
		  const uint8_t *chunk, size_t len,
		  uint8_t *ecc);

/* Verify the page. This doesn't correct data, but it's a cheaper
 * operation than syndrome calculation. Returns 0 on success or -1 if
 * the page requires correction.
 *
 * The ECC mask is constructed so that a fully erased page passes
 * verification.
 */
int bch_verify(const struct bch_def *bch,
	       const uint8_t *chunk, size_t len,
	       const uint8_t *ecc);

/* Correct errors. After correction, bch_verify() should be run again to
 * check for uncorrectable errors.
 */
void bch_repair(const struct bch_def *bch,
		uint8_t *chunk, size_t len, uint8_t *ecc);

#endif
