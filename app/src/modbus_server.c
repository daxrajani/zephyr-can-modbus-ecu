/*
 * Modbus server: wires the register map callbacks into Zephyr's Modbus
 * server and the RTU framer onto the UART chosen in devicetree.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/modbus/modbus.h>

#include "ecu_config.h"
#include "ecu_tasks.h"
#include "modbus_map.h"
#include "modbus_rtu.h"

LOG_MODULE_REGISTER(modbus_server, LOG_LEVEL_INF);

static struct modbus_user_callbacks mb_callbacks = {
	.coil_rd = mb_coil_rd,
	.coil_wr = mb_coil_wr,
	.discrete_input_rd = mb_discrete_input_rd,
	.input_reg_rd = mb_input_reg_rd,
	.holding_reg_rd = mb_holding_reg_rd,
	.holding_reg_wr = mb_holding_reg_wr,
};

static void on_request(void)
{
	mb_link_activity(k_uptime_get());
}

int modbus_server_start(void)
{
	const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(ecu_modbus_uart));
	int iface = modbus_iface_get_by_name("RAW_0");

	if (iface < 0) {
		LOG_ERR("No raw Modbus interface");
		return iface;
	}

	struct modbus_iface_param param = {
		.mode = MODBUS_MODE_RAW,
		.server = {
			.user_cb = &mb_callbacks,
			.unit_id = MODBUS_UNIT_ID,
		},
		.rawcb = {
			.raw_tx_cb = modbus_rtu_tx_cb,
			.user_data = NULL,
		},
	};

	int err = modbus_init_server(iface, param);

	if (err) {
		LOG_ERR("modbus_init_server failed: %d", err);
		return err;
	}

	mb_link_reset();
	return modbus_rtu_init(uart, MODBUS_BAUDRATE, iface, MODBUS_UNIT_ID, on_request);
}
