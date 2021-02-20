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

static void test(void)
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

		printf("Rep: %d\n", rep);

		printf("    enqueue until error...\n");
		count = jt_enqueue_sequence(&journal, 0, -1);
		printf("    enqueue count: %d\n", count);
		printf("    size: %d\n", dhara_journal_size(&journal));

		printf("    dequeue...\n");
		jt_dequeue_sequence(&journal, 0, count);
		printf("    size: %d\n", dhara_journal_size(&journal));

		/* Only way to recover space here... */
		journal.tail_sync = journal.tail;
	}

	printf("\n");
}

int main(void)
{
	for (int i = 0; i < 100; i++) {
		printf("--------------------------------"
		       "--------------------------------\n");
		printf("Seed: %d\n", i);
		srandom(i);
		test();
	}

	return 0;
}
