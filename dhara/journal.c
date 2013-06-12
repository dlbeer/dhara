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

#include <string.h>
#include "journal.h"
#include "bytes.h"

/************************************************************************
 * Metapage binary format
 */

/* Does the page buffer contain a valid checkpoint page? */
static inline int hdr_has_magic(const uint8_t *buf)
{
	return (buf[0] == 'D') &&
	       (buf[1] == 'h') &&
	       (buf[2] == 'a');
}

static inline void hdr_put_magic(uint8_t *buf)
{
	buf[0] = 'D';
	buf[1] = 'h';
	buf[2] = 'a';
}

/* What epoch is this page? */
static inline uint8_t hdr_get_epoch(const uint8_t *buf)
{
	return buf[3];
}

static inline void hdr_set_epoch(uint8_t *buf, uint8_t e)
{
	buf[3] = e;
}

static inline dhara_page_t hdr_get_tail(const uint8_t *buf)
{
	return dhara_r32(buf + 4);
}

static inline void hdr_set_tail(uint8_t *buf, dhara_page_t tail)
{
	dhara_w32(buf + 4, tail);
}

static inline dhara_page_t hdr_get_bb_current(const uint8_t *buf)
{
	return dhara_r32(buf + 8);
}

static inline void hdr_set_bb_current(uint8_t *buf, dhara_page_t count)
{
	dhara_w32(buf + 8, count);
}

static inline dhara_page_t hdr_get_bb_last(const uint8_t *buf)
{
	return dhara_r32(buf + 12);
}

static inline void hdr_set_bb_last(uint8_t *buf, dhara_page_t count)
{
	dhara_w32(buf + 12, count);
}

/* Clear user metadata */
static inline void hdr_clear_user(uint8_t *buf, uint8_t log2_page_size)
{
	memset(buf + DHARA_HEADER_SIZE + DHARA_COOKIE_SIZE, 0xff,
	       (1 << log2_page_size) - DHARA_HEADER_SIZE - DHARA_COOKIE_SIZE);
}

/* Obtain pointers to user data */
static inline size_t hdr_user_offset(uint8_t which)
{
	return DHARA_HEADER_SIZE + DHARA_COOKIE_SIZE +
		which * DHARA_META_SIZE;
}

/************************************************************************/

/* Is this page index aligned to N bits? */
static inline int is_aligned(dhara_page_t p, int n)
{
	return !(p & ((1 << n) - 1));
}

/* Are these two pages from the same alignment group? */
static inline int align_eq(dhara_page_t a, dhara_page_t b,
			   int n)
{
	return !((a ^ b) >> n);
}

/* Calculate a checkpoint period: the largest value of ppc such that
 * (2**ppc - 1) metadata blocks can fit on a page with one journal
 * header.
 */
static int choose_ppc(int log2_page_size, int max)
{
	const int max_meta = (1 << log2_page_size) -
		DHARA_HEADER_SIZE - DHARA_COOKIE_SIZE;
	int total_meta = DHARA_META_SIZE;
	int ppc = 1;

	while (ppc < max) {
		total_meta <<= 1;
		total_meta += DHARA_META_SIZE;

		if (total_meta > max_meta)
			break;

		ppc++;
	}

	return ppc;
}

/* Clear recovery status */
static void clear_recovery(struct dhara_journal *j)
{
	j->recover_next = DHARA_PAGE_NONE;
	j->recover_root = DHARA_PAGE_NONE;
	j->recover_meta = DHARA_PAGE_NONE;
	j->recover_start = DHARA_PAGE_NONE;
}

/* Set up an empty journal */
static void reset_journal(struct dhara_journal *j)
{
	/* We don't yet have a bad block estimate, so make a
	 * conservative guess.
	 */
	j->epoch = 0;
	j->bb_last = j->nand->num_blocks >> 6;
	j->bb_current = 0;

	/* Empty journal */
	j->head = 0;
	j->tail = 0;
	j->root = DHARA_PAGE_NONE;

	/* No recovery required */
	clear_recovery(j);

	/* Empty metadata buffer */
	memset(j->page_buf, 0xff, 1 << j->nand->log2_page_size);
}

