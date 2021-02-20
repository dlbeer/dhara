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

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "dhara/map.h"
#include "dhara/bytes.h"
#include "util.h"
#include "sim.h"

#define NUM_SECTORS		200
#define GC_RATIO		4

static dhara_sector_t sector_list[NUM_SECTORS];

static void shuffle(int seed)
{
	int i;

	srandom(seed);
	for (i = 0; i < NUM_SECTORS; i++)
		sector_list[i] = i;

	for (i = NUM_SECTORS - 1; i > 0; i--) {
		const int j = random() % i;
		const int tmp = sector_list[i];

		sector_list[i] = sector_list[j];
		sector_list[j] = tmp;
	}
}

static int check_recurse(struct dhara_map *m,
			 dhara_page_t parent,
			 dhara_page_t page,
			 dhara_sector_t id_expect,
			 int depth)
{
	uint8_t meta[DHARA_META_SIZE];
	dhara_error_t err;
	const dhara_page_t h_offset = m->journal.head - m->journal.tail;
	const dhara_page_t p_offset = parent - m->journal.tail;
	const dhara_page_t offset = page - m->journal.tail;
	dhara_sector_t id;
	int count = 1;
	int i;

	if (page == DHARA_PAGE_NONE)
		return 0;

	/* Make sure this is a valid journal user page, and one which is
	 * older than the page pointing to it.
	 */
	assert(offset < p_offset);
	assert(offset < h_offset);
	assert((~page) & ((1 << m->journal.log2_ppc) - 1));

	/* Fetch metadata */
	if (dhara_journal_read_meta(&m->journal, page, meta, &err) < 0)
		dabort("mt_check", err);

	/* Check the first <depth> bits of the ID field */
	id = dhara_r32(meta);
	if (!depth) {
		id_expect = id;
	} else {
		assert(!((id ^ id_expect) >> (32 - depth)));
	}

	/* Check all alt-pointers */
	for (i = depth; i < 32; i++) {
		dhara_page_t child = dhara_r32(meta + (i << 2) + 4);

		count += check_recurse(m, page, child,
			id ^ (1 << (31 - i)), i + 1);
	}

	return count;
}

static void mt_check(struct dhara_map *m)
{
	int count;

	sim_freeze();
	count = check_recurse(m, m->journal.head,
		dhara_journal_root(&m->journal), 0, 0);
	sim_thaw();

	assert(m->count == count);
}

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

static void mt_trim(struct dhara_map *m, dhara_sector_t s)
{
	dhara_error_t err;

	if (dhara_map_trim(m, s, &err) < 0)
		dabort("map_trim", err);
}

static void mt_assert_blank(struct dhara_map *m, dhara_sector_t s)
{
	dhara_error_t err;
	dhara_page_t loc;
	int r;

	r = dhara_map_find(m, s, &loc, &err);
	assert(r < 0);
	assert(err == DHARA_E_NOT_FOUND);
}

static void test(void)
{
	const size_t page_size = 1 << sim_nand.log2_page_size;
	uint8_t page_buf[page_size];
	struct dhara_map map;
	int i;

	printf("%d\n", (int)sizeof(map));

	sim_reset();
	sim_inject_bad(10);
	sim_inject_timebombs(30, 20);

	printf("Map init\n");
	dhara_map_init(&map, &sim_nand, page_buf, GC_RATIO);
	dhara_map_resume(&map, NULL);
	printf("  capacity: %d\n", dhara_map_capacity(&map));
	printf("  sector count: %d\n", NUM_SECTORS);
	printf("\n");

	printf("Sync...\n");
	dhara_map_sync(&map, NULL);
	printf("Resume...\n");
	dhara_map_init(&map, &sim_nand, page_buf, GC_RATIO);
	dhara_map_resume(&map, NULL);

	printf("Writing sectors...\n");
	shuffle(0);
	for (i = 0; i < NUM_SECTORS; i++) {
		const dhara_sector_t s = sector_list[i];

		mt_write(&map, s, s);
		mt_check(&map);
	}

	printf("Sync...\n");
	dhara_map_sync(&map, NULL);
	printf("Resume...\n");
	dhara_map_init(&map, &sim_nand, page_buf, GC_RATIO);
	dhara_map_resume(&map, NULL);
	printf("  capacity: %d\n", dhara_map_capacity(&map));
	printf("  use count: %d\n", dhara_map_size(&map));
	printf("\n");

	printf("Read back...\n");
	shuffle(1);
	for (i = 0; i < NUM_SECTORS; i++) {
		const dhara_sector_t s = sector_list[i];

		mt_assert(&map, s, s);
	}

	printf("Rewrite/trim half...\n");
	shuffle(2);
	for (i = 0; i < NUM_SECTORS; i += 2) {
		const dhara_sector_t s0 = sector_list[i];
		const dhara_sector_t s1 = sector_list[i + 1];

		mt_write(&map, s0, ~s0);
		mt_check(&map);
		mt_trim(&map, s1);
		mt_check(&map);
	}

	printf("Sync...\n");
	dhara_map_sync(&map, NULL);
	printf("Resume...\n");
	dhara_map_init(&map, &sim_nand, page_buf, GC_RATIO);
	dhara_map_resume(&map, NULL);
	printf("  capacity: %d\n", dhara_map_capacity(&map));
	printf("  use count: %d\n", dhara_map_size(&map));
	printf("\n");

	printf("Read back...\n");
	for (i = 0; i < NUM_SECTORS; i += 2) {
		const dhara_sector_t s0 = sector_list[i];
		const dhara_sector_t s1 = sector_list[i + 1];

		mt_assert(&map, s0, ~s0);
		mt_assert_blank(&map, s1);
	}

	printf("\n");
}

int main(void)
{
	for (int i = 0; i < 1000; i++) {
		printf("--------------------------------"
		       "--------------------------------\n");
		printf("Seed: %d\n", i);
		srandom(i);
		test();
	}

	sim_dump();
	return 0;
}
