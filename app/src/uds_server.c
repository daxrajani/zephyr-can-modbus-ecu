/*
 * UDS over ISO-TP (ISO 15765-2): physical addressing, request 0x7E0,
 * response 0x7E8.
 *
 * A multi-frame response needs the tester's flow control frame, which also
 * arrives on 0x7E0. Zephyr's ISO-TP send context installs its own 0x7E0
 * filter for it, overlapping the request binding. Controllers with hardware
 * filter dispatch (bxCAN, M_CAN) hand each frame to exactly one matching
 * filter, so the flow control would land in the request context and the
 * response would stall. UDS is half duplex, so the request binding is simply
 * released while a multi-frame response is in flight.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/canbus/isotp.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ecu_config.h"
#include "ecu_tasks.h"
#include "uds.h"

LOG_MODULE_REGISTER(uds_server, LOG_LEVEL_INF);

#define UDS_STACK 2048
#define UDS_PRIO  7

#define ISOTP_SF_MAX_PAYLOAD 7

static const struct isotp_fc_opts fc_opts = {
	.bs = 0,    /* receive the whole request without further flow control */
	.stmin = 0,
};

static const struct isotp_msg_id rx_addr = {
	.std_id = CAN_ID_UDS_REQUEST,
	.dl = 8,
};

static const struct isotp_msg_id tx_addr = {
	.std_id = CAN_ID_UDS_RESPONSE,
	.dl = 8,
};

static struct isotp_recv_ctx recv_ctx;
static struct isotp_send_ctx send_ctx;

K_THREAD_STACK_DEFINE(uds_stack, UDS_STACK);
static struct k_thread uds_thread;

static int bind_requests(const struct device *can_dev)
{
	int err = isotp_bind(&recv_ctx, can_dev, &rx_addr, &tx_addr, &fc_opts, K_FOREVER);

	if (err != ISOTP_N_OK) {
		LOG_ERR("isotp_bind failed: %d", err);
		return -EIO;
	}
	return 0;
}

static void uds_loop(void *p1, void *p2, void *p3)
{
	const struct device *can_dev = can_app_device();
	static uint8_t req[UDS_MAX_MESSAGE];
	static uint8_t resp[UDS_MAX_MESSAGE];

	while (true) {
		int len = isotp_recv(&recv_ctx, req, sizeof(req), K_MSEC(100));

		if (len == ISOTP_RECV_TIMEOUT) {
			uds_tick(k_uptime_get());
			continue;
		}
		if (len < 0) {
			LOG_WRN("ISO-TP receive error %d", len);
			continue;
		}

		size_t resp_len = uds_process(req, (size_t)len, resp, sizeof(resp),
					      k_uptime_get());

		if (resp_len == 0) {
			continue;
		}

		bool multi_frame = resp_len > ISOTP_SF_MAX_PAYLOAD;

		if (multi_frame) {
			isotp_unbind(&recv_ctx);
		}

		int err = isotp_send(&send_ctx, can_dev, resp, resp_len, &tx_addr, &rx_addr,
				     NULL, NULL);

		if (err != ISOTP_N_OK) {
			LOG_WRN("ISO-TP send error %d", err);
		}
		if (multi_frame && bind_requests(can_dev) != 0) {
			LOG_ERR("Lost the UDS request binding");
			return;
		}
	}
}

int uds_server_start(void)
{
	const struct device *can_dev = can_app_device();

	uds_init(k_cycle_get_32());

	if (bind_requests(can_dev) != 0) {
		return -EIO;
	}

	k_thread_create(&uds_thread, uds_stack, K_THREAD_STACK_SIZEOF(uds_stack), uds_loop,
			NULL, NULL, NULL, UDS_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&uds_thread, "uds");

	LOG_INF("UDS server on 0x%03X/0x%03X", CAN_ID_UDS_REQUEST, CAN_ID_UDS_RESPONSE);
	return 0;
}
