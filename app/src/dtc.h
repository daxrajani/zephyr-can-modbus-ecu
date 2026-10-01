/*
 * Diagnostic trouble code storage with ISO 14229 status bits. RAM only;
 * moving it to NVS/ZMS is the natural next step on real hardware.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DTC_H_
#define DTC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum dtc_id {
	DTC_SENSOR_OUT_OF_RANGE,
	DTC_CAN_BUS_OFF,
	DTC_MODBUS_MASTER_TIMEOUT,
	DTC_COMMAND_CRC_ERROR,
	DTC_COUNT,
};

/* ISO 14229-1 DTC status bits used by this ECU */
#define DTC_STATUS_TEST_FAILED            0x01
#define DTC_STATUS_TEST_FAILED_THIS_CYCLE 0x02
#define DTC_STATUS_PENDING                0x04
#define DTC_STATUS_CONFIRMED              0x08
#define DTC_STATUS_AVAILABILITY_MASK      0x0F

struct dtc_record {
	uint32_t code; /* 3-byte DTC number */
	uint8_t status;
};

void dtc_init(void);

/*
 * Report the result of a monitor. failed=true sets testFailed, pending and
 * confirmed. failed=false clears testFailed only, so a recovered fault
 * stays visible as confirmed until a tester clears it.
 */
void dtc_report(enum dtc_id id, bool failed);

void dtc_clear_all(void);

uint32_t dtc_code(enum dtc_id id);
uint8_t dtc_status(enum dtc_id id);

/* Number of DTCs whose status matches (status & mask) != 0. */
size_t dtc_count_by_mask(uint8_t mask);

/* Copy matching DTCs into out (up to max). Returns the number copied. */
size_t dtc_get_by_mask(uint8_t mask, struct dtc_record *out, size_t max);

#endif /* DTC_H_ */