int advance_head_block(struct dhara_journal *j, dhara_error_t *err)
{
	dhara_block_t blk = j->head >> j->nand->log2_ppb;
	dhara_block_t bad_cur = j->bb_current;
	dhara_block_t bad_last = j->bb_last;
	uint8_t e = j->epoch;
	int i;

	for (i = 0; i < DHARA_MAX_RETRIES; i++) {
		blk++;
		if (blk >= j->nand->num_blocks) {
			blk = 0;
			e++;
			bad_last = bad_cur;
			bad_cur = 0;
		}

		if (blk == (j->tail >> j->nand->log2_ppb))
			break;

		if (!dhara_nand_is_bad(j->nand, blk)) {
			j->head = blk << j->nand->log2_ppb;
			j->bb_last = bad_last;
			j->bb_current = bad_cur;
			j->epoch = e;
			return 0;
		}

		bad_cur++;
	}

	dhara_set_error(err, DHARA_E_TOO_BAD);
	return -1;
}

void dhara_journal_init(struct dhara_journal *j,
			const struct dhara_nand *n,
			uint8_t *page_buf)
{
	/* Set fixed parameters */
	j->nand = n;
	j->page_buf = page_buf;
	j->log2_ppc = choose_ppc(n->log2_page_size, n->log2_ppb);

	reset_journal(j);
}

/* Find the first checkpoint-containing block. If a block contains any
 * checkpoints at all, then it must contain one in the first checkpoint
 * location -- otherwise, we would have considered the block eraseable.
 */
static int find_checkblock(struct dhara_journal *j,
			   dhara_block_t blk, dhara_block_t *where,
			   dhara_error_t *err)
{
	int i;

	for (i = 0; (blk < j->nand->num_blocks) &&
		    (i < DHARA_MAX_RETRIES); i++) {
		const dhara_page_t p =
			(blk << j->nand->log2_ppb) |
			((1 << j->log2_ppc) - 1);

		if (!(dhara_nand_is_bad(j->nand, blk) ||
		      dhara_nand_read(j->nand, p,
				      0, 1 << j->nand->log2_page_size,
				      j->page_buf, err)) &&
		    hdr_has_magic(j->page_buf)) {
			*where = blk;
			return 0;
		}

		blk++;
	}

	dhara_set_error(err, DHARA_E_TOO_BAD);
	return 0;
}

static dhara_block_t find_last_checkblock(struct dhara_journal *j,
					  dhara_block_t first)
{
	dhara_block_t low = first;
	dhara_block_t high = j->nand->num_blocks - 1;

	while (low <= high) {
		const dhara_block_t mid = (low + high) >> 1;
		dhara_block_t found;

		if ((find_checkblock(j, mid, &found, NULL) < 0) ||
		    (hdr_get_epoch(j->page_buf) != j->epoch)) {
			if (!mid)
				return first;

			high = mid - 1;
		} else {
			dhara_block_t nf;

			if (((found + 1) >= j->nand->num_blocks) ||
			    (find_checkblock(j, found + 1,
					     &nf, NULL) < 0) ||
			    (hdr_get_epoch(j->page_buf) != j->epoch))
				return found;

			low = nf;
		}
	}

	return first;
}

