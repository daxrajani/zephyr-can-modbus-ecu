/*
 * Pack and unpack the project CAN messages. The bit layout here must match
 * dbc/sensor_ecu.dbc; the integration tests decode live frames with cantools
 * to prove that it does.
 *
 * All multi-byte signals are little endian (Intel). Byte 6 carries a 4-bit
 * rolling counter and byte 7 a CRC-8 SAE J1850 over bytes 0..6.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CAN_CODEC_H_
#define CAN_CODEC_H_

#include <stdbool.h>
#include <stdint.h>

#define CAN_CODEC_DLC 8

struct sensor_status_msg {
	int16_t temperature_dC;   /* 0.1 degC */
	uint16_t vibration_cmm_s; /* 0.01 mm/s */
	bool alarm_active;
	bool sensor_fault;
	uint8_t counter;          /* 0..15 */
};

struct heartbeat_msg {
	uint32_t uptime_s;
	uint8_t fw_state;
	uint8_t active_dtcs;
};

struct ecu_command_msg {
	uint16_t sample_period_ms; /* CMD_SAMPLE_PERIOD_KEEP = no change */
	int16_t alarm_threshold_dC; /* CMD_ALARM_THRESHOLD_KEEP = no change */
	bool fault_injection;
	bool alarm_reset;
	uint8_t counter;          /* 0..15 */
};

void can_codec_pack_sensor_status(const struct sensor_status_msg *msg, uint8_t data[8]);
int can_codec_unpack_sensor_status(const uint8_t *data, uint8_t dlc,
				   struct sensor_status_msg *msg);

void can_codec_pack_heartbeat(const struct heartbeat_msg *msg, uint8_t data[8]);
int can_codec_unpack_heartbeat(const uint8_t *data, uint8_t dlc, struct heartbeat_msg *msg);

void can_codec_pack_ecu_command(const struct ecu_command_msg *msg, uint8_t data[8]);
/* Returns 0, -EINVAL for a wrong DLC or -EBADMSG for a CRC mismatch. */
int can_codec_unpack_ecu_command(const uint8_t *data, uint8_t dlc, struct ecu_command_msg *msg);

#endif /* CAN_CODEC_H_ */
