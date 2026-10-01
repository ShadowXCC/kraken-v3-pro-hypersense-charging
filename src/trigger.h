/* SPDX-License-Identifier: MIT */
/*
 * When to re-send the gate commands. Pure logic: it does no I/O and reads no clock of its own,
 * so the whole policy is unit-testable against a scripted sequence of ticks.
 *
 * The gate is headset RAM, and the NXH3670 re-asserts it over an internal chip link that no
 * host-side monitor can see. So the service cannot observe the block coming back -- it can only
 * infer it from the events that cause it:
 *
 *   1. charge_status 0 -> 1   the cable went in (the main case)
 *   2. first tick, or a new dongle/driver instance  a fresh headset context means a fresh gate
 *   3. the 1 -> 0 -> 1 dip    the headset power-cycled with the cable still in: its first battery
 *                             report after a link-up carries state 0x00
 *   4. an external re-arm     resume from suspend, which the daemon spots as a CLOCK_BOOTTIME gap
 */

#ifndef KV3P_TRIGGER_H
#define KV3P_TRIGGER_H

#define TRIGGER_IDENT_LEN 96

struct trigger_cfg {
	long long settle_ms;    /* delay between the trigger and the send */
	long long min_send_ms;  /* debounce */
	long long keepalive_ms; /* 0 = off */
};

struct trigger_in {
	int charging;         /* 1 charging, 0 not, -1 unknown (no device, driver has no channel) */
	const char *identity; /* hid device + devnum; NULL when there is nothing to talk to */
	int rearm;            /* external one-shot re-arm (resume from suspend, or a retry) */
	const char *rearm_reason; /* label for the log; "re-arm" when NULL */
	long long now_ms;     /* CLOCK_MONOTONIC */
};

struct trigger {
	struct trigger_cfg cfg;
	char identity[TRIGGER_IDENT_LEN];
	int have_identity;
	int prev_charging; /* -1 = nothing seen yet for this identity */
	int armed;
	long long due_ms;
	const char *reason;
	long long last_send_ms;
	int have_last_send;
};

void trigger_init(struct trigger *t, const struct trigger_cfg *cfg);

/* 1 when a send should happen now, with *reason set to a short label for the log. */
int trigger_tick(struct trigger *t, const struct trigger_in *in, const char **reason);

/* Record that a send was attempted, for the debounce and the keepalive. */
void trigger_sent(struct trigger *t, long long now_ms);

#endif /* KV3P_TRIGGER_H */