static dhara_page_t find_last_group(struct dhara_journal *j,
				    dhara_block_t blk)
{
	const int num_groups = 1 << (j->nand->log2_ppb - j->log2_ppc);
	int low = 0;
	int high = num_groups - 1;

	/* If any of the pages in a checkpoint group are programmed, the
	 * first user-page will be. If a checkpoint group is completely
	 * unprogrammed, everything following it will be completely
	 * unprogrammed also.
	 *
	 * Therefore, binary search checkpoint groups until we find the
	 * last one with a programmed first user-page.
	 */
	while (low <= high) {
		int mid = (low + high) >> 1;
		const dhara_page_t p = (mid << j->log2_ppc) |
				 (blk << j->nand->log2_ppb);

		if (dhara_nand_is_free(j->nand, p)) {
			high = mid - 1;
		} else if (((mid + 1) >= num_groups) ||
			   dhara_nand_is_free(j->nand,
				p + (1 << j->log2_ppc))) {
			return p;
		} else {
			low = mid + 1;
		}
	}

	return blk << j->nand->log2_ppb;
}

static int find_root(struct dhara_journal *j, dhara_page_t start,
		     dhara_error_t *err)
{
	const dhara_block_t blk = start >> j->nand->log2_ppb;
	int i = (start & ((1 << j->nand->log2_ppb) - 1)) >> j->log2_ppc;

	while (i >= 0) {
		const dhara_page_t p = (blk << j->nand->log2_ppb) +
			((i + 1) << j->log2_ppc) - 1;

		if (!dhara_nand_read(j->nand, p,
				     0, 1 << j->nand->log2_page_size,
				     j->page_buf, err) &&
		    (hdr_get_epoch(j->page_buf) == j->epoch)) {
			j->root = p - 1;
			return 0;
		}

		i--;
	}

	dhara_set_error(err, DHARA_E_TOO_BAD);
	return -1;
}

static int find_head(struct dhara_journal *j, dhara_page_t start,
		     dhara_error_t *err)
{
	j->head = start;

	do {
		/* Are we on the last user-page? If so, find the next
		 * free block.
		 */
		if (is_aligned(j->head + 2, j->nand->log2_ppb))
			return advance_head_block(j, err);

		/* Otherwise, skip to the next free userpage and try
		 * again.
		 */
		j->head++;
		if (is_aligned(j->head + 1, j->log2_ppc))
			j->head++;
	} while (!dhara_nand_is_free(j->nand, j->head));

	return 0;
}

int dhara_journal_resume(struct dhara_journal *j, dhara_error_t *err)
{
	dhara_block_t first, last;
	dhara_page_t last_group;

	/* Find the first checkpoint-containing block */
	if (find_checkblock(j, 0, &first, err) < 0) {
		reset_journal(j);
		return -1;
	}

	/* Find the last checkpoint-containing block in this epoch */
	j->epoch = hdr_get_epoch(j->page_buf);
	last = find_last_checkblock(j, first);

	/* Find the last programmed checkpoint group in the block */
	last_group = find_last_group(j, last);

	/* Perform a linear scan to find the last good checkpoint (and
	 * therefore the root).
	 */
	if (find_root(j, last_group, err) < 0) {
		reset_journal(j);
		return -1;
	}

	/* Restore settings from checkpoint */
	j->tail = hdr_get_tail(j->page_buf);
	j->bb_current = hdr_get_bb_current(j->page_buf);
	j->bb_last = hdr_get_bb_last(j->page_buf);
	hdr_clear_user(j->page_buf, j->nand->log2_page_size);

	/* Perform another linear scan to find the next free user page */
	if (find_head(j, last_group, err) < 0) {
		reset_journal(j);
		return -1;
	}

	clear_recovery(j);
	return 0;
}

dhara_page_t dhara_journal_capacity(const struct dhara_journal *j)
{
	const dhara_block_t max_bad = j->bb_last > j->bb_current ?
		j->bb_last : j->bb_current;
	const dhara_block_t good_blocks = j->nand->num_blocks - max_bad - 1;
	const int log2_cpb = j->nand->log2_ppb - j->log2_ppc;
	const dhara_page_t good_cps = good_blocks << log2_cpb;

	/* Good checkpoints * (checkpoint period - 1) */
	return (good_cps << j->log2_ppc) - good_cps;
}

