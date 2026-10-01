/*
 * CAN front end: periodic SENSOR_STATUS / HEARTBEAT transmission and
 * ECU_COMMAND reception with rolling counter and CRC checks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "can_codec.h"
#include "data_model.h"
#include "dtc.h"
#include "ecu_config.h"
#include "ecu_tasks.h"

LOG_MODULE_REGISTER(can_app, LOG_LEVEL_INF);

#define CAN_TX_STACK 1024
#define CAN_RX_STACK 1024
#define CAN_TX_PRIO  5
#define CAN_RX_PRIO  6

static const struct device *const can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

CAN_MSGQ_DEFINE(cmd_msgq, 8);

K_THREAD_STACK_DEFINE(can_tx_stack, CAN_TX_STACK);
K_THREAD_STACK_DEFINE(can_rx_stack, CAN_RX_STACK);
static struct k_thread can_tx_thread;
static struct k_thread can_rx_thread;

static int send_frame(uint32_t id, const uint8_t data[8])
{
	struct can_frame frame = {
		.id = id,
		.dlc = CAN_CODEC_DLC,
	};

	memcpy(frame.data, data, CAN_CODEC_DLC);
	return can_send(can_dev, &frame, K_MSEC(20), NULL, NULL);
}

static void can_tx_loop(void *p1, void *p2, void *p3)
{
	int64_t next = k_uptime_get();
	uint8_t counter = 0;
	uint32_t ticks = 0;

	while (true) {
		struct dm_snapshot s;
		uint8_t data[8];

		dm_get(&s);

		struct sensor_status_msg status = {
			.temperature_dC = s.temperature_dC,
			.vibration_cmm_s = s.vibration_cmm_s,
			.alarm_active = s.alarm_active,
			.sensor_fault = s.sensor_fault,
			.counter = counter,
		};

		can_codec_pack_sensor_status(&status, data);
		if (send_frame(CAN_ID_SENSOR_STATUS, data) != 0) {
			LOG_DBG("SENSOR_STATUS not sent");
		}
		counter = (counter + 1) & 0x0F;

		if (ticks % (HEARTBEAT_PERIOD_MS / SENSOR_STATUS_PERIOD_MS) == 0) {
			size_t active = dtc_count_by_mask(DTC_STATUS_TEST_FAILED);
			struct heartbeat_msg hb = {
				.uptime_s = s.uptime_s,
				.fw_state = active ? FW_STATE_DEGRADED : s.state,
				.active_dtcs = (uint8_t)active,
			};

			can_codec_pack_heartbeat(&hb, data);
			(void)send_frame(CAN_ID_HEARTBEAT, data);
		}
		ticks++;

		/* Absolute deadlines keep the period free of cumulative drift. */
		next += SENSOR_STATUS_PERIOD_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next));
	}
}

static void apply_command(const struct ecu_command_msg *cmd)
{
	if (cmd->sample_period_ms != CMD_SAMPLE_PERIOD_KEEP &&
	    dm_set_sample_period(cmd->sample_period_ms) != 0) {
		LOG_WRN("ECU_COMMAND: sample period %u out of range", cmd->sample_period_ms);
	}
	if (cmd->alarm_threshold_dC != CMD_ALARM_THRESHOLD_KEEP &&
	    dm_set_alarm_threshold(cmd->alarm_threshold_dC) != 0) {
		LOG_WRN("ECU_COMMAND: threshold %d out of range", cmd->alarm_threshold_dC);
	}
	dm_set_fault_injection(cmd->fault_injection);
	if (cmd->alarm_reset) {
		dm_reset_alarm();
	}

	LOG_INF("ECU_COMMAND applied: period=%u threshold=%d fault=%d reset=%d",
		cmd->sample_period_ms, cmd->alarm_threshold_dC, cmd->fault_injection,
		cmd->alarm_reset);
}

static void can_rx_loop(void *p1, void *p2, void *p3)
{
	struct can_frame frame;
	int last_counter = -1;

	while (true) {
		struct ecu_command_msg cmd;

		k_msgq_get(&cmd_msgq, &frame, K_FOREVER);

		int err = can_codec_unpack_ecu_command(frame.data, frame.dlc, &cmd);

		if (err == -EBADMSG) {
			LOG_WRN("ECU_COMMAND rejected: CRC error");
			dtc_report(DTC_COMMAND_CRC_ERROR, true);
			continue;
		}
		if (err) {
			LOG_WRN("ECU_COMMAND rejected: DLC %u", frame.dlc);
			continue;
		}
		dtc_report(DTC_COMMAND_CRC_ERROR, false);

		/* A repeated counter means a stale or replayed frame. */
		if (cmd.counter == last_counter) {
			LOG_WRN("ECU_COMMAND rejected: repeated counter %u", cmd.counter);
			continue;
		}
		last_counter = cmd.counter;

		apply_command(&cmd);
	}
}

static void can_state_changed(const struct device *dev, enum can_state state,
			      struct can_bus_err_cnt err_cnt, void *user_data)
{
	static bool was_bus_off;

	if (state == CAN_STATE_BUS_OFF) {
		was_bus_off = true;
		dtc_report(DTC_CAN_BUS_OFF, true);
	} else if (was_bus_off && state == CAN_STATE_ERROR_ACTIVE) {
		/* Recovered: testFailed clears, confirmed stays for the tester. */
		was_bus_off = false;
		dtc_report(DTC_CAN_BUS_OFF, false);
	}
}

const struct device *can_app_device(void)
{
	return can_dev;
}

int can_app_start(void)
{
	const struct can_filter cmd_filter = {
		.id = CAN_ID_ECU_COMMAND,
		.mask = CAN_STD_ID_MASK,
		.flags = 0,
	};
	int err;

	if (!device_is_ready(can_dev)) {
		LOG_ERR("CAN device %s not ready", can_dev->name);
		return -ENODEV;
	}

	err = can_set_bitrate(can_dev, CAN_BITRATE);
	if (err && err != -ENOTSUP && err != -ENOSYS) {
		LOG_WRN("can_set_bitrate: %d", err);
	}

	can_set_state_change_callback(can_dev, can_state_changed, NULL);

	err = can_add_rx_filter_msgq(can_dev, &cmd_msgq, &cmd_filter);
	if (err < 0) {
		LOG_ERR("ECU_COMMAND filter: %d", err);
		return err;
	}

	err = can_start(can_dev);
	if (err && err != -EALREADY) {
		LOG_ERR("can_start: %d", err);
		return err;
	}

	k_thread_create(&can_tx_thread, can_tx_stack, K_THREAD_STACK_SIZEOF(can_tx_stack),
			can_tx_loop, NULL, NULL, NULL, CAN_TX_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&can_tx_thread, "can_tx");
	k_thread_create(&can_rx_thread, can_rx_stack, K_THREAD_STACK_SIZEOF(can_rx_stack),
			can_rx_loop, NULL, NULL, NULL, CAN_RX_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&can_rx_thread, "can_rx");

	LOG_INF("CAN up on %s, %u bit/s", can_dev->name, CAN_BITRATE);
	return 0;
}
