/* SPDX-License-Identifier: MIT */
#include "borrow.h"

#include <string.h>

#include "gate.h"
#include "kv3p.h"
#include "log.h"

#define ACK_TIMEOUT_MS 1000
#define READ_CHUNK 1024

static void block_signals(struct borrow *b)
{
	sigset_t mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, SIGQUIT);
	if (sigprocmask(SIG_BLOCK, &mask, &b->oldmask) == 0)
		b->signals_blocked = 1;
}

static void unblock_signals(struct borrow *b)
{
	if (b->signals_blocked) {
		sigprocmask(SIG_SETMASK, &b->oldmask, NULL);
		b->signals_blocked = 0;
	}
}

int borrow_acquire(struct borrow *b)
{
	char driver[DRIVER_LEN];
	int ret;

	memset(b, 0, sizeof(*b));
	b->port.fd = -1;

	ret = sysfs_find_dongle(&b->dongle);
	if (ret != KV3P_OK)
		return ret;

	ret = sysfs_interface_driver(b->dongle.sysname, KV3P_IF_COMM, driver, sizeof(driver));
	if (ret == KV3P_OK && strcmp(driver, KV3P_DRIVER_RAZER) == 0) {
		log_debug("borrowing %s from %s", b->dongle.ifid, driver);
		if (sysfs_driver_unbind(KV3P_DRIVER_RAZER, b->dongle.ifid) != KV3P_OK) {
			log_err("could not unbind %s from %s", b->dongle.ifid, KV3P_DRIVER_RAZER);
			return KV3P_BUSY;
		}
		b->rebind_razer = 1;
	} else if (ret == KV3P_OK && strcmp(driver, KV3P_DRIVER_CDC_ACM) != 0) {
		log_err("%s is held by '%s'; refusing to take it", b->dongle.ifid, driver);
		return KV3P_BUSY;
	}

	/* From here until borrow_release() the interface may be driverless: do not die. */
	block_signals(b);

	ret = usbfs_open(&b->port, b->dongle.node);
	if (ret != KV3P_OK) {
		borrow_release(b);
		return ret;
	}
	b->opened = 1;
	h4_reset(&b->parser);
	return KV3P_OK;
}

void borrow_release(struct borrow *b)
{
	if (b->opened) {
		usbfs_close(&b->port);
		b->opened = 0;
	}

	if (b->rebind_razer) {
		char driver[DRIVER_LEN];

		sysfs_driver_bind(KV3P_DRIVER_RAZER, b->dongle.ifid);
		if (sysfs_interface_driver(b->dongle.sysname, KV3P_IF_COMM, driver, sizeof(driver)) == KV3P_OK &&
		    strcmp(driver, KV3P_DRIVER_RAZER) == 0) {
			log_debug("%s handed back to %s", b->dongle.ifid, KV3P_DRIVER_RAZER);
		} else {
			log_warn("%s did not take %s back; asking the bus to re-probe",
				 KV3P_DRIVER_RAZER, b->dongle.ifid);
			sysfs_drivers_probe(b->dongle.ifid);
			if (sysfs_interface_driver(b->dongle.sysname, KV3P_IF_COMM, driver,
						   sizeof(driver)) != KV3P_OK)
				log_err("%s is left with no driver. Restore it with: "
					"echo %s | sudo tee /sys/bus/usb/drivers/%s/bind",
					b->dongle.ifid, b->dongle.ifid, KV3P_DRIVER_RAZER);
		}
		b->rebind_razer = 0;
	}

	unblock_signals(b);
}

int borrow_send(struct borrow *b, uint8_t cmd, int enable, struct kv3p_ack *ack)
{
	uint8_t frame[KV3P_FRAME_LEN];
	long long deadline;
	int ret;

	ret = gate_build(frame, cmd, enable);
	if (ret != KV3P_OK)
		return ret;

	ret = usbfs_write(&b->port, frame, sizeof(frame));
	if (ret != KV3P_OK)
		return ret;

	deadline = kv3p_now_ms() + ACK_TIMEOUT_MS;
	for (;;) {
		long long remaining = deadline - kv3p_now_ms();
		const uint8_t *pkt;
		uint8_t buf[READ_CHUNK];
		size_t len, got;

		if (remaining <= 0)
			break;
		if (!usbfs_poll(&b->port, (int)remaining))
			continue;

		ret = usbfs_read(&b->port, buf, sizeof(buf), &got);
		if (ret != KV3P_OK)
			return ret;
		if (!got)
			continue;

		h4_feed(&b->parser, buf, got);
		while (h4_next(&b->parser, &pkt, &len)) {
			struct kv3p_ack got_ack;

			if (frame_parse_ack(pkt, len, &got_ack) && got_ack.cmd == cmd) {
				*ack = got_ack;
				return KV3P_OK;
			}
		}
	}

	log_warn("switch_%s was written but not acked within %d ms", gate_name(cmd), ACK_TIMEOUT_MS);
	return KV3P_ERR;
}
