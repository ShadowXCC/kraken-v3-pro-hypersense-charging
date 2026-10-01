/* SPDX-License-Identifier: MIT */
/* The allowlist is the safety boundary: this service must not be able to emit anything else. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gate.h"
#include "kv3p.h"

static void test_only_the_two_gates_are_buildable(void)
{
	uint8_t frame[KV3P_FRAME_LEN];

	for (int cmd = 0; cmd <= 0xff; cmd++) {
		int ok = gate_build(frame, (uint8_t)cmd, 1) == KV3P_OK;

		if (cmd == GATE_CMD_HAPTIC || cmd == GATE_CMD_CHROMA)
			assert(ok);
		else
			assert(!ok); /* including 0x8a hypersense, and every flash/reboot command */
	}
}

static void test_enable_must_be_zero_or_one(void)
{
	uint8_t frame[KV3P_FRAME_LEN];

	assert(gate_build(frame, GATE_CMD_HAPTIC, 0) == KV3P_OK);
	assert(gate_build(frame, GATE_CMD_HAPTIC, 1) == KV3P_OK);
	assert(gate_build(frame, GATE_CMD_HAPTIC, 2) == KV3P_ERR);
	assert(gate_build(frame, GATE_CMD_HAPTIC, -1) == KV3P_ERR);
	assert(gate_build(frame, GATE_CMD_HAPTIC, 255) == KV3P_ERR);
}

static void test_argument_lands_on_byte_8(void)
{
	uint8_t frame[KV3P_FRAME_LEN];

	/* STM32 struct byte 1 is host frame byte 8 -- the mapping confirmed on hardware. */
	assert(gate_build(frame, GATE_CMD_HAPTIC, 1) == KV3P_OK);
	assert(frame[7] == GATE_CMD_HAPTIC);
	assert(frame[8] == 1);
	assert(gate_build(frame, GATE_CMD_HAPTIC, 0) == KV3P_OK);
	assert(frame[8] == 0);
}

static void test_names(void)
{
	assert(strcmp(gate_name(GATE_CMD_HAPTIC), "haptic") == 0);
	assert(strcmp(gate_name(GATE_CMD_CHROMA), "chroma") == 0);
	assert(strcmp(gate_name(0x8a), "?") == 0);
}

int main(void)
{
	test_only_the_two_gates_are_buildable();
	test_enable_must_be_zero_or_one();
	test_argument_lands_on_byte_8();
	test_names();
	printf("gate: ok\n");
	return 0;
}
