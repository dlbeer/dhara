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

#ifndef DHARA_ERROR_H_
#define DHARA_ERROR_H_

typedef enum {
	DHARA_E_NONE = 0,
	DHARA_E_BAD_BLOCK,
	DHARA_E_ECC,
	DHARA_E_MAX
} Dhara_error_t;

/* Produce a human-readable error message. This function is kept in a
 * separate compilation unit and can be omitted to reduce binary size.
 */
const char *Dhara_strerror(Dhara_error_t err);

/* Save an error */
static inline void Dhara_set_error(Dhara_error_t *err, Dhara_error_t v)
{
	if (err)
		*err = v;
}

#endif
