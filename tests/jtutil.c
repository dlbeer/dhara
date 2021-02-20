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
#include <stdlib.h>
#include "dhara/bytes.h"
#include "util.h"
#include "jtutil.h"

static void check_upage(const struct dhara_journal *j, dhara_page_t p)
{
	const dhara_page_t mask = (1 << j->log2_ppc) - 1;

	assert((~p) & mask);
	assert(p < (j->nand->num_blocks << j->nand->log2_ppb));
}

void jt_check(struct dhara_journal *j)
{
	/* Head and tail pointers always point to a valid user-page
	 * index (never a meta-page, and never out-of-bounds).
	 */
	check_upage(j, j->head);
	check_upage(j, j->tail);
	check_upage(j, j->tail_sync);

	/* The head never advances forward onto the same block as the
	 * tail.
	 */
	if (!((j->head ^ j->tail_sync) >> j->nand->log2_ppb)) {
		assert(j->head >= j->tail_sync);
	}

	/* The current tail is always between the head and the
	 * synchronized tail.
	 */
	assert((j->head - j->tail_sync) >= (j->tail - j->tail_sync));

	/* The root always points to a valid user page in a non-empty
	 * journal.
	 */
	if (j->root != DHARA_PAGE_NONE) {
		const dhara_page_t raw_size = j->head - j->tail;
		const dhara_page_t root_offset = j->root - j->tail;

		check_upage(j, j->root);
		assert(root_offset < raw_size);
	}
}

static void recover(struct dhara_journal *j)
{
	int retry_count = 0;

	printf("    recover: start\n");

	while (dhara_journal_in_recovery(j)) {
		const dhara_page_t p = dhara_journal_next_recoverable(j);
		dhara_error_t err;
		int ret;

		jt_check(j);

		if (p == DHARA_PAGE_NONE) {
			ret = dhara_journal_enqueue(j, NULL, NULL, &err);
		} else {
			uint8_t meta[DHARA_META_SIZE];

			if (dhara_journal_read_meta(j, p, meta, &err) < 0)
				dabort("read_meta", err);

			ret = dhara_journal_copy(j, p, meta, &err);
		}

		jt_check(j);

		if (ret < 0) {
			if (err == DHARA_E_RECOVER) {
				printf("    recover: restart\n");
				if (++retry_count >= DHARA_MAX_RETRIES)
					dabort("recover", DHARA_E_TOO_BAD);
				continue;
			}

			dabort("copy", err);
		}
	}

	jt_check(j);
	printf("    recover: complete\n");
}

static int enqueue(struct dhara_journal *j, uint32_t id, dhara_error_t *err)
{
	const int page_size = 1 << j->nand->log2_page_size;
	uint8_t r[page_size];
	uint8_t meta[DHARA_META_SIZE];
	dhara_error_t my_err;
	int i;

	seq_gen(id, r, page_size);
	dhara_w32(meta, id);

	for (i = 0; i < DHARA_MAX_RETRIES; i++) {
		jt_check(j);
		if (!dhara_journal_enqueue(j, r, meta, &my_err))
			return 0;

		if (my_err != DHARA_E_RECOVER) {
			dhara_set_error(err, my_err);
			return -1;
		}

		recover(j);
	}

	dhara_set_error(err, DHARA_E_TOO_BAD);
	return -1;
}

int jt_enqueue_sequence(struct dhara_journal *j, int start, int count)
{
	int i;

	if (count < 0)
		count = j->nand->num_blocks << j->nand->log2_ppb;

	for (i = 0; i < count; i++) {
		uint8_t meta[DHARA_META_SIZE];
		dhara_page_t root;
		dhara_error_t err;

		if (enqueue(j, start + i, &err) < 0) {
			if (err == DHARA_E_JOURNAL_FULL)
				return i;

			dabort("enqueue", err);
		}

		assert(dhara_journal_size(j) >= i);
		root = dhara_journal_root(j);

		if (dhara_journal_read_meta(j, root, meta, &err) < 0)
			dabort("read_meta", err);
		assert(dhara_r32(meta) == start + i);
	}

	return count;
}

void jt_dequeue_sequence(struct dhara_journal *j, int next, int count)
{
	const int max_garbage = 1 << j->log2_ppc;
	int garbage_count = 0;

	while (count > 0) {
		uint8_t meta[DHARA_META_SIZE];
		uint32_t id;
		dhara_page_t tail = dhara_journal_peek(j);
		dhara_error_t err;

		assert(tail != DHARA_PAGE_NONE);

		jt_check(j);
		if (dhara_journal_read_meta(j, tail, meta, &err) < 0)
			dabort("read_meta", err);

		jt_check(j);
		dhara_journal_dequeue(j);
		id = dhara_r32(meta);

		if (id == 0xffffffff) {
			garbage_count++;
			assert(garbage_count < max_garbage);
		} else {
			const int page_size = 1 << j->nand->log2_page_size;
			uint8_t r[page_size];

			assert(id == next);
			garbage_count = 0;
			next++;
			count--;

			if (dhara_nand_read(j->nand, tail, 0,
					    page_size, r, &err) < 0)
				dabort("nand_read", err);

			seq_assert(id, r, page_size);
		}

		if (count == 1)
			printf("head=%d, tail=%d, root=%d\n",
				j->head, j->tail, j->root);
	}

	jt_check(j);
}
