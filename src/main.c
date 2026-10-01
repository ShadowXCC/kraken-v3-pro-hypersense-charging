/* SPDX-License-Identifier: MIT */
/*
 * kraken-hypersense-charging: keep the Razer Kraken V3 Pro's HyperSense haptics (and, optionally,
 * its earcup Chroma) working while the headset is on the charge cable.
 *
 * The headset's firmware does not refuse haptics while charging because of anything physical: the
 * NXH3670 radio chip simply tells the STM32 "gate off" when the charger appears, and the STM32
 * then acks HyperSense commands and throws them away. The host can clear that gate again with one
 * command, which the dongle relays unfiltered. The gate lives in RAM, so it comes back every time
 * the headset is re-charged, power-cycled or re-linked -- hence a service rather than a one-off.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "borrow.h"
#include "config.h"
#include "frame.h"
#include "gate.h"
#include "kv3p.h"
#include "log.h"
#include "sysfs.h"
#include "trigger.h"

#define RESUME_GAP_MS 2000
#define MAX_RETRIES 5

static volatile sig_atomic_t stop_flag;

static void on_signal(int sig)
{
	(void)sig;
	stop_flag = 1;
}

struct opts {
	const char *config_path;
	int dry_run;
	int off;
	int if_charging;
	int no_chroma;
};

struct snapshot {
	struct dongle d;
	int have_dongle;
	int have_hid;
	int have_driver;
	char hid[HIDNAME_LEN];
	char driver[DRIVER_LEN];
	int charging; /* 1, 0, or -1 when nothing can tell us */
	char identity[TRIGGER_IDENT_LEN];
};

static void snapshot(struct snapshot *s)
{
	memset(s, 0, sizeof(*s));
	s->charging = -1;

	s->have_dongle = sysfs_find_dongle(&s->d) == KV3P_OK;
	if (s->have_dongle)
		s->have_driver = sysfs_interface_driver(s->d.sysname, KV3P_IF_COMM, s->driver,
							sizeof(s->driver)) == KV3P_OK;
	s->have_hid = kraken_hid_device(s->hid, sizeof(s->hid)) == KV3P_OK;

	if (!s->have_dongle || !s->have_hid)
		return;

	snprintf(s->identity, sizeof(s->identity), "%s@%d", s->hid, s->d.devnum);

	/* charge_status only means anything while the driver holds the control channel: it is a
	 * cached value fed by the dongle's battery notifications. */
	if (s->have_driver && strcmp(s->driver, KV3P_DRIVER_RAZER) == 0) {
		int cs = kraken_charge_status(s->hid);

		s->charging = cs >= 0 ? cs : -1;
	}
}

static void print_frame(const char *label, uint8_t cmd, int enable)
{
	uint8_t frame[KV3P_FRAME_LEN];
	char hex[3 * KV3P_FRAME_LEN + 1];

	if (gate_build(frame, cmd, enable) != KV3P_OK) {
		printf("%s: refused by the allowlist\n", label);
		return;
	}
	frame_hex(frame, sizeof(frame), hex, sizeof(hex));
	printf("%-20s %s\n", label, hex);
}

/* Borrow the channel, clear (or re-apply) the gates, hand it straight back. */
static int send_gates(const struct config *cfg, int enable)
{
	struct borrow b;
	struct kv3p_ack ack;
	int ret;

	ret = borrow_acquire(&b);
	if (ret != KV3P_OK) {
		log_err("cannot reach the dongle: %s", kv3p_strresult(ret));
		return ret;
	}

	ret = borrow_send(&b, GATE_CMD_HAPTIC, enable, &ack);
	if (ret == KV3P_OK && ack.status != 0) {
		log_warn("switch_haptic(%d) was rejected: status=0x%02x (headset off or link down?)",
			 enable, ack.status);
		ret = KV3P_ERR;
	} else if (ret == KV3P_OK) {
		log_info("switch_haptic(%d): acked", enable);
	} else {
		log_err("switch_haptic(%d): %s", enable, kv3p_strresult(ret));
	}

	if (cfg->chroma && ret == KV3P_OK) {
		struct kv3p_ack chroma_ack;
		int cret = borrow_send(&b, GATE_CMD_CHROMA, enable, &chroma_ack);

		if (cret == KV3P_OK && chroma_ack.status == 0)
			log_info("switch_chroma(%d): acked", enable);
		else
			log_warn("switch_chroma(%d) did not take: %s", enable,
				 cret == KV3P_OK ? "rejected by the headset" : kv3p_strresult(cret));
	}

	borrow_release(&b);
	return ret;
}

