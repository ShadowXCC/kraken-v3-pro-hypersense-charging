/* SPDX-License-Identifier: MIT */
/*
 * H4 framing for the dongle's CDC channel. Ported from the hardware-verified
 * kv3p/frame.py ("Adding to Open Razer/03-library"), structure only, no Razer semantics.
 *
 *   host->dongle, 28 bytes:  01 f3 ff 18 | prefix(3) | cmd | args(8..26) | CS
 *   dongle->host:            04 0e LEN 01 f3 ff STATUS | prefix(3) cmd args... CS
 *
 * CS makes sum(bytes 7..27) == 0 mod 256.
 */

#ifndef KV3P_FRAME_H
#define KV3P_FRAME_H

#include <stddef.h>
#include <stdint.h>

#define KV3P_FRAME_LEN 28
#define KV3P_ARG_FIRST 8 /* first argument byte; STM32 struct byte N is host frame byte N+7 */
#define KV3P_ARG_LAST 26

/* Build a host->dongle frame: `args` land at bytes 8.., the checksum at byte 27.
 * Returns KV3P_OK, or KV3P_ERR if nargs does not fit. */
int frame_assemble(uint8_t out[KV3P_FRAME_LEN], uint8_t cmd, const uint8_t *args, size_t nargs);

/* The CS byte that makes sum(body) + CS == 0 mod 256. */
uint8_t frame_checksum(const uint8_t *body, size_t n);

/* Reassembles H4 packets from the byte stream coming off bulk IN. */
#define H4_BUF_LEN 1024

struct h4_parser {
	uint8_t buf[H4_BUF_LEN]; /* the byte stream, consumed packet by packet */
	size_t len;
	uint8_t pkt[H4_BUF_LEN]; /* the packet h4_next last handed out, stable until the next call */
};

void h4_reset(struct h4_parser *p);
/* Append bytes. Overlong garbage is dropped rather than growing without bound. */
void h4_feed(struct h4_parser *p, const uint8_t *data, size_t n);
/* Pop one complete packet. `pkt` points at parser-owned storage that stays valid until the
 * next h4_next()/h4_feed() call. Returns 1 when a packet was ready, 0 when more bytes are needed. */
int h4_next(struct h4_parser *p, const uint8_t **pkt, size_t *pkt_len);

struct kv3p_ack {
	uint8_t cmd;
	uint8_t status;
};

/* 1 if `pkt` is a Command Complete for a 02 21 40 settings write, with its cmd and HCI status. */
int frame_parse_ack(const uint8_t *pkt, size_t n, struct kv3p_ack *out);

/* "01 f3 ff 18 ..." as a hex string, for logs and --dry-run. `out` needs 3*len bytes. */
void frame_hex(const uint8_t *frame, size_t n, char *out, size_t outlen);

#endif /* KV3P_FRAME_H */
