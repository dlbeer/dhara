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

#ifndef ECC_HAMMING_H_
#define ECC_HAMMING_H_

#include <stdint.h>
#include <stddef.h>

/* ECC size is fixed. Chunk size can't be any larger than the maximum
 * given below. Hamming codes can correct 1-bit errors and detect 2-bit
 * errors within a chunk.
 */
#define HAMMING_MAX_CHUNK_SIZE	512
#define HAMMING_ECC_SIZE	3

/* Generate ECC bytes for the given page */
void hamming_generate(const uint8_t *chunk, size_t len, uint8_t *ecc);

/* Calculate ECC parity for a given page. If zero, the page is ok. */
typedef uint32_t hamming_ecc_t;

hamming_ecc_t hamming_syndrome(const uint8_t *chunk, size_t len,
			       const uint8_t *ecc);

/* Attempt to repair ECC errors. Returns 0 if successful, -1 if an error
 * occurs.
 */
int hamming_repair(uint8_t *chunk, size_t len, hamming_ecc_t syndrome);

#endif
