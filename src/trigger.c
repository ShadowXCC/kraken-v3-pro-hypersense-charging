/* SPDX-License-Identifier: MIT */
#include "trigger.h"

#include <stdio.h>
#include <string.h>

void trigger_init(struct trigger *t, const struct trigger_cfg *cfg)
{
	memset(t, 0, sizeof(*t));
	t->cfg = *cfg;
	t->prev_charging = -1;
}

static void arm(struct trigger *t, long long now_ms, long long delay_ms, const char *reason)
{
	if (t->armed) /* an earlier trigger already has a send pending; don't push it later */
		return;
	t->armed = 1;
	t->due_ms = now_ms + delay_ms;
	t->reason = reason;
}

static void forget(struct trigger *t)
{
	t->have_identity = 0;
	t->identity[0] = '\0';
	t->prev_charging = -1;
	t->armed = 0;
	t->have_last_send = 0;
}

int trigger_tick(struct trigger *t, const struct trigger_in *in, const char **reason)
{
	if (!in->identity) { /* no dongle, or the driver has no control channel */
		forget(t);
		return 0;
	}

	if (!t->have_identity || strcmp(t->identity, in->identity) != 0) {
		/* A new dongle or a freshly (re)bound driver: the headset context is new, so
		 * nothing we sent before still applies. */
		forget(t);
		snprintf(t->identity, sizeof(t->identity), "%s", in->identity);
		t->have_identity = 1;
	}

	if (in->charging < 0) /* can't tell right now; leave any pending send alone */
		return 0;

	if (!in->charging) {
		t->armed = 0;
		t->prev_charging = 0;
		return 0;
	}

	if (t->prev_charging != 1)
		arm(t, in->now_ms, t->cfg.settle_ms,
		    t->prev_charging < 0 ? "already charging" : "charge-start");
	t->prev_charging = 1;

	if (in->rearm)
		arm(t, in->now_ms, t->cfg.settle_ms, in->rearm_reason ? in->rearm_reason : "re-arm");

	if (t->cfg.keepalive_ms > 0 && t->have_last_send &&
	    in->now_ms - t->last_send_ms >= t->cfg.keepalive_ms)
		arm(t, in->now_ms, 0, "keepalive");

	if (!t->armed || in->now_ms < t->due_ms)
		return 0;

	/* Debounce: stay armed and fire as soon as the interval has passed. */
	if (t->have_last_send && in->now_ms - t->last_send_ms < t->cfg.min_send_ms)
		return 0;

	t->armed = 0;
	*reason = t->reason;
	return 1;
}

void trigger_sent(struct trigger *t, long long now_ms)
{
	t->last_send_ms = now_ms;
	t->have_last_send = 1;
}
