/*
 * Peer node: a second ECU on the same CAN bus that exercises the sensor
 * ECU the way a gateway or body controller would. It runs a fixed check
 * sequence and prints one line per step, which the Robot Framework suite
 * in renode/ecu.robot waits for.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/canbus/isotp.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "can_codec.h"
#include "ecu_config.h"

static const struct device *const can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

CAN_MSGQ_DEFINE(rx_msgq, 16);

static const struct isotp_fc_opts fc_opts = {.bs = 0, .stmin = 0};
static const struct isotp_msg_id uds_req = {.std_id = CAN_ID_UDS_REQUEST, .dl = 8};
static const struct isotp_msg_id uds_resp = {.std_id = CAN_ID_UDS_RESPONSE, .dl = 8};
static struct isotp_recv_ctx recv_ctx;
static struct isotp_send_ctx send_ctx;

static uint8_t cmd_counter;
static int failures;

static void check(bool ok, const char *what)
{
	if (!ok) {
		failures++;
		printk("peer: FAIL %s\n", what);
	}
}

/* Wait for a frame with the given ID, up to timeout. */
static bool wait_frame(uint32_t id, struct can_frame *frame, int32_t timeout_ms)
{
	int64_t end = k_uptime_get() + timeout_ms;

	while (true) {
		int64_t left = end - k_uptime_get();

		if (left <= 0 || k_msgq_get(&rx_msgq, frame, K_MSEC(left)) != 0) {
			return false;
		}
		if (frame->id == id) {
			return true;
		}
	}
}

static bool wait_status(struct sensor_status_msg *st, int32_t timeout_ms)
{
	struct can_frame frame;

	if (!wait_frame(CAN_ID_SENSOR_STATUS, &frame, timeout_ms)) {
		return false;
	}
	return can_codec_unpack_sensor_status(frame.data, frame.dlc, st) == 0;
}

static void send_command(int16_t threshold_dC, bool alarm_reset)
{
	struct ecu_command_msg cmd = {
		.sample_period_ms = CMD_SAMPLE_PERIOD_KEEP,
		.alarm_threshold_dC = threshold_dC,
		.alarm_reset = alarm_reset,
		.counter = cmd_counter++,
	};
	struct can_frame frame = {.id = CAN_ID_ECU_COMMAND, .dlc = 8};

	can_codec_pack_ecu_command(&cmd, frame.data);
	check(can_send(can_dev, &frame, K_MSEC(100), NULL, NULL) == 0, "send ECU_COMMAND");
}

/* Wait up to 2 s for the alarm flag to reach the wanted state. */
static bool wait_alarm(bool wanted)
{
	struct sensor_status_msg st;

	for (int i = 0; i < 20; i++) {
		if (wait_status(&st, 300) && st.alarm_active == wanted) {
			return true;
		}
	}
	return false;
}

static int uds_request(const uint8_t *req, size_t len, uint8_t *resp, size_t resp_size)
{
	/*
	 * Leave the server a moment between transactions, as a real tester
	 * does. Two lockstep-emulated CPUs can otherwise answer the last
	 * consecutive frame before the server has seen its own TX complete.
	 */
	k_sleep(K_MSEC(10));

	/* Drop any stale response first. */
	while (isotp_recv(&recv_ctx, resp, resp_size, K_NO_WAIT) > 0) {
	}

	int err = isotp_send(&send_ctx, can_dev, req, len, &uds_req, &uds_resp, NULL, NULL);

	if (err != ISOTP_N_OK) {
		return -EIO;
	}
	return isotp_recv(&recv_ctx, resp, resp_size, K_MSEC(1000));
}

int main(void)
{
	const struct can_filter filter = {.id = 0x100, .mask = 0x7FE, .flags = 0};
	struct can_frame frame;
	struct sensor_status_msg st;
	uint8_t resp[64];
	int len;

	printk("peer: node starting\n");

	if (!device_is_ready(can_dev) || can_add_rx_filter_msgq(can_dev, &rx_msgq, &filter) < 0 ||
	    can_start(can_dev) != 0) {
		printk("peer: FAIL CAN init\n");
		return 0;
	}
	if (isotp_bind(&recv_ctx, can_dev, &uds_resp, &uds_req, &fc_opts, K_FOREVER) !=
	    ISOTP_N_OK) {
		printk("peer: FAIL isotp_bind\n");
		return 0;
	}

	/* 1. Heartbeat */
	if (wait_frame(CAN_ID_HEARTBEAT, &frame, 5000)) {
		struct heartbeat_msg hb;

		can_codec_unpack_heartbeat(frame.data, frame.dlc, &hb);
		printk("peer: HEARTBEAT uptime=%u state=%u\n", hb.uptime_s, hb.fw_state);
	} else {
		check(false, "no HEARTBEAT");
	}

	/* 2. Sensor status with valid E2E protection, counter advancing */
	struct sensor_status_msg first;

	if (wait_status(&first, 2000) && wait_status(&st, 1000)) {
		check(st.counter == ((first.counter + 1) & 0x0F), "rolling counter");
		printk("peer: SENSOR_STATUS temp=%d vib=%u alarm=%d crc=ok\n", st.temperature_dC,
		       st.vibration_cmm_s, st.alarm_active);
	} else {
		check(false, "no valid SENSOR_STATUS");
	}

	/* 3. UDS ReadDataByIdentifier 0xF189 on the ARM binary */
	len = uds_request((const uint8_t[]){0x22, 0xF1, 0x89}, 3, resp, sizeof(resp));
	if (len > 3 && resp[0] == 0x62) {
		printk("peer: UDS F189 = %.*s\n", len - 3, &resp[3]);
	} else {
		check(false, "UDS read F189");
	}

	/* 4. UDS session control */
	len = uds_request((const uint8_t[]){0x10, 0x03}, 2, resp, sizeof(resp));
	check(len == 6 && resp[0] == 0x50 && resp[1] == 0x03, "UDS extended session");
	if (len == 6) {
		printk("peer: UDS extended session ok\n");
	}

	/* 5. Two-node exchange: lower the threshold over CAN, see the alarm */
	send_command(100, false);
	check(wait_alarm(true), "alarm after lowering threshold");
	printk("peer: alarm active after ECU_COMMAND\n");

	/* 6. Raise the threshold and reset the alarm */
	send_command(ALARM_THRESHOLD_DEFAULT, true);
	check(wait_alarm(false), "alarm reset");
	printk("peer: alarm cleared after reset\n");

	/* 7. No faults expected */
	len = uds_request((const uint8_t[]){0x19, 0x01, 0x01}, 3, resp, sizeof(resp));
	check(len == 6 && resp[0] == 0x59, "UDS DTC count");
	if (len == 6) {
		printk("peer: active DTCs=%u\n", (resp[4] << 8) | resp[5]);
	}

	if (failures == 0) {
		printk("peer: ALL CHECKS PASSED\n");
	} else {
		printk("peer: %d CHECKS FAILED\n", failures);
	}
	return 0;
}
