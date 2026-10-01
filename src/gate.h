/* SPDX-License-Identifier: MIT */
/*
 * The STM32's two master gates, decoded from the firmware with Ghidra on 2026-09-30 and verified
 * on hardware the same evening:
 *
 *   0x94 switch_haptic -> gate 0x200049ef (HyperSense)
 *   0x93 switch_chroma -> gate 0x200049ce (earcup LEDs)
 *
 * The NXH3670 sets both gates when the charger is connected; the STM32 then acks HyperSense and
 * lighting commands and discards them. Clearing the gate from the host lifts that, because the
 * dongle relays these two commands unfiltered.
 *
 * This file is the ONLY place in the service that turns a command byte into a frame, and it can
 * build nothing but 0x93 and 0x94. Everything else in the Razer command family -- including the
 * flash and reboot commands this protocol shares with the Barracuda platform -- is unreachable.
 */

#ifndef KV3P_GATE_H
#define KV3P_GATE_H

#include <stdint.h>

#include "frame.h"

#define GATE_CMD_HAPTIC 0x94
#define GATE_CMD_CHROMA 0x93

/* Build switch_haptic/switch_chroma. Refuses (KV3P_ERR) any cmd but 0x93/0x94 and any enable
 * but 0/1. Returns KV3P_OK on success. */
int gate_build(uint8_t out[KV3P_FRAME_LEN], uint8_t cmd, int enable);

/* "haptic"/"chroma" for logs. */
const char *gate_name(uint8_t cmd);

#endif /* KV3P_GATE_H */
