/* SPDX-License-Identifier: MIT */
#include "gate.h"

#include "kv3p.h"

int gate_build(uint8_t out[KV3P_FRAME_LEN], uint8_t cmd, int enable)
{
	uint8_t arg;

	/* The allowlist. Nothing else in the command family is reachable from this service. */
	if (cmd != GATE_CMD_HAPTIC && cmd != GATE_CMD_CHROMA)
		return KV3P_ERR;
	if (enable != 0 && enable != 1)
		return KV3P_ERR;

	arg = (uint8_t)enable;
	return frame_assemble(out, cmd, &arg, 1);
}

const char *gate_name(uint8_t cmd)
{
	switch (cmd) {
	case GATE_CMD_HAPTIC:
		return "haptic";
	case GATE_CMD_CHROMA:
		return "chroma";
	default:
		return "?";
	}
}
