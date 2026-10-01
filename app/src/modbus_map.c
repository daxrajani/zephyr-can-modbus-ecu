/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include "data_model.h"
#include "dtc.h"
#include "ecu_config.h"
#include "modbus_map.h"

static bool link_seen;
static int64_t link_last_ms;

int mb_input_reg_rd(uint16_t addr, uint16_t *reg)
{
	struct dm_snapshot s;

	dm_get(&s);

	switch (addr) {
	case MB_IR_TEMPERATURE:
		*reg = (uint16_t)s.temperature_dC;
		return 0;
	case MB_IR_VIBRATION:
		*reg = s.vibration_cmm_s;
		return 0;
	case MB_IR_UPTIME_HI:
		*reg = (uint16_t)(s.uptime_s >> 16);
		return 0;
	case MB_IR_UPTIME_LO:
		*reg = (uint16_t)s.uptime_s;
		return 0;
	case MB_IR_ACTIVE_DTCS:
		*reg = (uint16_t)dtc_count_by_mask(DTC_STATUS_TEST_FAILED);
		return 0;
	default:
		return -ENOTSUP;
	}
}

int mb_discrete_input_rd(uint16_t addr, bool *state)
{
	struct dm_snapshot s;

	dm_get(&s);

	switch (addr) {
	case MB_DI_ALARM:
		*state = s.alarm_active;
		return 0;
	case MB_DI_SENSOR_FAULT:
		*state = s.sensor_fault;
		return 0;
	default:
		return -ENOTSUP;
	}
}

int mb_holding_reg_rd(uint16_t addr, uint16_t *reg)
{
	struct dm_snapshot s;

	dm_get(&s);

	switch (addr) {
	case MB_HR_SAMPLE_PERIOD:
		*reg = s.sample_period_ms;
		return 0;
	case MB_HR_ALARM_THRESHOLD:
		*reg = (uint16_t)s.alarm_threshold_dC;
		return 0;
	default:
		return -ENOTSUP;
	}
}

int mb_holding_reg_wr(uint16_t addr, uint16_t reg)
{
	switch (addr) {
	case MB_HR_SAMPLE_PERIOD:
		return dm_set_sample_period(reg);
	case MB_HR_ALARM_THRESHOLD:
		return dm_set_alarm_threshold((int16_t)reg);
	default:
		return -ENOTSUP;
	}
}

int mb_coil_rd(uint16_t addr, bool *state)
{
	struct dm_snapshot s;

	switch (addr) {
	case MB_COIL_ALARM_RESET:
		/* Momentary command, always reads back as off */
		*state = false;
		return 0;
	case MB_COIL_FAULT_INJECTION:
		dm_get(&s);
		*state = s.fault_injection;
		return 0;
	default:
		return -ENOTSUP;
	}
}

int mb_coil_wr(uint16_t addr, bool state)
{
	switch (addr) {
	case MB_COIL_ALARM_RESET:
		if (state) {
			dm_reset_alarm();
		}
		return 0;
	case MB_COIL_FAULT_INJECTION:
		dm_set_fault_injection(state);
		return 0;
	default:
		return -ENOTSUP;
	}
}

void mb_link_reset(void)
{
	link_seen = false;
	link_last_ms = 0;
}

void mb_link_activity(int64_t now_ms)
{
	link_seen = true;
	link_last_ms = now_ms;
}

bool mb_link_timed_out(int64_t now_ms)
{
	return link_seen && (now_ms - link_last_ms) > MODBUS_MASTER_TIMEOUT_MS;
}
