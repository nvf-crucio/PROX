/*
  Copyright(c) 2010-2017 Intel Corporation.
  Copyright(c) 2016-2018 Viosoft Corporation.
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

#ifndef _HANDLE_LAT_H_
#define _HANDLE_LAT_H_

#include <stdio.h>
#include <math.h>
#include <string.h>

#include "task_base.h"
#include "clock.h"

#define MAX_PACKETS_FOR_LATENCY 64
#define LATENCY_ACCURACY	1

struct lat_test {
	uint64_t tot_all_pkts;
	uint64_t tot_pkts;
	uint64_t max_lat;
	uint64_t min_lat;
	uint64_t tot_lat;
	unsigned __int128 var_lat; /* variance */
	uint64_t accuracy_limit_tsc;

	uint64_t max_lat_error;
	uint64_t min_lat_error;
	uint64_t tot_lat_error;
	unsigned __int128 var_lat_error;

	uint64_t buckets[128];
	uint64_t bucket_size;
	uint64_t lost_packets;
};

static struct time_unit lat_test_get_accuracy_limit(struct lat_test *lat_test)
{
	return tsc_to_time_unit(lat_test->accuracy_limit_tsc);
}

static struct time_unit_err lat_test_get_avg(struct lat_test *lat_test)
{
	uint64_t tsc;
	uint64_t tsc_error;

	tsc = lat_test->tot_lat/lat_test->tot_pkts;
	tsc_error = lat_test->tot_lat_error/lat_test->tot_pkts;

	struct time_unit_err ret = {
		.time = tsc_to_time_unit(tsc),
		.error = tsc_to_time_unit(tsc_error),
	};

	return ret;
}

static struct time_unit_err lat_test_get_min(struct lat_test *lat_test)
{
	struct time_unit_err ret = {
		.time = tsc_to_time_unit(lat_test->min_lat),
		.error = tsc_to_time_unit(lat_test->min_lat_error),
	};

	return ret;
}

static struct time_unit_err lat_test_get_max(struct lat_test *lat_test)
{
	struct time_unit_err ret = {
		.time = tsc_to_time_unit(lat_test->max_lat),
		.error = tsc_to_time_unit(lat_test->max_lat_error),
	};

	return ret;
}

static struct time_unit_err lat_test_get_stddev(struct lat_test *lat_test)
{
	unsigned __int128 avg_tsc = lat_test->tot_lat/lat_test->tot_pkts;
	unsigned __int128 avg_tsc_squared = avg_tsc * avg_tsc;
	unsigned __int128 avg_squares_tsc = lat_test->var_lat/lat_test->tot_pkts;

	/* The assumption is that variance fits into 64 bits, meaning
	   that standard deviation fits into 32 bits. In other words,
	   the assumption is that the standard deviation is not more
	   than approximately 1 second. */
	uint64_t var_tsc = avg_squares_tsc - avg_tsc_squared;
	uint64_t stddev_tsc = sqrt(var_tsc);

	unsigned __int128 avg_tsc_error = lat_test->tot_lat_error / lat_test->tot_pkts;
	unsigned __int128 avg_tsc_squared_error = 2 * avg_tsc * avg_tsc_error + avg_tsc_error * avg_tsc_error;
	unsigned __int128 avg_squares_tsc_error = lat_test->var_lat_error / lat_test->tot_pkts;

	uint64_t var_tsc_error = avg_squares_tsc_error + avg_tsc_squared_error;

	/* sqrt(a+-b) = sqrt(a) +- (-sqrt(a) + sqrt(a + b)) */

	uint64_t stddev_tsc_error = - stddev_tsc + sqrt(var_tsc + var_tsc_error);

	struct time_unit_err ret = {
		.time = tsc_to_time_unit(stddev_tsc),
		.error = tsc_to_time_unit(stddev_tsc_error),
	};

	return ret;
}

static void _lat_test_histogram_combine(struct lat_test *dst, struct lat_test *src)
{
	for (size_t i = 0; i < sizeof(dst->buckets)/sizeof(dst->buckets[0]); ++i)
		dst->buckets[i] += src->buckets[i];
}

static void lat_test_combine(struct lat_test *dst, struct lat_test *src)
{
	dst->tot_all_pkts += src->tot_all_pkts;

	dst->tot_pkts += src->tot_pkts;

	dst->tot_lat += src->tot_lat;
	dst->tot_lat_error += src->tot_lat_error;

	/* (a +- b)^2 = a^2 +- (2ab + b^2) */
	dst->var_lat += src->var_lat;
	dst->var_lat_error += src->var_lat_error;

	if (src->max_lat > dst->max_lat) {
		dst->max_lat = src->max_lat;
		dst->max_lat_error = src->max_lat_error;
	}
	if (src->min_lat < dst->min_lat) {
		dst->min_lat = src->min_lat;
		dst->min_lat_error = src->min_lat_error;
	}

	if (src->accuracy_limit_tsc > dst->accuracy_limit_tsc)
		dst->accuracy_limit_tsc = src->accuracy_limit_tsc;
	dst->lost_packets += src->lost_packets;

#ifdef LATENCY_HISTOGRAM
	_lat_test_histogram_combine(dst, src);
#endif
}

static void lat_test_reset(struct lat_test *lat_test)
{
	lat_test->tot_all_pkts = 0;
	lat_test->tot_pkts = 0;
	lat_test->max_lat = 0;
	lat_test->min_lat = -1;
	lat_test->tot_lat = 0;
	lat_test->var_lat = 0;
	lat_test->max_lat_error = 0;
	lat_test->min_lat_error = 0;
	lat_test->tot_lat_error = 0;
	lat_test->var_lat_error = 0;
	lat_test->accuracy_limit_tsc = 0;

	lat_test->lost_packets = 0;

	memset(lat_test->buckets, 0, sizeof(lat_test->buckets));
}

static void lat_test_copy(struct lat_test *dst, struct lat_test *src)
{
	if (src->tot_all_pkts)
		memcpy(dst, src, sizeof(struct lat_test));
}

struct task_lat;

struct lat_test *task_lat_get_latency_meassurement(struct task_lat *task);
void task_lat_use_other_latency_meassurement(struct task_lat *task);
void task_lat_set_accuracy_limit(struct task_lat *task, uint32_t accuracy_limit_nsec);

#endif /* _HANDLE_LAT_H_ */
