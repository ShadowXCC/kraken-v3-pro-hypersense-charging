/* SPDX-License-Identifier: MIT */
/*
 * Borrowing the control channel from the OpenRazer driver, and giving it straight back.
 *
 * The patched razerkraken's CDC half probes on interface 4 and claims interface 5 itself
 * (usb_driver_claim_interface), releasing the partner on disconnect -- so one unbind of
 * "<sysname>:1.4" frees both, and one bind brings both back. Its probe resets the driver's
 * gone/wedged/jammed flags, clears its cached link state and queues its refresh work
 * immediately, so it re-reads link and battery and re-applies lighting on its own. That is why
 * a borrow of about a second is safe for it.
 *
 * Leaving that interface driverless would silently kill Kraken support, so the restore is a hard
 * guarantee: one exit path through borrow_release(), with SIGTERM/SIGINT/SIGHUP/SIGQUIT blocked
 * for the duration. The daemon acts on signals between borrows, never inside one.
 */

#ifndef KV3P_BORROW_H
#define KV3P_BORROW_H

#include <signal.h>
#include <stdint.h>

#include "frame.h"
#include "sysfs.h"
#include "usbfs.h"

struct borrow {
	struct dongle dongle;
	struct usbfs_port port;
	struct h4_parser parser;
	int rebind_razer;
	int opened;
	int signals_blocked;
	sigset_t oldmask;
};

/* Take the channel: find the dongle, hand interface 4 over if razerkraken_cdc has it, open usbfs.
 * On failure everything already done is undone before returning. */
int borrow_acquire(struct borrow *b);

/* Send one gate command and wait for its ack. KV3P_OK fills `ack`. */
int borrow_send(struct borrow *b, uint8_t cmd, int enable, struct kv3p_ack *ack);

/* Always safe to call, including after a failed acquire. */
void borrow_release(struct borrow *b);

#endif /* KV3P_BORROW_H */
