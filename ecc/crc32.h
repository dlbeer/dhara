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

#ifndef ECC_CRC32_H_
#define ECC_CRC32_H_

#include <stdint.h>
#include <stddef.h>

#define CRC32_INIT	((uint32_t)0xffffffff)

/* Calculate the checksum over the given block of data, optionally
 * specifying a checksum to be carried.
 *
 * The polynominal representation used is one where the coefficients are
 * inverted. This is done so that the checksum of a fully erased block
 * (with erased checksum bytes) passes.
 *
 * If the carry argument is not needed, pass CRC32_INIT.
 *
 * The polynomial used is the IEEE 802.3 CRC32 polynomial, which has a
 * Hamming distance of 4 over 4096 bit messages. For more information,
 * see:
 *
 * Koopman, Philip (July 2002). "32-Bit Cyclic Redundancy Codes for
 * Internet Applications". The International Conference on Dependable
 * Systems and Networks: 459–468. doi:10.1109/DSN.2002.1028931. ISBN
 * 0-7695-1597-5. Retrieved 14 January 2011.
 */
uint32_t crc32_nand(const uint8_t *block, size_t len,
		    uint32_t carry);

#endif