static int mode_once(const struct config *cfg, const struct opts *o)
{
	int enable = o->off ? 0 : 1;

	if (o->dry_run) {
		struct snapshot s;

		snapshot(&s);
		printf("dry run: nothing is sent\n");
		print_frame(o->off ? "switch_haptic(0)" : "switch_haptic(1)", GATE_CMD_HAPTIC, enable);
		if (cfg->chroma)
			print_frame(o->off ? "switch_chroma(0)" : "switch_chroma(1)", GATE_CMD_CHROMA, enable);
		printf("%-20s %s\n", "control interface",
		       s.have_dongle ? (s.have_driver ? s.driver : "unbound") : "no dongle");
		return 0;
	}

	if (o->if_charging) {
		struct snapshot s;

		snapshot(&s);
		if (s.charging != 1) {
			log_info("not charging (charge_status %s); nothing to do",
				 s.charging < 0 ? "unknown" : "0");
			return 0;
		}
	}

	return send_gates(cfg, enable) == KV3P_OK ? 0 : 1;
}

static int mode_status(const struct config *cfg)
{
	struct snapshot s;

	snapshot(&s);

	if (s.have_dongle)
		printf("%-18s %04x:%04x at %s (bus %03d dev %03d), node %s\n", "dongle:", KV3P_VID,
		       KV3P_PID, s.d.sysname, s.d.busnum, s.d.devnum, s.d.node);
	else
		printf("%-18s not plugged in\n", "dongle:");

	if (s.have_dongle)
		printf("%-18s %s -> %s\n", "control iface:", s.d.ifid,
		       s.have_driver ? s.driver : "(no driver)");

	printf("%-18s %s\n", "kraken sysfs:", s.have_hid ? s.hid : "not bound by razerkraken");
	printf("%-18s %s\n", "charge_status:",
	       s.charging == 1 ? "1 (on the cable)" :
	       s.charging == 0 ? "0 (on battery)" : "unknown (the driver has no control channel)");
	printf("%-18s chroma=%s poll=%.2fs settle=%.2fs min_interval=%.2fs keepalive=", "config:",
	       cfg->chroma ? "yes" : "no", cfg->poll_seconds, cfg->settle_seconds, cfg->min_send_interval);
	if (cfg->keepalive_seconds > 0)
		printf("%.2fs\n", cfg->keepalive_seconds);
	else
		printf("off\n");

	if (geteuid() != 0)
		printf("\nnote: run as root to read charge_status and to send anything.\n");
	return 0;
}

static int mode_daemon(const struct config *cfg)
{
	struct trigger_cfg tc = {
		.settle_ms = (long long)(cfg->settle_seconds * 1000.0),
		.min_send_ms = (long long)(cfg->min_send_interval * 1000.0),
		.keepalive_ms = (long long)(cfg->keepalive_seconds * 1000.0),
	};
	struct timespec interval = {
		.tv_sec = (time_t)cfg->poll_seconds,
		.tv_nsec = (long)((cfg->poll_seconds - (double)(time_t)cfg->poll_seconds) * 1e9),
	};
	struct trigger t;
	long long last_mono = kv3p_now_ms();
	long long last_boot = kv3p_boottime_ms();
	long long retry_at = 0;
	int fails = 0;
	int wedged_devnum = -1;
	int said_no_channel = 0;

	trigger_init(&t, &tc);
	log_info("kraken-hypersense-charging " KV3P_VERSION ": poll %.2fs, settle %.2fs, chroma %s, keepalive %s",
		 cfg->poll_seconds, cfg->settle_seconds, cfg->chroma ? "on" : "off",
		 cfg->keepalive_seconds > 0 ? "on" : "off");

	while (!stop_flag) {
		struct snapshot s;
		struct trigger_in in;
		const char *reason = "";
		const char *rearm_reason = NULL;
		long long now = kv3p_now_ms();
		long long boot = kv3p_boottime_ms();
		long long slept = (boot - last_boot) - (now - last_mono);
		int rearm = 0;

		if (slept > RESUME_GAP_MS) {
			log_info("resumed after %.0f s suspended; re-checking the gate", (double)slept / 1000.0);
			rearm = 1;
			rearm_reason = "resume";
		}
		last_mono = now;
		last_boot = boot;

		snapshot(&s);

		if (s.have_dongle && wedged_devnum >= 0 && s.d.devnum != wedged_devnum) {
			log_info("the dongle was replugged; sending again");
			wedged_devnum = -1;
		}
		if (s.have_dongle && s.have_hid && s.charging < 0 && !said_no_channel) {
			log_info("%s is on '%s', not %s, so the charge state is unknown; waiting",
				 s.d.ifid, s.have_driver ? s.driver : "no driver", KV3P_DRIVER_RAZER);
			said_no_channel = 1;
		} else if (s.charging >= 0) {
			said_no_channel = 0;
		}

		if (!rearm && retry_at && now >= retry_at) {
			rearm = 1;
			rearm_reason = "retry";
			retry_at = 0;
		}

		in.charging = s.charging;
		in.identity = s.identity[0] ? s.identity : NULL;
		in.rearm = rearm;
		in.rearm_reason = rearm_reason;
		in.now_ms = now;

		if (trigger_tick(&t, &in, &reason)) {
			trigger_sent(&t, now);
			if (strcmp(reason, "retry") != 0)
				fails = 0;

			if (wedged_devnum >= 0 && s.have_dongle && s.d.devnum == wedged_devnum) {
				log_debug("skipping the '%s' send: the dongle is wedged until replugged", reason);
			} else {
				int ret;

				log_info("%s: clearing the haptic%s gate", reason,
					 cfg->chroma ? " and Chroma" : "");
				ret = send_gates(cfg, 1);
				if (ret == KV3P_OK) {
					fails = 0;
					retry_at = 0;
				} else if (ret == KV3P_WEDGED) {
					wedged_devnum = s.have_dongle ? s.d.devnum : -1;
					log_err("the dongle stopped accepting data; replug it. "
						"Nothing more will be sent until then.");
				} else if (++fails <= MAX_RETRIES) {
					long long backoff = 5000LL << (fails - 1);

					if (backoff > 60000)
						backoff = 60000;
					retry_at = kv3p_now_ms() + backoff;
					log_warn("that did not work; trying again in %llds", backoff / 1000);
				} else {
					log_err("giving up after %d attempts; waiting for the next event", fails);
				}
			}
		}

		nanosleep(&interval, NULL);
	}

	log_info("stopping");
	return 0;
}

