/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/ztest.h>

#include "can_codec.h"
#include "crc.h"
#include "ecu_config.h"

ZTEST(can_codec, test_sensor_status_layout_matches_dbc)
{
	struct sensor_status_msg msg = {
		.temperature_dC = -123, /* -12.3 degC -> 0xFF85 */
		.vibration_cmm_s = 0x1234,
		.alarm_active = true,
		.sensor_fault = false,
		.counter = 0x1F, /* only the low nibble is sent */
	};
	uint8_t d[8];

	can_codec_pack_sensor_status(&msg, d);

	zassert_equal(d[0], 0x85);
	zassert_equal(d[1], 0xFF);
	zassert_equal(d[2], 0x34);
	zassert_equal(d[3], 0x12);
	zassert_equal(d[4], 0x01);
	zassert_equal(d[5], 0x00);
	zassert_equal(d[6], 0x0F);
	zassert_equal(d[7], crc8_sae_j1850(d, 7));
}

ZTEST(can_codec, test_sensor_status_round_trip)
{
	struct sensor_status_msg in = {
		.temperature_dC = 1500,
		.vibration_cmm_s = 999,
		.alarm_active = true,
		.sensor_fault = true,
		.counter = 9,
	};
	struct sensor_status_msg out;
	uint8_t d[8];

	can_codec_pack_sensor_status(&in, d);
	zassert_ok(can_codec_unpack_sensor_status(d, 8, &out));
	zassert_equal(out.temperature_dC, in.temperature_dC);
	zassert_equal(out.vibration_cmm_s, in.vibration_cmm_s);
	zassert_true(out.alarm_active);
	zassert_true(out.sensor_fault);
	zassert_equal(out.counter, 9);
}

ZTEST(can_codec, test_heartbeat_round_trip)
{
	struct heartbeat_msg in = {.uptime_s = 0x01020304, .fw_state = 2, .active_dtcs = 3};
	struct heartbeat_msg out;
	uint8_t d[8];

	can_codec_pack_heartbeat(&in, d);
	zassert_equal(d[0], 0x04);
	zassert_equal(d[3], 0x01);
	zassert_ok(can_codec_unpack_heartbeat(d, 8, &out));
	zassert_equal(out.uptime_s, in.uptime_s);
	zassert_equal(out.fw_state, 2);
	zassert_equal(out.active_dtcs, 3);
}

ZTEST(can_codec, test_command_round_trip_and_sentinels)
{
	struct ecu_command_msg in = {
		.sample_period_ms = CMD_SAMPLE_PERIOD_KEEP,
		.alarm_threshold_dC = CMD_ALARM_THRESHOLD_KEEP,
		.fault_injection = true,
		.alarm_reset = true,
		.counter = 15,
	};
	struct ecu_command_msg out;
	uint8_t d[8];

	can_codec_pack_ecu_command(&in, d);
	zassert_ok(can_codec_unpack_ecu_command(d, 8, &out));
	zassert_equal(out.sample_period_ms, CMD_SAMPLE_PERIOD_KEEP);
	zassert_equal(out.alarm_threshold_dC, CMD_ALARM_THRESHOLD_KEEP);
	zassert_true(out.fault_injection);
	zassert_true(out.alarm_reset);
	zassert_equal(out.counter, 15);
}

ZTEST(can_codec, test_command_rejects_bad_crc)
{
	struct ecu_command_msg in = {.sample_period_ms = 50, .alarm_threshold_dC = 300};
	struct ecu_command_msg out;
	uint8_t d[8];

	can_codec_pack_ecu_command(&in, d);
	d[2] ^= 0x01;
	zassert_equal(can_codec_unpack_ecu_command(d, 8, &out), -EBADMSG);
}

ZTEST(can_codec, test_command_rejects_wrong_dlc)
{
	uint8_t d[8] = {0};
	struct ecu_command_msg out;

	zassert_equal(can_codec_unpack_ecu_command(d, 7, &out), -EINVAL);
	zassert_equal(can_codec_unpack_ecu_command(d, 0, &out), -EINVAL);
}

ZTEST_SUITE(can_codec, NULL, NULL, NULL, NULL, NULL);
