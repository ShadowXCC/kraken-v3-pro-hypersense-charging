/* SPDX-License-Identifier: MIT */
/*
 * Shared constants and result codes for the Kraken V3 Pro HyperSense-while-charging service.
 *
 * Device identity is fixed at compile time on purpose: only the HyperSpeed dongle 1532:052c is
 * ever opened. The headset's own USB-C VCOM (1532:052d) must never be written to -- it speaks a
 * Barracuda-family protocol whose command space includes flash erase/write/reboot.
 */

#ifndef KV3P_H
#define KV3P_H

#define KV3P_VERSION "0.1.0"

#define KV3P_VID 0x1532
#define KV3P_PID 0x052c

/* The dongle's CDC-ACM interfaces: 4 is comm (control + notifications), 5 is data. */
#define KV3P_IF_COMM 4
#define KV3P_IF_DATA 5

/* Endpoints on those interfaces. */
#define KV3P_EP_OUT 0x06
#define KV3P_EP_IN 0x86
#define KV3P_EP_NOTIFY 0x85

/* The kernel driver that owns the channel when OpenRazer's patched razerkraken is installed. */
#define KV3P_DRIVER_RAZER "razerkraken_cdc"
#define KV3P_DRIVER_CDC_ACM "cdc_acm"

/* Result codes. Everything that can fail returns one of these. */
enum kv3p_result {
	KV3P_OK = 0,
	KV3P_ERR = -1,     /* unspecified failure, errno may be set */
	KV3P_MISSING = -2, /* no dongle on USB */
	KV3P_BUSY = -3,    /* an interface is held by someone we must not displace */
	KV3P_GONE = -4,    /* the device went away mid-operation */
	KV3P_WEDGED = -5,  /* the dongle stopped accepting data; it needs a replug */
};

const char *kv3p_strresult(int result);

/* CLOCK_MONOTONIC: stops while the machine is suspended. */
long long kv3p_now_ms(void);
/* CLOCK_BOOTTIME: keeps counting across suspend, so the difference reveals a resume. */
long long kv3p_boottime_ms(void);

#endif /* KV3P_H */
