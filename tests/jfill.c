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
#include <assert.h>
#include <string.h>
#include "dhara/journal.h"
#include "dhara/bytes.h"
#include "sim.h"
#include "util.h"
#include "jtutil.h"

static int fill_journal(struct dhara_journal *j)
{
	const size_t page_size = 1 << sim_nand.log2_page_size;
	const int max_pages = j->nand->num_blocks << j->nand->log2_ppb;
	int count = 0;

	for (;;) {
		uint8_t meta[DHARA_META_SIZE];
		uint8_t my_page[page_size];
		dhara_error_t err;

		seq_gen(count, my_page, page_size);
		dhara_w32(meta, count);

		if (dhara_journal_enqueue(j, my_page, meta, &err) < 0) {
			printf("    error after %d pages: %d (%s)\n",
			       count, err, dhara_strerror(err));
			break;
		} else {
			count++;
			assert(count < max_pages);
		}
	}

	return count;
}

int main(void)
{
	struct dhara_journal journal;
	const size_t page_size = 1 << sim_nand.log2_page_size;
	uint8_t page_buf[page_size];
	int rep;

	sim_reset();
	sim_inject_bad(10);
	sim_inject_failed(10);

	printf("Journal init\n");
	dhara_journal_init(&journal, &sim_nand, page_buf);
	printf("    capacity: %d\n", dhara_journal_capacity(&journal));
	printf("\n");

	for (rep = 0; rep < 5; rep++) {
		int count;
		int i;

		printf("Rep: %d\n", rep);

		printf("    shift head...\n");
		jt_enqueue(&journal, 0xff);
		jt_dequeue(&journal, 0xff);

		printf("    enqueue until error...\n");
		count = fill_journal(&journal);
		printf("    size: %d\n", dhara_journal_size(&journal));

		printf("    dequeue...\n");
		for (i = 0; i < count; i++)
			jt_dequeue(&journal, i);
		printf("    size: %d\n", dhara_journal_size(&journal));
	}

	printf("\n");
	sim_dump();
	return 0;
}
