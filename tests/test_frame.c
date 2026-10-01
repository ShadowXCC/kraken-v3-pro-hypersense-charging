/* SPDX-License-Identifier: MIT */
/* Golden frames come from the hardware-verified record in tier3a-switch-haptic-test.md. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "frame.h"
#include "gate.h"
#include "kv3p.h"

static const uint8_t golden_haptic_on[KV3P_FRAME_LEN] = {
	0x01, 0xf3, 0xff, 0x18, 0x02, 0x21, 0x40, 0x94, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6b
};
static const uint8_t golden_haptic_off[KV3P_FRAME_LEN] = {
	0x01, 0xf3, 0xff, 0x18, 0x02, 0x21, 0x40, 0x94, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6c
};
static const uint8_t golden_chroma_on[KV3P_FRAME_LEN] = {
	0x01, 0xf3, 0xff, 0x18, 0x02, 0x21, 0x40, 0x93, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6c
};

static void test_golden_frames(void)
{
	uint8_t frame[KV3P_FRAME_LEN];

	assert(gate_build(frame, GATE_CMD_HAPTIC, 1) == KV3P_OK);
	assert(memcmp(frame, golden_haptic_on, sizeof(frame)) == 0);

	assert(gate_build(frame, GATE_CMD_HAPTIC, 0) == KV3P_OK);
	assert(memcmp(frame, golden_haptic_off, sizeof(frame)) == 0);

	assert(gate_build(frame, GATE_CMD_CHROMA, 1) == KV3P_OK);
	assert(memcmp(frame, golden_chroma_on, sizeof(frame)) == 0);
}

static void test_checksum_rule(void)
{
	uint8_t frame[KV3P_FRAME_LEN];
	unsigned sum = 0;

	assert(gate_build(frame, GATE_CMD_HAPTIC, 1) == KV3P_OK);
	for (int i = 7; i < KV3P_FRAME_LEN; i++)
		sum += frame[i];
	assert((sum & 0xff) == 0); /* sum(bytes 7..CS) == 0 mod 256 */
}

static void test_arg_bounds(void)
{
	uint8_t frame[KV3P_FRAME_LEN];
	uint8_t args[32] = { 0 };

	assert(frame_assemble(frame, 0x8a, args, 19) == KV3P_OK);  /* bytes 8..26 */
	assert(frame_assemble(frame, 0x8a, args, 20) == KV3P_ERR); /* one too many */
}

/* 04 0e LEN | 01 f3 ff STATUS | 02 21 40 CMD 01 */
static const uint8_t ack_pkt[] = {
	0x04, 0x0e, 0x09, 0x01, 0xf3, 0xff, 0x00, 0x02, 0x21, 0x40, 0x94, 0x01
};

static void test_parser_reassembles_split_packets(void)
{
	struct h4_parser p;
	const uint8_t *pkt;
	size_t len;
	struct kv3p_ack ack;

	h4_reset(&p);
	h4_feed(&p, ack_pkt, 5);
	assert(h4_next(&p, &pkt, &len) == 0); /* not yet complete */
	h4_feed(&p, ack_pkt + 5, sizeof(ack_pkt) - 5);
	assert(h4_next(&p, &pkt, &len) == 1);
	assert(len == sizeof(ack_pkt));
	assert(memcmp(pkt, ack_pkt, len) == 0);
	assert(h4_next(&p, &pkt, &len) == 0);

	assert(frame_parse_ack(pkt, len, &ack) == 1);
	assert(ack.cmd == GATE_CMD_HAPTIC);
	assert(ack.status == 0);
}

static void test_parser_survives_junk(void)
{
	static const uint8_t junk[] = { 0xaa, 0xbb, 0xcc };
	struct h4_parser p;
	const uint8_t *pkt;
	size_t len;

	h4_reset(&p);
	h4_feed(&p, junk, sizeof(junk));
	h4_feed(&p, ack_pkt, sizeof(ack_pkt));
	assert(h4_next(&p, &pkt, &len) == 1);
	assert(len == sizeof(ack_pkt) && memcmp(pkt, ack_pkt, len) == 0);
}

static void test_parser_handles_two_packets_in_one_read(void)
{
	uint8_t both[2 * sizeof(ack_pkt)];
	struct h4_parser p;
	const uint8_t *pkt;
	size_t len;
	int seen = 0;

	memcpy(both, ack_pkt, sizeof(ack_pkt));
	memcpy(both + sizeof(ack_pkt), ack_pkt, sizeof(ack_pkt));
	h4_reset(&p);
	h4_feed(&p, both, sizeof(both));
	while (h4_next(&p, &pkt, &len)) {
		assert(len == sizeof(ack_pkt));
		assert(memcmp(pkt, ack_pkt, len) == 0); /* must survive the buffer shifting */
		seen++;
	}
	assert(seen == 2);
}

static void test_not_an_ack(void)
{
	/* A battery notification (prefix 02 20 24) is not a settings ack. */
	static const uint8_t battery[] = {
		0x04, 0x0e, 0x0b, 0x01, 0xf3, 0xff, 0x00, 0x02, 0x20, 0x24, 0x05, 0x0f, 0x3c, 0x64
	};
	struct kv3p_ack ack;

	assert(frame_parse_ack(battery, sizeof(battery), &ack) == 0);
	assert(frame_parse_ack(ack_pkt, 4, &ack) == 0); /* truncated */
}

static void test_hex(void)
{
	uint8_t frame[KV3P_FRAME_LEN];
	char hex[3 * KV3P_FRAME_LEN + 1];

	assert(gate_build(frame, GATE_CMD_HAPTIC, 1) == KV3P_OK);
	frame_hex(frame, sizeof(frame), hex, sizeof(hex));
	assert(strncmp(hex, "01 f3 ff 18 02 21 40 94 01", 26) == 0);
	assert(strlen(hex) == 3 * KV3P_FRAME_LEN - 1);
}

int main(void)
{
	test_golden_frames();
	test_checksum_rule();
	test_arg_bounds();
	test_parser_reassembles_split_packets();
	test_parser_survives_junk();
	test_parser_handles_two_packets_in_one_read();
	test_not_an_ack();
	test_hex();
	printf("frame: ok\n");
	return 0;
}