dhara_page_t dhara_journal_size(const struct dhara_journal *j)
{
	/* Find the number of raw pages, and the number of checkpoints
	 * between the head and the tail. The difference between the two
	 * is the number of user pages (upper limit).
	 */
	dhara_page_t num_pages = j->head;
	dhara_page_t num_cps = j->head >> j->log2_ppc;

	if (j->head < j->tail) {
		const dhara_page_t total_pages =
			j->nand->num_blocks << j->nand->log2_ppb;

		num_pages += total_pages;
		num_cps += total_pages >> j->log2_ppc;
	}

	num_pages -= j->tail;
	num_cps -= j->tail >> j->log2_ppc;

	return num_pages - num_cps;
}

static int check_size(const struct dhara_journal *j, dhara_error_t *err)
{
	if (dhara_journal_size(j) >= dhara_journal_capacity(j)) {
		dhara_set_error(err, DHARA_E_JOURNAL_FULL);
		return -1;
	}

	return 0;
}

int dhara_journal_read_meta(struct dhara_journal *j, dhara_page_t p,
			    uint8_t *buf, dhara_error_t *err)
{
	/* Offset of metadata within the metadata page */
	const dhara_page_t ppc_mask = (1 << j->log2_ppc) - 1;
	const size_t offset = hdr_user_offset(p & ppc_mask);

	/* Special case: buffered metadata */
	if (align_eq(p, j->head, j->log2_ppc)) {
		memcpy(buf, j->page_buf + offset, DHARA_META_SIZE);
		return 0;
	}

	/* Special case: incomplete metadata dumped at start of
	 * recovery.
	 */
	if ((j->recover_meta != DHARA_PAGE_NONE) &&
	    align_eq(p, j->recover_root, j->log2_ppc))
		return dhara_nand_read(j->nand, j->recover_meta,
				       offset, DHARA_META_SIZE,
				       buf, err);

	/* General case: fetch from metadata page for checkpoint group */
	return dhara_nand_read(j->nand, p | ppc_mask,
			       offset, DHARA_META_SIZE,
			       buf, err);
}

int dhara_journal_dequeue(struct dhara_journal *j, dhara_error_t *err)
{
	dhara_page_t t = j->tail;

	if (t == j->head)
		return 0;

	/* Advance to the next user page (skip metadata pages) */
	t++;
	if (is_aligned(t + 1, j->log2_ppc))
		t++;

	/* Did we cross a block boundary? */
	if (is_aligned(t, j->nand->log2_ppb)) {
		dhara_block_t blk = j->tail >> j->nand->log2_ppb;
		int i;

		for (i = 0; i < DHARA_MAX_RETRIES; i++) {
			if (blk == (j->head >> j->nand->log2_ppb))
				break;

			blk++;
			if (blk >= j->nand->num_blocks)
				blk = 0;

			if (!dhara_nand_is_bad(j->nand, blk)) {
				j->tail = blk << j->nand->log2_ppb;
				return 0;
			}
		}

		dhara_set_error(err, DHARA_E_TOO_BAD);
		return -1;
	} else {
		j->tail = t;
	}

	return 0;
}

void dhara_journal_clear(struct dhara_journal *j)
{
	j->tail = j->head;
	j->root = DHARA_PAGE_NONE;

	hdr_clear_user(j->page_buf, j->nand->log2_page_size);
}

static void restart_recovery(struct dhara_journal *j, dhara_page_t old_head)
{
	/* Mark the current head bad immediately, unless we're also
	 * using it to hold our dumped metadata (it will then be marked
	 * bad at the end of recovery).
	 */
	if ((j->recover_meta == DHARA_PAGE_NONE) ||
	    !align_eq(j->recover_meta, old_head, j->nand->log2_ppb))
		dhara_nand_mark_bad(j->nand, old_head >> j->nand->log2_ppb);

	/* Start recovery again. Reset the source enumeration to
	 * the start of the original bad block, and reset the
	 * destination enumeration to the newly found good
	 * block.
	 */
	j->recover_start = j->head;
	j->recover_next =
		j->recover_root & ~((1 << j->nand->log2_ppb) - 1);

	j->root = j->recover_root;
}

