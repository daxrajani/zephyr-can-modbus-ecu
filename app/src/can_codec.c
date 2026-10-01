/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include "can_codec.h"
#include "crc.h"

#define CRC_BYTE     7
#define COUNTER_BYTE 6

static void put_le16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static uint16_t get_le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static void seal(uint8_t data[8], uint8_t counter)
{
	data[COUNTER_BYTE] = counter & 0x0F;
	data[CRC_BYTE] = crc8_sae_j1850(data, CRC_BYTE);
}

static int check(const uint8_t *data, uint8_t dlc)
{
	if (dlc != CAN_CODEC_DLC) {
		return -EINVAL;
	}
	if (crc8_sae_j1850(data, CRC_BYTE) != data[CRC_BYTE]) {
		return -EBADMSG;
	}
	return 0;
}

void can_codec_pack_sensor_status(const struct sensor_status_msg *msg, uint8_t data[8])
{
	memset(data, 0, CAN_CODEC_DLC);
	put_le16(&data[0], (uint16_t)msg->temperature_dC);
	put_le16(&data[2], msg->vibration_cmm_s);
	data[4] = (msg->alarm_active ? 0x01 : 0) | (msg->sensor_fault ? 0x02 : 0);
	seal(data, msg->counter);
}

int can_codec_unpack_sensor_status(const uint8_t *data, uint8_t dlc, struct sensor_status_msg *msg)
{
	int err = check(data, dlc);

	if (err) {
		return err;
	}
	msg->temperature_dC = (int16_t)get_le16(&data[0]);
	msg->vibration_cmm_s = get_le16(&data[2]);
	msg->alarm_active = data[4] & 0x01;
	msg->sensor_fault = data[4] & 0x02;
	msg->counter = data[COUNTER_BYTE] & 0x0F;
	return 0;
}

/* HEARTBEAT is not safety relevant, so it carries no counter or CRC. */
void can_codec_pack_heartbeat(const struct heartbeat_msg *msg, uint8_t data[8])
{
	memset(data, 0, CAN_CODEC_DLC);
	put_le16(&data[0], (uint16_t)msg->uptime_s);
	put_le16(&data[2], (uint16_t)(msg->uptime_s >> 16));
	data[4] = msg->fw_state;
	data[5] = msg->active_dtcs;
}

int can_codec_unpack_heartbeat(const uint8_t *data, uint8_t dlc, struct heartbeat_msg *msg)
{
	if (dlc != CAN_CODEC_DLC) {
		return -EINVAL;
	}
	msg->uptime_s = get_le16(&data[0]) | ((uint32_t)get_le16(&data[2]) << 16);
	msg->fw_state = data[4];
	msg->active_dtcs = data[5];
	return 0;
}

void can_codec_pack_ecu_command(const struct ecu_command_msg *msg, uint8_t data[8])
{
	memset(data, 0, CAN_CODEC_DLC);
	put_le16(&data[0], msg->sample_period_ms);
	put_le16(&data[2], (uint16_t)msg->alarm_threshold_dC);
	data[4] = (msg->fault_injection ? 0x01 : 0) | (msg->alarm_reset ? 0x02 : 0);
	seal(data, msg->counter);
}

int can_codec_unpack_ecu_command(const uint8_t *data, uint8_t dlc, struct ecu_command_msg *msg)
{
	int err = check(data, dlc);

	if (err) {
		return err;
	}
	msg->sample_period_ms = get_le16(&data[0]);
	msg->alarm_threshold_dC = (int16_t)get_le16(&data[2]);
	msg->fault_injection = data[4] & 0x01;
	msg->alarm_reset = data[4] & 0x02;
	msg->counter = data[COUNTER_BYTE] & 0x0F;
	return 0;
}
