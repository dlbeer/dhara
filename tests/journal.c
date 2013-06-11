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
#include "dhara/journal.h"
#include "dhara/bytes.h"
#include "sim.h"
#include "util.h"

static void seq_enqueue(struct Dhara_Journal *j, int i)
{
	const int page_size = 1 << j->nand->log2_page_size;
	uint8_t r[page_size];
	uint8_t meta[DHARA_META_SIZE];
	Dhara_error_t err;

	seq_gen(i, r, page_size);
	Dhara_w32(meta, i);

	if (Dhara_Journal_enqueue(j, r, meta, &err) < 0)
		dabort("enqueue", err);

	if (Dhara_Journal_read_meta(j, Dhara_Journal_root(j), meta, &err) < 0)
		dabort("read_meta", err);

	assert(Dhara_r32(meta) == i);
}

static void seq_dequeue(struct Dhara_Journal *j, int expect)
{
	const int page_size = 1 << j->nand->log2_page_size;
	uint8_t r[page_size];
	uint8_t meta[DHARA_META_SIZE];
	const Dhara_page_t tail = Dhara_Journal_tail(j);
	int seed;
	Dhara_error_t err;

	if (Dhara_Journal_read_meta(j, tail, meta, &err) < 0)
		dabort("read_meta", err);

	seed = Dhara_r32(meta);
	assert((expect < 0) || (expect == seed));

	if (Dhara_NAND_read(j->nand, tail, 0, page_size, r,
			    &err) < 0)
		dabort("NAND_read", err);

	seq_assert(seed, r, page_size);

	if (Dhara_Journal_dequeue(j, &err) < 0)
		dabort("dequeue", err);
}

static void suspend_resume(struct Dhara_Journal *j)
{
	const Dhara_page_t old_root = Dhara_Journal_root(j);
	const Dhara_page_t old_tail = Dhara_Journal_tail(j);
	const Dhara_page_t old_head = j->head;
	Dhara_error_t err;

	j->root = DHARA_PAGE_NONE;
	j->head = DHARA_PAGE_NONE;
	j->tail = DHARA_PAGE_NONE;

	if (Dhara_Journal_resume(j, &err) < 0)
		dabort("resume", err);

	assert(old_root == Dhara_Journal_root(j));
	assert(old_tail == Dhara_Journal_tail(j));
	assert(old_head == j->head);
}

static void dump_info(struct Dhara_Journal *j)
{
	printf("    log2_ppc   = %d\n", j->log2_ppc);
	printf("    size       = %d\n", Dhara_Journal_size(j));
	printf("    capacity   = %d\n", Dhara_Journal_capacity(j));
	printf("    bb_current = %d\n", j->bb_current);
	printf("    bb_last    = %d\n", j->bb_last);
}

int main(void)
{
	struct Dhara_Journal journal;
	const size_t page_size = 1 << sim_nand.log2_page_size;
	uint8_t page_buf[page_size];
	int i;
	int rep;

	sim_inject_bad(20);

	printf("Journal init\n");
	Dhara_Journal_init(&journal, &sim_nand, page_buf);
	dump_info(&journal);
	printf("\n");

	printf("Enqueue/dequeue, 100 pages x20\n");
	for (rep = 0; rep < 20; rep++) {
		for (i = 0; i < 100; i++)
			seq_enqueue(&journal, i);
		printf("    size     = %d -> ", Dhara_Journal_size(&journal));
		for (i = 0; i < 100; i++)
			seq_dequeue(&journal, i);
		printf("%d\n", Dhara_Journal_size(&journal));
	}
	printf("\n");

	printf("Journal stats:\n");
	dump_info(&journal);
	printf("\n");

	printf("Enqueue/dequeue, ~100 pages x20 (resume)\n");
	for (rep = 0; rep < 20; rep++) {
		int j;

		for (i = 0; i < 100; i++)
			seq_enqueue(&journal, i);

		while (!Dhara_Journal_is_checkpointed(&journal))
			seq_enqueue(&journal, i++);

		printf("    size     = %d -> ", Dhara_Journal_size(&journal));
		suspend_resume(&journal);

		for (j = 0; j < i; j++)
			seq_dequeue(&journal, j);
		printf("%d\n", Dhara_Journal_size(&journal));
	}
	printf("\n");

	printf("Journal stats:\n");
	dump_info(&journal);
	printf("\n");

	sim_dump();

	return 0;
}