static int dump_meta(struct dhara_journal *j, dhara_error_t *err)
{
	int i;

	/* We've just begun recovery on a new erasable block, but we
	 * have buffered metadata from the failed block.
	 */
	for (i = 0; i < DHARA_MAX_RETRIES; i++) {
		const dhara_block_t head_blk = j->head >> j->nand->log2_ppb;
		dhara_error_t my_err;

		/* Try to dump metadata on this page */
		if (!(dhara_nand_erase(j->nand, head_blk, &my_err) ||
		      dhara_nand_prog(j->nand, j->head,
				      j->page_buf, &my_err))) {
			j->recover_meta = j->head;
			j->head++;
			hdr_clear_user(j->page_buf, j->nand->log2_page_size);
			return 0;
		}

		/* Report fatal errors */
		if (my_err != DHARA_E_BAD_BLOCK) {
			dhara_set_error(err, my_err);
			return -1;
		}

		/* If the block went bad, try again on the next block */
		if (advance_head_block(j, err) < 0)
			return -1;

		dhara_nand_mark_bad(j->nand, head_blk);
	}

	dhara_set_error(err, DHARA_E_TOO_BAD);
	return -1;
}

static void recover_tail_fixup(struct dhara_journal *j, dhara_page_t bad_page)
{
	dhara_block_t blk = j->tail >> j->nand->log2_ppb;
	int i;

	if (!align_eq(j->tail, bad_page, j->nand->log2_ppb))
		return;

	for (i = 0; i < DHARA_MAX_RETRIES; i++) {
		blk++;
		if (blk >= j->nand->num_blocks)
			blk = 0;

		if (!dhara_nand_is_bad(j->nand, blk)) {
			j->tail = blk << j->nand->log2_ppb;
			break;
		}
	}
}

static int recover_from(struct dhara_journal *j,
			dhara_error_t write_err,
			dhara_error_t *err)
{
	const dhara_page_t old_head = j->head;

	if (write_err != DHARA_E_BAD_BLOCK) {
		dhara_set_error(err, write_err);
		return -1;
	}

	/* Find the next available block */
	if (advance_head_block(j, err) < 0)
		return -1;

	/* Are we already in the middle of a recovery? */
	if (j->recover_root != DHARA_PAGE_NONE) {
		restart_recovery(j, old_head);
		dhara_set_error(err, DHARA_E_RECOVER);
		return -1;
	}

	/* Were we block aligned? No recovery required! */
	if (is_aligned(old_head, j->nand->log2_ppb)) {
		dhara_nand_mark_bad(j->nand, old_head >> j->nand->log2_ppb);
		recover_tail_fixup(j, old_head);
		return 0;
	}

	j->recover_root = j->root;
	j->recover_next =
		j->recover_root & ~((1 << j->nand->log2_ppb) - 1);

	/* Are we holding buffered metadata? Dump it first. */
	if (!is_aligned(old_head, j->log2_ppc) &&
	    dump_meta(j, err) < 0)
		return -1;

	j->recover_start = j->head;
	dhara_set_error(err, DHARA_E_RECOVER);
	return -1;
}

