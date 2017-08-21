/*
  Copyright(c) 2010-2017 Intel Corporation.
  Copyright(c) 2016-2017 Viosoft Corporation.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "display_priority.h"
#include "stats_prio_task.h"
#include "display.h"
#include "lconf.h"

#define PRIORITY_COUNT 8

static struct display_page display_page_priority;
static struct display_column *stats_tx[PRIORITY_COUNT];
static struct display_column *stats_drop[PRIORITY_COUNT];
static struct display_column *core_col;
static struct display_column *name_col;

static void display_priority_draw_frame(struct screen_state *state)
{
  	uint32_t n_tasks = stats_get_n_prio_tasks_tot();
	struct lcore_cfg *lconf = NULL;
	struct task_args *targ;
	char name[32];
	char *ptr;

	display_page_init(&display_page_priority);

	struct display_table *core_name = display_page_add_table(&display_page_priority);

	display_table_init(core_name, "Core/task");
	core_col = display_table_add_col(core_name);
	name_col = display_table_add_col(core_name);
	display_column_init(core_col, "Nb", 4);
	display_column_init(name_col, "Name", 5);

	struct display_table *stats = display_page_add_table(&display_page_priority);
	if (state->toggle == 0) {
		display_table_init(stats, "Statistics per second");

		char title[64];
		for (int i = 0; i < PRIORITY_COUNT; ++i) {
			stats_tx[i] = display_table_add_col(stats);
			snprintf(title, sizeof(title), "TX %d (K)", i);
			display_column_init(stats_tx[i], title, 9);

			stats_drop[i] = display_table_add_col(stats);
			snprintf(title, sizeof(title), "DRP %d (K)", i);
			display_column_init(stats_drop[i], title, 9);
		}
	} else {
		display_table_init(stats, "Total statistics");

		char title[64];
		for (int i = 0; i < PRIORITY_COUNT; ++i) {
			stats_tx[i] = display_table_add_col(stats);
			snprintf(title, sizeof(title), "TX %d (#)", i);
			display_column_init(stats_tx[i], title, 9);

			stats_drop[i] = display_table_add_col(stats);
			snprintf(title, sizeof(title), "DRP %d (#)", i);
			display_column_init(stats_drop[i], title, 9);
		}
	}

	display_page_draw_frame(&display_page_priority, n_tasks);

	uint32_t count = 0;
	lconf = NULL;
	while (core_targ_next(&lconf, &targ, 0) == 0) {
		if (strcmp(targ->task_init->mode_str, "aggreg") == 0) {
			display_column_print_core_task(core_col, count, lconf, targ);
			if (targ->id == 0)
				display_column_print(name_col, count, "%s", lconf->name);
			count++;
		}
	}
}

static void display_priority_draw_stats(struct screen_state *state)
{
	uint64_t rx_prio;
	uint64_t drop_tx_fail_prio;
	struct lcore_cfg *lconf = NULL;
	struct task_args *targ;
	const uint32_t n_stats_prio = stats_get_n_prio_tasks_tot();

	if (state->toggle == 0) {
		for (uint32_t count = 0; count < n_stats_prio; ++count) {
			struct prio_task_stats_sample *last = stats_get_prio_task_stats_sample(count, 1);
			struct prio_task_stats_sample *prev = stats_get_prio_task_stats_sample(count, 0);

			uint64_t delta_t = (last->tsc - prev->tsc) * 1000;
			if (delta_t == 0) // This could happen if we just reset the screen => stats will be updated later
				continue;

			for (uint8_t i = 0; i < PRIORITY_COUNT; i++) {
				rx_prio = last->rx_prio[i] - prev->rx_prio[i];
				drop_tx_fail_prio = last->drop_tx_fail_prio[i] - prev->drop_tx_fail_prio[i];

				display_column_print(stats_tx[i], count, "%9lu", val_to_rate(rx_prio, delta_t));
				display_column_print(stats_drop[i], count, "%9lu", val_to_rate(drop_tx_fail_prio, delta_t));
			}
		}
	} else {
		for (uint32_t count = 0; count < n_stats_prio; ++count) {
			for (uint8_t i = 0; i < PRIORITY_COUNT; i++) {
				rx_prio = stats_core_task_tot_rx_prio(count, i);
				drop_tx_fail_prio = stats_core_task_tot_drop_tx_fail_prio(count, i);

				display_column_print(stats_tx[i], count, "%9lu", rx_prio);
				display_column_print(stats_drop[i], count, "%9lu", drop_tx_fail_prio);
			}
		}
	}
}

static int display_priority_get_height(void)
{
	return stats_get_n_prio_tasks_tot();
}

static struct display_screen display_screen_priority = {
	.draw_frame = display_priority_draw_frame,
	.draw_stats = display_priority_draw_stats,
	.get_height = display_priority_get_height,
	.title = "priority",
};

struct display_screen *display_priority(void)
{
	return &display_screen_priority;
}
