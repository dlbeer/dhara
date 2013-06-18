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

#ifndef TESTS_SIM_H_
#define TESTS_SIM_H_

#include "dhara/nand.h"

/* Simulated NAND layer. This layer reads and writes to an in-memory
 * buffer.
 */
extern const struct dhara_nand sim_nand;

/* Reset to start-up defaults */
void sim_reset(void);

/* Dump statistics and status */
void sim_dump(void);

/* Halt/resume counting of statistics */
void sim_freeze(void);
void sim_thaw(void);

/* Set faults on individual blocks */
void sim_set_failed(dhara_block_t blk);
void sim_set_timebomb(dhara_block_t blk, int ttl);

/* Create some factory-marked bad blocks */
void sim_inject_bad(int count);

/* Create some unmarked bad blocks */
void sim_inject_failed(int count);

/* Create a timebomb on the given block */
void sim_inject_timebombs(int count, int max_ttl);

#endif
