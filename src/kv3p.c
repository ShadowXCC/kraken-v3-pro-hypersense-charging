/* SPDX-License-Identifier: MIT */
#include "kv3p.h"

#include <time.h>

const char *kv3p_strresult(int result)
{
	switch (result) {
	case KV3P_OK:
		return "ok";
	case KV3P_MISSING:
		return "no Kraken V3 Pro dongle on USB";
	case KV3P_BUSY:
		return "the control interface is held by another driver or process";
	case KV3P_GONE:
		return "the dongle went away";
	case KV3P_WEDGED:
		return "the dongle stopped accepting data (replug it)";
	case KV3P_ERR:
	default:
		return "failed";
	}
}

static long long clock_ms(clockid_t clk)
{
	struct timespec ts;

	if (clock_gettime(clk, &ts) < 0)
		return 0;
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

long long kv3p_now_ms(void)
{
	return clock_ms(CLOCK_MONOTONIC);
}

long long kv3p_boottime_ms(void)
{
	return clock_ms(CLOCK_BOOTTIME);
}
