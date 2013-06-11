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

#include "util.h"
#include "sim.h"

int main(void)
{
	int i;

	sim_reset();
	sim_inject_bad(5);

	for (i = 0; i < (1 << sim_nand.log2_ppb); i++) {
		int j;

		for (j = 0; j < sim_nand.num_blocks; j++) {
			uint8_t block[1 << sim_nand.log2_page_size];
			dhara_error_t err;
			dhara_page_t p =
				(j << sim_nand.log2_ppb) | i;

			if (dhara_nand_is_bad(&sim_nand, j))
				continue;

			if (!i && (dhara_nand_erase(&sim_nand, j, &err) < 0))
				dabort("erase", err);

			seq_gen(p, block, sizeof(block));
			if (dhara_nand_prog(&sim_nand, p, block, &err) < 0)
				dabort("prog", err);
		}
	}

	for (i = 0; i < (sim_nand.num_blocks << sim_nand.log2_ppb); i++) {
		uint8_t block[1 << sim_nand.log2_page_size];
		dhara_error_t err;

		if (dhara_nand_is_bad(&sim_nand, i >> sim_nand.log2_ppb))
			continue;

		if (dhara_nand_read(&sim_nand, i, 0, sizeof(block),
				    block, &err) < 0)
			dabort("read", err);

		seq_assert(i, block, sizeof(block));
	}

	sim_dump();
	return 0;
}
