/* SPDX-License-Identifier: MIT */
#include "frame.h"

#include <string.h>

#include "kv3p.h"

#define H4_CMD 0x01
#define H4_ACL 0x02
#define H4_EVT 0x04
#define EVT_CMD_COMPLETE 0x0e
#define OPCODE_RAZER 0xfff3

static const uint8_t header[4] = { H4_CMD, OPCODE_RAZER & 0xff, OPCODE_RAZER >> 8, KV3P_FRAME_LEN - 4 };
static const uint8_t write_prefix[3] = { 0x02, 0x21, 0x40 };

uint8_t frame_checksum(const uint8_t *body, size_t n)
{
	unsigned sum = 0;

	for (size_t i = 0; i < n; i++)
		sum += body[i];
	return (uint8_t)(-sum & 0xff);
}

int frame_assemble(uint8_t out[KV3P_FRAME_LEN], uint8_t cmd, const uint8_t *args, size_t nargs)
{
	if (nargs > KV3P_ARG_LAST - KV3P_ARG_FIRST + 1)
		return KV3P_ERR;

	memset(out, 0, KV3P_FRAME_LEN);
	memcpy(out, header, sizeof(header));
	memcpy(out + 4, write_prefix, sizeof(write_prefix));
	out[7] = cmd;
	if (nargs)
		memcpy(out + KV3P_ARG_FIRST, args, nargs);
	out[KV3P_FRAME_LEN - 1] = frame_checksum(out + 7, KV3P_FRAME_LEN - 8);
	return KV3P_OK;
}

void h4_reset(struct h4_parser *p)
{
	p->len = 0;
}

void h4_feed(struct h4_parser *p, const uint8_t *data, size_t n)
{
	size_t room = sizeof(p->buf) - p->len;

	if (n > room) {
		/* The dongle only ever sends short packets; a full buffer means we lost sync.
		 * Start again from the newest bytes rather than growing or wedging. */
		h4_reset(p);
		if (n > sizeof(p->buf)) {
			data += n - sizeof(p->buf);
			n = sizeof(p->buf);
		}
	}
	memcpy(p->buf + p->len, data, n);
	p->len += n;
}

static void consume(struct h4_parser *p, size_t n)
{
	memmove(p->buf, p->buf + n, p->len - n);
	p->len -= n;
}

int h4_next(struct h4_parser *p, const uint8_t **pkt, size_t *pkt_len)
{
	while (p->len) {
		size_t need = 0;

		switch (p->buf[0]) {
		case H4_EVT:
			if (p->len < 3)
				return 0;
			need = 3 + p->buf[2];
			break;
		case H4_CMD:
			if (p->len < 4)
				return 0;
			need = 4 + p->buf[3];
			break;
		case H4_ACL:
			if (p->len < 5)
				return 0;
			need = 5 + ((size_t)p->buf[3] | ((size_t)p->buf[4] << 8));
			break;
		default: {
			/* Not a packet boundary: drop up to the next plausible one. */
			size_t i = 1;

			while (i < p->len && p->buf[i] != H4_CMD && p->buf[i] != H4_ACL && p->buf[i] != H4_EVT)
				i++;
			consume(p, i);
			continue;
		}
		}

		if (need > sizeof(p->buf)) { /* impossible length: resync */
			consume(p, 1);
			continue;
		}
		if (p->len < need)
			return 0;

		/* Copy it out before consume() shifts the buffer under the caller's feet. */
		memcpy(p->pkt, p->buf, need);
		consume(p, need);
		*pkt = p->pkt;
		*pkt_len = need;
		return 1;
	}
	return 0;
}

int frame_parse_ack(const uint8_t *pkt, size_t n, struct kv3p_ack *out)
{
	const uint8_t *params, *payload;
	size_t nparams, npayload;
	uint16_t opcode;

	if (n < 11 || pkt[0] != H4_EVT || pkt[1] != EVT_CMD_COMPLETE)
		return 0;

	params = pkt + 3;
	nparams = n - 3;
	if (nparams < 5)
		return 0;
	opcode = (uint16_t)(params[1] | ((uint16_t)params[2] << 8));
	if (opcode != OPCODE_RAZER)
		return 0;

	payload = params + 4;
	npayload = nparams - 4;
	if (npayload < 4 || memcmp(payload, write_prefix, sizeof(write_prefix)) != 0)
		return 0;

	out->cmd = payload[3];
	out->status = params[3];
	return 1;
}

void frame_hex(const uint8_t *frame, size_t n, char *out, size_t outlen)
{
	static const char digits[] = "0123456789abcdef";
	size_t w = 0;

	for (size_t i = 0; i < n && w + 4 <= outlen; i++) {
		if (i)
			out[w++] = ' ';
		out[w++] = digits[frame[i] >> 4];
		out[w++] = digits[frame[i] & 0x0f];
	}
	out[w < outlen ? w : outlen - 1] = '\0';
}