static void usage(FILE *out)
{
	fprintf(out,
		"kraken-hypersense-charging " KV3P_VERSION " -- keep Razer Kraken V3 Pro haptics on while charging\n"
		"\n"
		"usage: kraken-hypersense-charging [options] [daemon|once|status]\n"
		"\n"
		"  daemon          watch for the charger and clear the gate when it appears (default)\n"
		"  once            clear the gate now and exit\n"
		"  status          show what the service can see, and exit\n"
		"\n"
		"options:\n"
		"  -c, --config PATH   config file (default " CONFIG_DEFAULT_PATH ")\n"
		"  -n, --dry-run       with 'once': print the frames, send nothing\n"
		"      --off           with 'once': re-apply the block instead of clearing it\n"
		"      --if-charging   with 'once': do nothing unless the headset is on the cable\n"
		"      --no-chroma     do not touch the Chroma gate this run\n"
		"  -v, --verbose       debug logging\n"
		"  -q, --quiet         errors only\n"
		"  -h, --help          this text\n"
		"  -V, --version       print the version\n");
}

int main(int argc, char **argv)
{
	struct opts o = { .config_path = CONFIG_DEFAULT_PATH };
	struct config cfg;
	const char *mode = NULL;
	struct sigaction sa;
	int ret;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(stdout);
			return 0;
		} else if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
			printf("%s\n", KV3P_VERSION);
			return 0;
		} else if (!strcmp(a, "-c") || !strcmp(a, "--config")) {
			if (++i >= argc) {
				fprintf(stderr, "%s needs a path\n", a);
				return 2;
			}
			o.config_path = argv[i];
		} else if (!strcmp(a, "-n") || !strcmp(a, "--dry-run")) {
			o.dry_run = 1;
		} else if (!strcmp(a, "--off")) {
			o.off = 1;
		} else if (!strcmp(a, "--if-charging")) {
			o.if_charging = 1;
		} else if (!strcmp(a, "--no-chroma")) {
			o.no_chroma = 1;
		} else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) {
			log_level = LOG_L_DEBUG;
		} else if (!strcmp(a, "-q") || !strcmp(a, "--quiet")) {
			log_level = LOG_L_ERR;
		} else if (a[0] == '-') {
			fprintf(stderr, "unknown option '%s'\n", a);
			usage(stderr);
			return 2;
		} else if (!mode) {
			mode = a;
		} else {
			fprintf(stderr, "unexpected argument '%s'\n", a);
			return 2;
		}
	}

	if (!mode)
		mode = "daemon";
	if (strcmp(mode, "daemon") && strcmp(mode, "once") && strcmp(mode, "status")) {
		fprintf(stderr, "unknown mode '%s'\n", mode);
		usage(stderr);
		return 2;
	}

	config_defaults(&cfg);
	ret = config_load(&cfg, o.config_path);
	if (ret == KV3P_ERR)
		log_warn("could not read %s; using the defaults", o.config_path);
	else if (ret == KV3P_MISSING)
		log_debug("no %s; using the defaults", o.config_path);
	if (o.no_chroma)
		cfg.chroma = 0;

	if (!strcmp(mode, "status"))
		return mode_status(&cfg);

	/* --dry-run sends nothing, so it is the one mode that does not need root. */
	if (geteuid() != 0 && !(o.dry_run && !strcmp(mode, "once"))) {
		log_err("this needs root: it reads the driver's sysfs attributes and moves the "
			"dongle's control interface between drivers");
		return 1;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	if (!strcmp(mode, "once"))
		return mode_once(&cfg, &o);
	return mode_daemon(&cfg);
}
