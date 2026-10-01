/* SPDX-License-Identifier: MIT */
/* Levelled logging to stderr, with the <N> prefixes journald parses into priorities. */

#ifndef KV3P_LOG_H
#define KV3P_LOG_H

enum {
	LOG_L_ERR = 0,
	LOG_L_WARN = 1,
	LOG_L_INFO = 2,
	LOG_L_DEBUG = 3,
};

extern int log_level; /* messages above this are dropped; default LOG_L_INFO */

void log_msg(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#define log_err(...) log_msg(LOG_L_ERR, __VA_ARGS__)
#define log_warn(...) log_msg(LOG_L_WARN, __VA_ARGS__)
#define log_info(...) log_msg(LOG_L_INFO, __VA_ARGS__)
#define log_debug(...) log_msg(LOG_L_DEBUG, __VA_ARGS__)

#endif /* KV3P_LOG_H */
