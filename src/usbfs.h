/* SPDX-License-Identifier: MIT */
/*
 * The dongle's CDC channel through usbfs: its bulk endpoints with no tty in between.
 *
 * Ported line for line from kv3p/usbfs.py in "Adding to Open Razer/03-library", which is the
 * code that actually cleared the gate on this hardware on 2026-09-30. The sequence and its
 * reasons, all of them learned the hard way:
 *
 *  1. open /dev/bus/usb/BBB/DDD for the 1532:052c dongle;
 *  2. take interfaces 4 and 5 with USBDEVFS_DISCONNECT_CLAIM, restricted to cdc_acm -- so an
 *     interface held by any other driver yields EBUSY instead of being stolen;
 *  3. SET_LINE_CODING 9600 8N1 and SET_CONTROL_LINE_STATE DTR|RTS on interface 4. cdc_acm's own
 *     pair. 115200 with RTS set silently dropped every frame after the first (Session D T3);
 *  4. keep an interrupt-IN URB queued on 0x85, as cdc_acm and Windows' usbser do: the dongle posts
 *     a CDC SERIAL_STATE there at every DTR change, and without it a second frame in the same
 *     session was accepted at USB level and then never acted on. This service sends two;
 *  5. keep two bulk-IN URBs queued on 0x86 so notifications are always read;
 *  6. write with a synchronous bulk OUT to 0x06 that must complete within a second. A timeout means
 *     the dongle is wedged -- a known fault of this unit -- and nothing more is ever written.
 *
 * On close: DTR/RTS drop, the interfaces are released, and cdc_acm gets interface 4 back if it had
 * it. With no tty there is no line discipline and none of the tty echo that jammed the dongle once.
 */

#ifndef KV3P_USBFS_H
#define KV3P_USBFS_H

#include <linux/usbdevice_fs.h>
#include <stddef.h>
#include <stdint.h>

#define USBFS_IN_URBS 2
#define USBFS_IN_LEN 512
#define USBFS_NOTIFY_LEN 16

struct usbfs_urb_slot {
	struct usbdevfs_urb urb;
	uint8_t buf[USBFS_IN_LEN];
};

struct usbfs_port {
	int fd;
	int claimed[2];
	size_t nclaimed;
	int was_cdc_acm;
	int wedged;
	int urb_errors;
	struct usbfs_urb_slot slots[USBFS_IN_URBS + 1];
	size_t nslots;
};

/* KV3P_OK, KV3P_BUSY when an interface is held by a driver other than cdc_acm, KV3P_MISSING
 * when the node is gone, KV3P_ERR otherwise. */
int usbfs_open(struct usbfs_port *p, const char *node);
void usbfs_close(struct usbfs_port *p);

/* The driver bound to `ifno`, straight from the kernel. KV3P_MISSING when there is none. */
int usbfs_getdriver(int fd, int ifno, char *name, size_t n);

/* Synchronous bulk OUT. KV3P_WEDGED latches: after it, nothing is ever written again. */
int usbfs_write(struct usbfs_port *p, const uint8_t *frame, size_t n);

/* 1 when a queued URB has completed (or the device went away), 0 on timeout. */
int usbfs_poll(struct usbfs_port *p, int timeout_ms);

/* Reap every completed bulk-IN URB, requeue it, and return the bytes they carried. */
int usbfs_read(struct usbfs_port *p, uint8_t *out, size_t outlen, size_t *got);

#endif /* KV3P_USBFS_H */
