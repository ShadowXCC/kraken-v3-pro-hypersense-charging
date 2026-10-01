/* SPDX-License-Identifier: MIT */
#include "log.h"

#include <stdarg.h>
#include <stdio.h>

int log_level = LOG_L_INFO;

/* syslog priorities: 3 err, 4 warning, 6 info, 7 debug. systemd reads these off stderr. */
static const char *const prefixes[] = { "<3>", "<4>", "<6>", "<7>" };

void log_msg(int level, const char *fmt, ...)
{
	va_list ap;

	if (level > log_level)
		return;
	if (level < LOG_L_ERR || level > LOG_L_DEBUG)
		level = LOG_L_INFO;

	fputs(prefixes[level], stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	fflush(stderr);
}
