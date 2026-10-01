/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include "dtc.h"

/*
 * DTC numbers follow the SAE J2012 3-byte layout (2-byte code + failure
 * type byte). The U-codes reuse real network fault numbers; P0A10 is a
 * project-defined powertrain sensor code.
 */
static const uint32_t dtc_codes[DTC_COUNT] = {
	[DTC_SENSOR_OUT_OF_RANGE] = 0x0A1011,   /* P0A10-11 temperature out of range */
	[DTC_CAN_BUS_OFF] = 0xC07388,           /* U0073-88 CAN bus off */
	[DTC_MODBUS_MASTER_TIMEOUT] = 0xC10087, /* U0100-87 Modbus master missing */
	[DTC_COMMAND_CRC_ERROR] = 0xC41481,     /* U0414-81 invalid command CRC */
};

static uint8_t dtc_state[DTC_COUNT];
static struct k_spinlock dtc_lock;

void dtc_init(void)
{
	dtc_clear_all();
}

void dtc_report(enum dtc_id id, bool failed)
{
	if (id >= DTC_COUNT) {
		return;
	}

	K_SPINLOCK(&dtc_lock) {
		if (failed) {
			dtc_state[id] |= DTC_STATUS_TEST_FAILED | DTC_STATUS_TEST_FAILED_THIS_CYCLE |
					 DTC_STATUS_PENDING | DTC_STATUS_CONFIRMED;
		} else {
			dtc_state[id] &= ~DTC_STATUS_TEST_FAILED;
		}
	}
}

void dtc_clear_all(void)
{
	K_SPINLOCK(&dtc_lock) {
		for (int i = 0; i < DTC_COUNT; i++) {
			dtc_state[i] = 0;
		}
	}
}

uint32_t dtc_code(enum dtc_id id)
{
	return id < DTC_COUNT ? dtc_codes[id] : 0;
}

uint8_t dtc_status(enum dtc_id id)
{
	uint8_t status = 0;

	if (id < DTC_COUNT) {
		K_SPINLOCK(&dtc_lock) {
			status = dtc_state[id];
		}
	}
	return status;
}

size_t dtc_count_by_mask(uint8_t mask)
{
	return dtc_get_by_mask(mask, NULL, DTC_COUNT);
}

size_t dtc_get_by_mask(uint8_t mask, struct dtc_record *out, size_t max)
{
	size_t n = 0;

	K_SPINLOCK(&dtc_lock) {
		for (int i = 0; i < DTC_COUNT && n < max; i++) {
			if ((dtc_state[i] & mask) == 0) {
				continue;
			}
			if (out != NULL) {
				out[n].code = dtc_codes[i];
				out[n].status = dtc_state[i];
			}
			n++;
		}
	}
	return n;
}
