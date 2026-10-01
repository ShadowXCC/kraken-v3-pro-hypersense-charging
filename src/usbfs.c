/* SPDX-License-Identifier: MIT */
#include "usbfs.h"

#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "kv3p.h"
#include "log.h"

/* CDC ACM class requests (USB CDC PSTN subclass 1.2, section 6.3). */
#define REQTYPE_CLASS_IF_OUT 0x21
#define SET_LINE_CODING 0x20
#define SET_CONTROL_LINE_STATE 0x22
#define LINE_DTR 0x01
#define LINE_RTS 0x02

#define WRITE_TIMEOUT_MS 1000
#define CONTROL_TIMEOUT_MS 1000
#define MAX_URB_ERRORS 20

/* 9600 8N1: dwDTERate LE, 1 stop bit, no parity, 8 data bits. */
static const uint8_t line_coding_9600_8n1[7] = { 0x80, 0x25, 0x00, 0x00, 0x00, 0x00, 0x08 };

/* usbfs_read casts a reaped URB back to its slot, so the URB must sit at offset 0. */
_Static_assert(offsetof(struct usbfs_urb_slot, urb) == 0, "urb must be first in usbfs_urb_slot");

static int gone_errno(int err)
{
	return err == ENODEV || err == ESHUTDOWN || err == EPIPE;
}

int usbfs_getdriver(int fd, int ifno, char *name, size_t n)
{
	struct usbdevfs_getdriver gd;

	memset(&gd, 0, sizeof(gd));
	gd.interface = (unsigned int)ifno;
	if (ioctl(fd, USBDEVFS_GETDRIVER, &gd) < 0)
		return (errno == ENODATA || errno == ENOENT) ? KV3P_MISSING : KV3P_ERR;
	snprintf(name, n, "%s", gd.driver);
	return KV3P_OK;
}

static int control(struct usbfs_port *p, uint8_t request, uint16_t value, const void *data, uint16_t len)
{
	struct usbdevfs_ctrltransfer ct;

	memset(&ct, 0, sizeof(ct));
	ct.bRequestType = REQTYPE_CLASS_IF_OUT;
	ct.bRequest = request;
	ct.wValue = value;
	ct.wIndex = KV3P_IF_COMM;
	ct.wLength = len;
	ct.timeout = CONTROL_TIMEOUT_MS;
	ct.data = (void *)(uintptr_t)data;

	if (ioctl(p->fd, USBDEVFS_CONTROL, &ct) < 0)
		return gone_errno(errno) ? KV3P_GONE : KV3P_ERR;
	return KV3P_OK;
}

static int disconnect_claim(struct usbfs_port *p, int ifno)
{
	struct usbdevfs_disconnect_claim dc;

	memset(&dc, 0, sizeof(dc));
	dc.interface = (unsigned int)ifno;
	dc.flags = USBDEVFS_DISCONNECT_CLAIM_IF_DRIVER;
	snprintf(dc.driver, sizeof(dc.driver), "%s", KV3P_DRIVER_CDC_ACM);

	if (ioctl(p->fd, USBDEVFS_DISCONNECT_CLAIM, &dc) < 0) {
		if (errno == EBUSY)
			return KV3P_BUSY;
		return gone_errno(errno) ? KV3P_GONE : KV3P_ERR;
	}
	p->claimed[p->nclaimed++] = ifno;
	return KV3P_OK;
}

static int submit(struct usbfs_port *p, struct usbfs_urb_slot *slot)
{
	slot->urb.status = 0;
	slot->urb.actual_length = 0;
	if (ioctl(p->fd, USBDEVFS_SUBMITURB, &slot->urb) < 0)
		return gone_errno(errno) ? KV3P_GONE : KV3P_ERR;
	return KV3P_OK;
}

static int queue_urbs(struct usbfs_port *p)
{
	size_t i;

	p->nslots = 0;
	for (i = 0; i < USBFS_IN_URBS + 1; i++) {
		struct usbfs_urb_slot *slot = &p->slots[i];
		int is_notify = (i == USBFS_IN_URBS);
		int ret;

		memset(&slot->urb, 0, sizeof(slot->urb));
		slot->urb.type = is_notify ? USBDEVFS_URB_TYPE_INTERRUPT : USBDEVFS_URB_TYPE_BULK;
		slot->urb.endpoint = is_notify ? KV3P_EP_NOTIFY : KV3P_EP_IN;
		slot->urb.buffer = slot->buf;
		slot->urb.buffer_length = is_notify ? USBFS_NOTIFY_LEN : USBFS_IN_LEN;

		ret = submit(p, slot);
		if (ret != KV3P_OK)
			return ret;
		p->nslots++;
	}
	return KV3P_OK;
}

