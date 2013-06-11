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
#include <assert.h>
#include "dhara/bytes.h"
#include "util.h"
#include "jtutil.h"

static void recover(struct Dhara_Journal *j)
{
	int retry_count = 0;

	printf("    recover: start\n");

	while (Dhara_Journal_in_recovery(j)) {
		const Dhara_page_t p = Dhara_Journal_next_recoverable(j);
		uint8_t meta[DHARA_META_SIZE];
		Dhara_error_t err;

		if (Dhara_Journal_read_meta(j, p, meta, &err) < 0)
			dabort("read_meta", err);

		if (Dhara_Journal_copy(j, p, meta, &err) < 0) {
			if (err == DHARA_E_RECOVER) {
				printf("    recover: restart\n");
				if (++retry_count >= DHARA_MAX_RETRIES)
					dabort("recover", DHARA_E_TOO_BAD);
				continue;
			}

			dabort("copy", err);
		}

		Dhara_Journal_ack_recoverable(j);
	}

	printf("    recover: complete\n");
}

void jt_enqueue(struct Dhara_Journal *j, int i)
{
	const int page_size = 1 << j->nand->log2_page_size;
	uint8_t r[page_size];
	uint8_t meta[DHARA_META_SIZE];
	Dhara_error_t err;
	int retry_count = 0;

	seq_gen(i, r, page_size);
	Dhara_w32(meta, i);

retry:
	if (Dhara_Journal_enqueue(j, r, meta, &err) < 0) {
		if (err == DHARA_E_RECOVER) {
			recover(j);
			if (++retry_count >= DHARA_MAX_RETRIES)
				dabort("recover", DHARA_E_TOO_BAD);
			goto retry;
		}

		dabort("enqueue", err);
	}

	if (Dhara_Journal_read_meta(j, Dhara_Journal_root(j), meta, &err) < 0)
		dabort("read_meta", err);

	assert(Dhara_r32(meta) == i);
}

uint32_t jt_dequeue(struct Dhara_Journal *j, int expect)
{
	const int page_size = 1 << j->nand->log2_page_size;
	uint8_t r[page_size];
	uint8_t meta[DHARA_META_SIZE];
	const Dhara_page_t tail = Dhara_Journal_tail(j);
	uint32_t seed;
	Dhara_error_t err;

	if (Dhara_Journal_read_meta(j, tail, meta, &err) < 0)
		dabort("read_meta", err);

	seed = Dhara_r32(meta);
	assert((expect < 0) || (expect == seed));

	if (Dhara_NAND_read(j->nand, tail, 0, page_size, r,
			    &err) < 0)
		dabort("NAND_read", err);

	if (seed != 0xffffffff)
		seq_assert(seed, r, page_size);

	if (Dhara_Journal_dequeue(j, &err) < 0)
		dabort("dequeue", err);

	return seed;
}
