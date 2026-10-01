/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "crc.h"
#include "modbus_rtu.h"

LOG_MODULE_REGISTER(modbus_rtu, LOG_LEVEL_INF);

#define RTU_MAX_FRAME 256
#define RTU_MIN_FRAME 4 /* unit + fc + crc16 */

static const struct device *rtu_uart;
static int rtu_iface;
static uint8_t rtu_unit_id;
static modbus_rtu_activity_cb_t rtu_on_request;
static uint32_t rtu_gap_us;

/* Filled from the UART ISR, handed to the work item when the line goes quiet. */
static uint8_t rx_buf[RTU_MAX_FRAME];
static size_t rx_len;
static bool rx_overflow;

static uint8_t frame[RTU_MAX_FRAME];
static size_t frame_len;

static struct modbus_adu rx_adu;

static void frame_work_handler(struct k_work *work);
static K_WORK_DEFINE(frame_work, frame_work_handler);

static void gap_timer_expired(struct k_timer *timer)
{
	k_work_submit(&frame_work);
}
static K_TIMER_DEFINE(gap_timer, gap_timer_expired, NULL);

static void uart_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
		uint8_t byte;

		while (uart_fifo_read(dev, &byte, 1) == 1) {
			if (rx_len < sizeof(rx_buf)) {
				rx_buf[rx_len++] = byte;
			} else {
				rx_overflow = true;
			}
		}
		/* Every byte restarts the t3.5 silent-interval timer. */
		k_timer_start(&gap_timer, K_USEC(rtu_gap_us), K_NO_WAIT);
	}
}

static void frame_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	unsigned int key = irq_lock();
	bool overflow = rx_overflow;

	frame_len = rx_len;
	memcpy(frame, rx_buf, rx_len);
	rx_len = 0;
	rx_overflow = false;
	irq_unlock(key);

	if (overflow || frame_len < RTU_MIN_FRAME) {
		LOG_WRN("Dropped frame (len %zu, overflow %d)", frame_len, overflow);
		return;
	}

	uint16_t crc = (uint16_t)(frame[frame_len - 2] | (frame[frame_len - 1] << 8));

	if (crc16_modbus(frame, frame_len - 2) != crc) {
		LOG_WRN("CRC error, frame dropped");
		return;
	}

	if (frame[0] != rtu_unit_id && frame[0] != 0) {
		return; /* addressed to another server on the bus */
	}

	memset(&rx_adu, 0, sizeof(rx_adu));
	rx_adu.unit_id = frame[0];
	rx_adu.fc = frame[1];
	rx_adu.length = (uint16_t)(frame_len - RTU_MIN_FRAME);
	memcpy(rx_adu.data, &frame[2], rx_adu.length);

	if (rtu_on_request != NULL) {
		rtu_on_request();
	}

	int err = modbus_raw_submit_rx(rtu_iface, &rx_adu);

	if (err) {
		LOG_ERR("modbus_raw_submit_rx failed: %d", err);
	}
}

int modbus_rtu_tx_cb(const int iface, const struct modbus_adu *adu, void *user_data)
{
	uint8_t out[RTU_MAX_FRAME];
	size_t len = 0;

	ARG_UNUSED(iface);
	ARG_UNUSED(user_data);

	if (adu->unit_id == 0) {
		return 0; /* broadcast requests get no reply */
	}
	if ((size_t)adu->length + RTU_MIN_FRAME > sizeof(out)) {
		return -EMSGSIZE;
	}

	out[len++] = adu->unit_id;
	out[len++] = adu->fc;
	memcpy(&out[len], adu->data, adu->length);
	len += adu->length;

	uint16_t crc = crc16_modbus(out, len);

	out[len++] = (uint8_t)crc;
	out[len++] = (uint8_t)(crc >> 8);

	for (size_t i = 0; i < len; i++) {
		uart_poll_out(rtu_uart, out[i]);
	}
	return 0;
}

int modbus_rtu_init(const struct device *uart, uint32_t baudrate, int raw_iface,
		    uint8_t unit_id, modbus_rtu_activity_cb_t on_request)
{
	if (!device_is_ready(uart)) {
		return -ENODEV;
	}

	rtu_uart = uart;
	rtu_iface = raw_iface;
	rtu_unit_id = unit_id;
	rtu_on_request = on_request;

	/* t3.5 = 3.5 characters of 11 bits; the spec fixes it at 1750 us above 19200 baud. */
	rtu_gap_us = baudrate > 19200 ? 1750 : (uint32_t)(38500000ULL / baudrate);
	rtu_gap_us = MAX(rtu_gap_us, CONFIG_ECU_MODBUS_MIN_FRAME_GAP_US);

	struct uart_config cfg = {
		.baudrate = baudrate,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};
	int err = uart_configure(uart, &cfg);

	if (err == -ENOSYS) {
		LOG_INF("UART has no runtime configuration (PTY), using it as is");
	} else if (err) {
		LOG_ERR("uart_configure failed: %d", err);
		return err;
	}

	err = uart_irq_callback_user_data_set(uart, uart_isr, NULL);
	if (err) {
		return err;
	}
	uart_irq_rx_enable(uart);

	LOG_INF("Modbus RTU on %s, %u baud 8N1, unit %u, frame gap %u us", uart->name,
		baudrate, unit_id, rtu_gap_us);
	return 0;
}