int usbfs_open(struct usbfs_port *p, const char *node)
{
	static const int ifaces[2] = { KV3P_IF_COMM, KV3P_IF_DATA };
	char driver[64];
	int ret;

	memset(p, 0, sizeof(*p));
	p->fd = open(node, O_RDWR | O_CLOEXEC);
	if (p->fd < 0) {
		log_debug("open %s: %s", node, strerror(errno));
		return errno == ENOENT ? KV3P_MISSING : KV3P_ERR;
	}

	/* Refuse to displace anything but cdc_acm. razerkraken_cdc is handed over explicitly by
	 * borrow.c, which also puts it back; nothing else may be taken at all. */
	for (size_t i = 0; i < 2; i++) {
		ret = usbfs_getdriver(p->fd, ifaces[i], driver, sizeof(driver));
		if (ret == KV3P_OK && strcmp(driver, KV3P_DRIVER_CDC_ACM) != 0) {
			log_err("interface %d is held by '%s'; not touching it", ifaces[i], driver);
			ret = KV3P_BUSY;
			goto fail;
		}
		if (ret == KV3P_ERR)
			goto fail;
		if (ifaces[i] == KV3P_IF_COMM)
			p->was_cdc_acm = (ret == KV3P_OK);
	}

	for (size_t i = 0; i < 2; i++) {
		ret = disconnect_claim(p, ifaces[i]);
		if (ret != KV3P_OK)
			goto fail;
	}

	ret = control(p, SET_LINE_CODING, 0, line_coding_9600_8n1, sizeof(line_coding_9600_8n1));
	if (ret != KV3P_OK)
		goto fail;
	ret = control(p, SET_CONTROL_LINE_STATE, LINE_DTR | LINE_RTS, NULL, 0);
	if (ret != KV3P_OK)
		goto fail;

	ret = queue_urbs(p);
	if (ret != KV3P_OK)
		goto fail;

	return KV3P_OK;

fail:
	usbfs_close(p);
	return ret;
}

void usbfs_close(struct usbfs_port *p)
{
	/* fd 0 means "never opened": a zeroed port must not close someone else's stdin. */
	if (p->fd <= 0)
		return;

	if (p->nclaimed) {
		control(p, SET_CONTROL_LINE_STATE, 0, NULL, 0); /* best effort */
		while (p->nclaimed) {
			unsigned int ifno = (unsigned int)p->claimed[--p->nclaimed];

			if (ioctl(p->fd, USBDEVFS_RELEASEINTERFACE, &ifno) < 0)
				log_debug("release interface %u: %s", ifno, strerror(errno));
		}
	}

	if (p->was_cdc_acm) {
		struct usbdevfs_ioctl cmd;

		memset(&cmd, 0, sizeof(cmd));
		cmd.ifno = KV3P_IF_COMM;
		cmd.ioctl_code = USBDEVFS_CONNECT;
		if (ioctl(p->fd, USBDEVFS_IOCTL, &cmd) < 0)
			log_debug("handing interface %d back to cdc_acm: %s", KV3P_IF_COMM, strerror(errno));
	}

	close(p->fd); /* usbfs kills anything still queued before this returns */
	p->fd = -1;
	p->nslots = 0;
}

int usbfs_write(struct usbfs_port *p, const uint8_t *frame, size_t n)
{
	struct usbdevfs_bulktransfer bt;
	int wrote;

	if (p->wedged)
		return KV3P_WEDGED;

	memset(&bt, 0, sizeof(bt));
	bt.ep = KV3P_EP_OUT;
	bt.len = (unsigned int)n;
	bt.timeout = WRITE_TIMEOUT_MS;
	bt.data = (void *)(uintptr_t)frame;

	wrote = ioctl(p->fd, USBDEVFS_BULK, &bt);
	if (wrote < 0) {
		if (errno == ETIMEDOUT) {
			p->wedged = 1;
			return KV3P_WEDGED;
		}
		return gone_errno(errno) ? KV3P_GONE : KV3P_ERR;
	}
	if ((size_t)wrote != n) {
		log_err("short write: %d of %zu bytes", wrote, n);
		return KV3P_ERR;
	}
	return KV3P_OK;
}

int usbfs_poll(struct usbfs_port *p, int timeout_ms)
{
	struct pollfd pfd = { .fd = p->fd, .events = POLLOUT | POLLWRNORM };

	return poll(&pfd, 1, timeout_ms) > 0 ? 1 : 0;
}

int usbfs_read(struct usbfs_port *p, uint8_t *out, size_t outlen, size_t *got)
{
	*got = 0;

	for (;;) {
		struct usbdevfs_urb *done = NULL;
		struct usbfs_urb_slot *slot;
		int ret;

		if (ioctl(p->fd, USBDEVFS_REAPURBNDELAY, &done) < 0) {
			if (errno == EAGAIN)
				return KV3P_OK;
			return gone_errno(errno) ? KV3P_GONE : KV3P_ERR;
		}

		slot = (struct usbfs_urb_slot *)done;
		if (done->status == 0 && done->endpoint == KV3P_EP_IN) {
			size_t n = (size_t)done->actual_length;

			p->urb_errors = 0;
			if (n > outlen - *got)
				n = outlen - *got;
			memcpy(out + *got, slot->buf, n);
			*got += n;
		} else if (done->status != 0) {
			if (gone_errno(-done->status))
				return KV3P_GONE;
			if (++p->urb_errors > MAX_URB_ERRORS) {
				log_err("bulk IN keeps failing (last status %d)", done->status);
				return KV3P_ERR;
			}
		}
		/* the notify URB's own completions carry no payload we need: requeue and move on */

		ret = submit(p, slot);
		if (ret != KV3P_OK)
			return ret;
	}
}
