/* SPDX-License-Identifier: MIT */
/* The /etc config file: a handful of key = value lines, no sections. */

#ifndef KV3P_CONFIG_H
#define KV3P_CONFIG_H

#define CONFIG_DEFAULT_PATH "/etc/kraken-hypersense-charging.conf"

struct config {
	int chroma;              /* also clear the Chroma gate (0x93) */
	double poll_seconds;     /* how often charge_status is read; a pure memory read in the driver */
	double settle_seconds;   /* wait after charge-start so the headset has asserted the gate first */
	double min_send_interval;/* debounce between two sends */
	double keepalive_seconds;/* 0 = off: re-send every N s while charging */
};

void config_defaults(struct config *c);

/* KV3P_OK when the file was read, KV3P_MISSING when there is none (defaults kept),
 * KV3P_ERR when it exists but could not be read. Bad values warn and keep the default. */
int config_load(struct config *c, const char *path);

#endif /* KV3P_CONFIG_H */
