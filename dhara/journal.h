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

#ifndef DHARA_JOURNAL_H_
#define DHARA_JOURNAL_H_

#include <stdint.h>
#include "nand.h"

/* Number of bytes used by the journal checkpoint header. */
#define DHARA_HEADER_SIZE		16

/* This is the size of the metadata slice which accompanies each written
 * page. This is independent of the underlying page/OOB size.
 */
#define DHARA_META_SIZE			132

/* When a block fails, or garbage is encountered, we try again on the
 * next block/checkpoint. We can do this up to the given number of
 * times.
 */
#define DHARA_MAX_RETRIES		8

/* This is a page number which can be used to represent "no such page".
 * It's guaranteed to never be a valid user page.
 */
#define DHARA_PAGE_NONE			((Dhara_page_t)0xffffffff)

/* The journal layer presents the NAND pages as a double-ended queue.
 * Pages, with associated metadata may be pushed onto the end of the
 * queue, and pages may be popped from the end.
 *
 * Block erase, metadata storage are handled automatically. Bad blocks
 * are handled by relocating data to the next available non-bad page in
 * the sequence.
 *
 * It's up to the user to ensure that the queue doesn't grow beyond the
 * capacity of the NAND chip, but helper functions are provided to
 * assist with this. If the head meets the tail, the journal will refuse
 * to enqueue more pages.
 */
struct Dhara_Journal {
	const struct Dhara_NAND		*nand;
	uint8_t				*page_buf;

	/* In the journal, user data is grouped into checkpoints of
	 * 2**log2_ppc contiguous aligned pages.
	 *
	 * The last page of each checkpoint contains the journal header
	 * and the metadata for the other pages in the period (the user
	 * pages).
	 */
	uint8_t				log2_ppc;

	/* Epoch counter. This is incremented whenever the journal head
	 * passes the end of the chip and wraps around.
	 */
	uint8_t				epoch;

	/* Bad-block counters. bb_last is our best estimate of the
	 * number of bad blocks in the chip as a whole. bb_current is
	 * the number of bad blocks in all blocks before the current
	 * head.
	 */
	Dhara_block_t			bb_current;
	Dhara_block_t			bb_last;

	/* Log head and tail. The tail pointer points to the last user
	 * page in the log, and the head point points to the next free
	 * raw page. The root points to the last written user page.
	 */
	Dhara_page_t			tail;
	Dhara_page_t			head;

	/* This points to the last written user page in the journal */
	Dhara_page_t			root;

	/* Recovery mode: recover_root points to the last valid user
	 * page in the block requiring recovery. recover_next points to
	 * the next user page needing recovery.
	 *
	 * If we had buffered metadata before recovery started, it will
	 * have been dumped to a free page, indicated by recover_meta.
	 * recover_start indicates the first free page used when the
	 * successful recovery started.
	 */
	Dhara_page_t			recover_next;
	Dhara_page_t			recover_root;
	Dhara_page_t			recover_meta;
	Dhara_page_t			recover_start;
};

/* Initialize a journal. You must supply a pointer to a NAND chip
 * driver, and a single page buffer. This page buffer will be used
 * exclusively by the journal, but you are responsible for allocating
 * it, and freeing it (if necessary) at the end.
 *
 * No NAND operations are performed at this point.
 */
void Dhara_Journal_init(struct Dhara_Journal *j,
			const struct Dhara_NAND *n,
			uint8_t *page_buf);

/* Start up the journal -- search the NAND for the journal head, or
 * initialize a blank journal if one isn't found. Returns 0 on success
 * or -1 if a (fatal) error occurs.
 *
 * This operation is O(log N), where N is the number of pages in the
 * NAND chip. All other operations are O(1).
 *
 * If this operation fails, the journal will be reset to an empty state.
 */
int Dhara_Journal_resume(struct Dhara_Journal *j, Dhara_error_t *err);

/* Obtain an upper bound on the number of user pages storable in the
 * journal.
 */
Dhara_page_t Dhara_Journal_capacity(const struct Dhara_Journal *j);

/* Obtain an upper bound on the number of user pages consumed by the
 * journal.
 */
Dhara_page_t Dhara_Journal_size(const struct Dhara_Journal *j);

/* Obtain the locations of the first and last pages in the journal.
 */
static inline Dhara_page_t Dhara_Journal_root(const struct Dhara_Journal *j)
{
	return j->root;
}

static inline Dhara_page_t Dhara_Journal_tail(const struct Dhara_Journal *j)
{
	return j->tail;
}

/* Read metadata associated with a page. This assumes that the page
 * provided is a valid data page. The actual page data is read via the
 * normal NAND interface.
 */
int Dhara_Journal_read_meta(struct Dhara_Journal *j, Dhara_page_t p,
			    uint8_t *buf, Dhara_error_t *err);

/* Remove the last page from the journal. This doesn't take permanent
 * effect until the next checkpoint.
 */
int Dhara_Journal_dequeue(struct Dhara_Journal *j, Dhara_error_t *err);

/* Append a page to the journal. Both raw page data and metadata must be
 * specified. The push operation is not persistent until a checkpoint is
 * reached.
 *
 * This operation may fail with the error code E_RECOVER. If this
 * occurs, the upper layer must complete the assisted recovery procedure
 * and then try again.
 *
 * This operation may be used as part of a recovery. If further errors
 * occur during recovery, E_RECOVER is returned, and the procedure must
 * be restarted.
 */
int Dhara_Journal_enqueue(struct Dhara_Journal *j,
			  const uint8_t *data, const uint8_t *meta,
			  Dhara_error_t *err);

/* Copy an existing page to the front of the journal. New metadata must
 * be specified. This operation is not persistent until a checkpoint is
 * reached.
 *
 * This operation may fail with the error code E_RECOVER. If this
 * occurs, the upper layer must complete the assisted recovery procedure
 * and then try again.
 *
 * This operation may be used as part of a recovery. If further errors
 * occur during recovery, E_RECOVER is returned, and the procedure must
 * be restarted.
 */
int Dhara_Journal_copy(struct Dhara_Journal *j,
		       Dhara_page_t p, const uint8_t *meta,
		       Dhara_error_t *err);

/* Is the journal checkpointed? If true, then all pages enqueued are now
 * persistent.
 */
int Dhara_Journal_is_checkpointed(const struct Dhara_Journal *j);

/* These two functions comprise the assisted recovery procedure. If an
 * enqueue or copy operation returns an error of E_RECOVER, the journal
 * has been placed into recovery mode.
 *
 * Call Dhara_Journal_next_recoverable() to obtain the user page which
 * needs recovery. Perform whatever operations are necessary to recover
 * the page (usually copy() with updated metadata), and then call
 * Dhara_Journal_ack_recoverable().
 *
 * If a further E_RECOVER error occurs during recovery, this indicates
 * that recovery needs to be restarted -- DO NOT call ack_recoverable()
 * after receiving this error.
 *
 * Bad-block marking will be performed automatically (after recovering
 * the last user page, and after a recovery failure).
 */
static inline int Dhara_Journal_in_recovery(const struct Dhara_Journal *j)
{
	return j->recover_root != DHARA_PAGE_NONE;
}

static inline Dhara_page_t Dhara_Journal_next_recoverable
	(const struct Dhara_Journal *j)
{
	return j->recover_next;
}

void Dhara_Journal_ack_recoverable(struct Dhara_Journal *j);

#endif
