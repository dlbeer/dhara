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
#include "dhara/map.h"
#include "util.h"
#include "sim.h"

#define GC_RATIO		4

static void mt_write(struct dhara_map *m, dhara_sector_t s, int seed)
{
	const size_t page_size = 1 << m->journal.nand->log2_page_size;
	uint8_t buf[page_size];
	dhara_error_t err;

	seq_gen(seed, buf, sizeof(buf));
	if (dhara_map_write(m, s, buf, &err) < 0)
		dabort("map_write", err);
}

static void mt_assert(struct dhara_map *m, dhara_sector_t s, int seed)
{
	const size_t page_size = 1 << m->journal.nand->log2_page_size;
	uint8_t buf[page_size];
	dhara_error_t err;

	if (dhara_map_read(m, s, buf, &err) < 0)
		dabort("map_read", err);

	seq_assert(seed, buf, sizeof(buf));
}

int main(void)
{
	const size_t page_size = 1 << sim_nand.log2_page_size;
	struct dhara_map map;
	uint8_t page_buf[page_size];
	int write_seed = 0;
	int i;

	sim_reset();
	dhara_map_init(&map, &sim_nand, page_buf, GC_RATIO);
	dhara_map_resume(&map, NULL);
	printf("resumed, head = %d\n", map.journal.head);

	/* Write pages until we have just barely wrapped around, but not
	 * yet hit a checkpoint.
	 */
	for (i = 0; i < 200; i++)
		mt_write(&map, i, write_seed++);
	printf("written a little, head = %d\n", map.journal.head);

	for (i = 0; i < 200; i++)
		mt_write(&map, i, write_seed++);
	printf("written a little, head = %d\n", map.journal.head);
	for (i = 0; i < 200; i++)
		mt_write(&map, i, write_seed++);
	printf("written a little, head = %d\n", map.journal.head);
	for (i = 0; i < 79; i++)
		mt_write(&map, i, write_seed++);
	printf("written a little, head = %d\n", map.journal.head);
	assert(map.journal.head == 1); /* Required for this test */

	/* Now, see what happens on resume if we don't sync.
	 *
	 * Here's where a bug occured: the new epoch counter was not
	 * incremented when finding the next free user page, if that
	 * procedure required wrapping around the end of the chip from
	 * the last checkblock. From this point on, new pages written
	 * are potentially lost, because they will be wrongly identified
	 * as older than the pages coming physically later in the chip.
	 */
	printf("before resume: head = %d, tail = %d, epoch = %d\n",
		map.journal.head, map.journal.tail, map.journal.epoch);
	dhara_map_resume(&map, NULL);
	printf("resumed, head = %d, tail = %d, epoch = %d\n",
		map.journal.head, map.journal.tail, map.journal.epoch);

	for (i = 0; i < 2; i++)
		mt_write(&map, i, i + 10000);
	printf("written new data, head = %d\n", map.journal.head);
	dhara_map_sync(&map, NULL);

	/* Try another resume */
	printf("--------------------------------------------------------\n");
	printf("before resume: head = %d, tail = %d, epoch = %d\n",
		map.journal.head, map.journal.tail, map.journal.epoch);
	mt_assert(&map, 0, 10000);
	mt_assert(&map, 1, 10001);
	dhara_map_resume(&map, NULL);
	printf("resumed, head = %d, tail = %d, epoch = %d\n",
		map.journal.head, map.journal.tail, map.journal.epoch);
	mt_assert(&map, 0, 10000);
	mt_assert(&map, 1, 10001);

	return 0;
}