static int push_meta(struct dhara_journal *j, const uint8_t *meta,
		     dhara_error_t *err)
{
	const dhara_page_t old_head = j->head;
	dhara_error_t my_err;
	const size_t offset =
		hdr_user_offset(j->head & ((1 << j->log2_ppc) - 1));

	/* We've just written a user page. Add the metadata to the
	 * buffer.
	 */
	j->head++;

	memcpy(j->page_buf + offset, meta, DHARA_META_SIZE);

	/* Unless we've filled the buffer, don't do any IO */
	if (!is_aligned(j->head + 1, j->log2_ppc)) {
		j->root = old_head;
		return 0;
	}

	/* We don't need to check for immediate recover, because that'll
	 * never happen -- we're not block-aligned.
	 */
	hdr_put_magic(j->page_buf);
	hdr_set_epoch(j->page_buf, j->epoch);
	hdr_set_tail(j->page_buf, j->tail);
	hdr_set_bb_current(j->page_buf, j->bb_current);
	hdr_set_bb_last(j->page_buf, j->bb_last);

	if (dhara_nand_prog(j->nand, j->head, j->page_buf, &my_err) < 0)
		return recover_from(j, my_err, err);

	hdr_clear_user(j->page_buf, j->nand->log2_page_size);

	/* Find the next free page */
	if (is_aligned(j->head + 1, j->nand->log2_ppb)) {
		if (advance_head_block(j, err) < 0) {
			j->head = old_head;
			return -1;
		}
	} else {
		j->head++;
	}

	j->root = old_head;
	return 0;
}

static int prepare_prog(const struct dhara_nand *n, dhara_page_t head,
			dhara_page_t tail, dhara_error_t *err)
{
	if (!is_aligned(head, n->log2_ppb))
		return 0;

	if ((head < tail) && align_eq(head, tail, n->log2_ppb)) {
		dhara_set_error(err, DHARA_E_JOURNAL_FULL);
		return -1;
	}

	return dhara_nand_erase(n, head >> n->log2_ppb, err);
}

int dhara_journal_enqueue(struct dhara_journal *j,
			  const uint8_t *data, const uint8_t *meta,
			  dhara_error_t *err)
{
	dhara_error_t my_err;
	int i;

	if (check_size(j, err) < 0)
		return -1;

	for (i = 0; i < DHARA_MAX_RETRIES; i++) {
		if (!(prepare_prog(j->nand, j->head, j->tail, &my_err) ||
		      dhara_nand_prog(j->nand, j->head, data, &my_err)))
			return push_meta(j, meta, err);

		if (recover_from(j, my_err, err) < 0)
			return -1;
	}

	dhara_set_error(err, DHARA_E_TOO_BAD);
	return -1;
}

int dhara_journal_copy(struct dhara_journal *j,
		       dhara_page_t p, const uint8_t *meta,
		       dhara_error_t *err)
{
	dhara_error_t my_err;
	int i;

	if (check_size(j, err) < 0)
		return -1;

	for (i = 0; i < DHARA_MAX_RETRIES; i++) {
		if (!(prepare_prog(j->nand, j->head, j->tail, &my_err) ||
		      dhara_nand_copy(j->nand, p, j->head, &my_err)))
			return push_meta(j, meta, err);

		if (recover_from(j, my_err, err) < 0)
			return -1;
	}

	dhara_set_error(err, DHARA_E_TOO_BAD);
	return -1;
}

int dhara_journal_is_checkpointed(const struct dhara_journal *j)
{
	return is_aligned(j->head, j->log2_ppc);
}

void dhara_journal_ack_recoverable(struct dhara_journal *j)
{
	if (!dhara_journal_in_recovery(j))
		return;

	if (j->recover_next == j->recover_root) {
		/* We just recovered the last page. Mark the recovered
		 * block as bad.
		 */
		dhara_nand_mark_bad(j->nand,
			j->recover_root >> j->nand->log2_ppb);

		/* If we had to dump metadata, and the page on which we
		 * did this also went bad, mark it bad too.
		 */
		if ((j->recover_meta != DHARA_PAGE_NONE) &&
		    !align_eq(j->recover_start, j->recover_meta,
			      j->nand->log2_ppb))
			dhara_nand_mark_bad(j->nand,
				j->recover_meta >> j->nand->log2_ppb);

		/* Was the tail on this page? Skip it forward */
		recover_tail_fixup(j, j->recover_root);
		clear_recovery(j);
	} else {
		/* Skip to next user page */
		j->recover_next++;
		if (is_aligned(j->recover_next + 1, j->log2_ppc))
			j->recover_next++;
	}
}
