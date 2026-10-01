/* SPDX-License-Identifier: MIT */
/* The whole re-send policy, driven by a scripted clock and scripted charge_status readings. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "trigger.h"

#define SETTLE 2000
#define MIN_SEND 5000

static const char *ID = "0003:1532:052C.001D@18";

static void setup(struct trigger *t, long long keepalive_ms)
{
	struct trigger_cfg cfg = { .settle_ms = SETTLE, .min_send_ms = MIN_SEND, .keepalive_ms = keepalive_ms };

	trigger_init(t, &cfg);
}

static int tick(struct trigger *t, int charging, long long now, const char **reason)
{
	struct trigger_in in = { .charging = charging, .identity = ID, .rearm = 0,
				 .rearm_reason = NULL, .now_ms = now };
	const char *r = "";
	int fired = trigger_tick(t, &in, &r);

	if (reason)
		*reason = r;
	if (fired)
		trigger_sent(t, now);
	return fired;
}

/* The cable goes in: one send, after the settle delay and not before. */
static void test_charge_start(void)
{
	struct trigger t;
	const char *reason = "";

	setup(&t, 0);
	assert(!tick(&t, 0, 0, NULL));
	assert(!tick(&t, 1, 1000, NULL));           /* armed, still settling */
	assert(!tick(&t, 1, 2500, NULL));           /* 1500 ms in: too early */
	assert(tick(&t, 1, 3000, &reason));         /* 2000 ms after the edge */
	assert(strcmp(reason, "charge-start") == 0);
	assert(!tick(&t, 1, 4000, NULL));           /* no repeats without an event */
	assert(!tick(&t, 1, 60000, NULL));
}

/* Already on the cable when the service starts: send once. */
static void test_already_charging_at_startup(void)
{
	struct trigger t;
	const char *reason = "";

	setup(&t, 0);
	assert(!tick(&t, 1, 0, NULL));
	assert(tick(&t, 1, SETTLE, &reason));
	assert(strcmp(reason, "already charging") == 0);
}

/* The headset power-cycles with the cable still in: charge_status dips 1 -> 0 -> 1. */
static void test_power_cycle_dip(void)
{
	struct trigger t;

	setup(&t, 0);
	assert(!tick(&t, 1, 0, NULL));
	assert(tick(&t, 1, SETTLE, NULL));          /* startup send at t=2000 */
	assert(!tick(&t, 0, 20000, NULL));          /* the dip */
	assert(!tick(&t, 1, 20500, NULL));          /* back on the cable: armed */
	assert(tick(&t, 1, 22500, NULL));           /* and sent after the settle */
}

/* Nothing on the cable: never send. */
static void test_never_sends_on_battery(void)
{
	struct trigger t;

	setup(&t, 0);
	for (long long now = 0; now < 120000; now += 500)
		assert(!tick(&t, 0, now, NULL));
}

/* "unknown" must not cancel a pending send, and must not trigger one. */
static void test_unknown_is_not_an_event(void)
{
	struct trigger t;

	setup(&t, 0);
	assert(!tick(&t, 1, 0, NULL));              /* arms at t=0, due t=2000 */
	assert(!tick(&t, -1, 1000, NULL));
	assert(tick(&t, 1, 2000, NULL));            /* still fires on time */

	setup(&t, 0);
	for (long long now = 0; now < 10000; now += 500)
		assert(!tick(&t, -1, now, NULL));
}

/* A new dongle or a freshly bound driver means a fresh headset context. */
static void test_identity_change_rearms(void)
{
	struct trigger t;
	struct trigger_in in = { .charging = 1, .identity = ID, .rearm = 0, .rearm_reason = NULL };
	const char *reason = "";

	setup(&t, 0);
	in.now_ms = 0;
	assert(!trigger_tick(&t, &in, &reason));
	in.now_ms = SETTLE;
	assert(trigger_tick(&t, &in, &reason));
	trigger_sent(&t, in.now_ms);

	in.identity = "0003:1532:052C.0021@19"; /* replugged: new devnum */
	in.now_ms = 3000;
	assert(!trigger_tick(&t, &in, &reason));
	in.now_ms = 5000;
	assert(trigger_tick(&t, &in, &reason));
	assert(strcmp(reason, "already charging") == 0);

	/* The device going away wipes the state rather than leaving a stale send pending. */
	in.identity = NULL;
	in.now_ms = 6000;
	assert(!trigger_tick(&t, &in, &reason));
}

/* An external re-arm (resume from suspend, or a retry after a failure). */
static void test_external_rearm(void)
{
	struct trigger t;
	struct trigger_in in = { .charging = 1, .identity = ID, .now_ms = 0 };
	const char *reason = "";

	setup(&t, 0);
	in.now_ms = 0;
	trigger_tick(&t, &in, &reason);
	in.now_ms = SETTLE;
	assert(trigger_tick(&t, &in, &reason));
	trigger_sent(&t, in.now_ms);

	in.rearm = 1;
	in.rearm_reason = "resume";
	in.now_ms = 30000;
	assert(!trigger_tick(&t, &in, &reason)); /* settles first */
	in.rearm = 0;
	in.rearm_reason = NULL;
	in.now_ms = 32000;
	assert(trigger_tick(&t, &in, &reason));
	assert(strcmp(reason, "resume") == 0);
}

/* Two events in quick succession must not become two sends inside the debounce window. */
static void test_debounce(void)
{
	struct trigger t;
	struct trigger_in in = { .charging = 1, .identity = ID, .now_ms = 0 };
	const char *reason = "";

	setup(&t, 0);
	in.now_ms = 0;
	trigger_tick(&t, &in, &reason);
	in.now_ms = SETTLE;
	assert(trigger_tick(&t, &in, &reason));
	trigger_sent(&t, in.now_ms);

	in.rearm = 1; /* 500 ms later, well inside min_send_ms */
	in.now_ms = SETTLE + 500;
	assert(!trigger_tick(&t, &in, &reason));
	in.rearm = 0;
	in.now_ms = SETTLE + 2600; /* settled, but still debounced */
	assert(!trigger_tick(&t, &in, &reason));
	in.now_ms = SETTLE + MIN_SEND; /* the window has passed: the pending send goes out */
	assert(trigger_tick(&t, &in, &reason));
}

/* keepalive_seconds is off by default and on only if asked for. */
static void test_keepalive(void)
{
	struct trigger t;
	long long now;
	int sends = 0;

	setup(&t, 0);
	for (now = 0; now <= 300000; now += 500)
		sends += tick(&t, 1, now, NULL);
	assert(sends == 1); /* the startup send, and nothing else */

	setup(&t, 60000);
	sends = 0;
	for (now = 0; now <= 300000; now += 500)
		sends += tick(&t, 1, now, NULL);
	/* the startup send at t=2000, then one every 60 s: 62000, 122000, 182000, 242000 */
	assert(sends == 5);
}

int main(void)
{
	test_charge_start();
	test_already_charging_at_startup();
	test_power_cycle_dip();
	test_never_sends_on_battery();
	test_unknown_is_not_an_event();
	test_identity_change_rearms();
	test_external_rearm();
	test_debounce();
	test_keepalive();
	printf("trigger: ok\n");
	return 0;
}
