/*
 * Modbus RTU framing over a Zephyr UART, feeding Zephyr's Modbus server
 * through its raw ADU interface.
 *
 * Zephyr's built-in RTU backend needs uart_configure(), which the
 * native_sim PTY UART does not implement. Doing the framing here (silent
 * interval detection plus CRC-16) gives one code path that runs on both
 * native_sim and the STM32F4.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MODBUS_RTU_H_
#define MODBUS_RTU_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/modbus/modbus.h>

typedef void (*modbus_rtu_activity_cb_t)(void);

int modbus_rtu_init(const struct device *uart, uint32_t baudrate, int raw_iface,
		    uint8_t unit_id, modbus_rtu_activity_cb_t on_request);

/* Raw ADU transmit callback to register with modbus_init_server(). */
int modbus_rtu_tx_cb(const int iface, const struct modbus_adu *adu, void *user_data);

#endif /* MODBUS_RTU_H_ */
