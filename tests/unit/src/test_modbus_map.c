/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/ztest.h>

#include "data_model.h"
#include "dtc.h"
#include "ecu_config.h"
#include "modbus_map.h"

static void before(void *fixture)
{
	dm_init();
	dtc_init();
	mb_link_reset();
}

ZTEST(modbus_map, test_input_registers)
{
	uint16_t reg;

	dm_update_sensors(-55, 321, false);
	zassert_ok(mb_input_reg_rd(MB_IR_TEMPERATURE, &reg));
	zassert_equal((int16_t)reg, -55);
	zassert_ok(mb_input_reg_rd(MB_IR_VIBRATION, &reg));
	zassert_equal(reg, 321);
	zassert_ok(mb_input_reg_rd(MB_IR_UPTIME_HI, &reg));
	zassert_ok(mb_input_reg_rd(MB_IR_UPTIME_LO, &reg));

	dtc_report(DTC_SENSOR_OUT_OF_RANGE, true);
	zassert_ok(mb_input_reg_rd(MB_IR_ACTIVE_DTCS, &reg));
	zassert_equal(reg, 1);
}

ZTEST(modbus_map, test_register_map_bounds)
{
	uint16_t reg;
	bool bit;

	zassert_equal(mb_input_reg_rd(MB_IR_COUNT, &reg), -ENOTSUP);
	zassert_equal(mb_holding_reg_rd(MB_HR_COUNT, &reg), -ENOTSUP);
	zassert_equal(mb_holding_reg_wr(MB_HR_COUNT, 1), -ENOTSUP);
	zassert_equal(mb_discrete_input_rd(MB_DI_COUNT, &bit), -ENOTSUP);
	zassert_equal(mb_coil_rd(MB_COIL_COUNT, &bit), -ENOTSUP);
	zassert_equal(mb_coil_wr(MB_COIL_COUNT, true), -ENOTSUP);
	zassert_equal(mb_input_reg_rd(0xFFFF, &reg), -ENOTSUP);
}

ZTEST(modbus_map, test_holding_write_validates)
{
	uint16_t reg;

	zassert_ok(mb_holding_reg_wr(MB_HR_SAMPLE_PERIOD, 250));
	zassert_ok(mb_holding_reg_rd(MB_HR_SAMPLE_PERIOD, &reg));
	zassert_equal(reg, 250);
	zassert_equal(mb_holding_reg_wr(MB_HR_SAMPLE_PERIOD, 5), -EINVAL);

	/* -10.0 degC arrives as two's complement */
	zassert_ok(mb_holding_reg_wr(MB_HR_ALARM_THRESHOLD, (uint16_t)-100));
	zassert_ok(mb_holding_reg_rd(MB_HR_ALARM_THRESHOLD, &reg));
	zassert_equal((int16_t)reg, -100);
	zassert_equal(mb_holding_reg_wr(MB_HR_ALARM_THRESHOLD, 2000), -EINVAL);
}

ZTEST(modbus_map, test_threshold_write_drives_alarm_input)
{
	bool alarm;

	dm_update_sensors(250, 0, false);
	zassert_ok(mb_discrete_input_rd(MB_DI_ALARM, &alarm));
	zassert_false(alarm);

	zassert_ok(mb_holding_reg_wr(MB_HR_ALARM_THRESHOLD, 200));
	zassert_ok(mb_discrete_input_rd(MB_DI_ALARM, &alarm));
	zassert_true(alarm);

	/* Raise threshold again, then reset via the coil */
	zassert_ok(mb_holding_reg_wr(MB_HR_ALARM_THRESHOLD, 800));
	zassert_ok(mb_coil_wr(MB_COIL_ALARM_RESET, true));
	zassert_ok(mb_discrete_input_rd(MB_DI_ALARM, &alarm));
	zassert_false(alarm);
}

ZTEST(modbus_map, test_coils)
{
	bool bit;

	zassert_ok(mb_coil_wr(MB_COIL_FAULT_INJECTION, true));
	zassert_ok(mb_coil_rd(MB_COIL_FAULT_INJECTION, &bit));
	zassert_true(bit);
	zassert_ok(mb_coil_rd(MB_COIL_ALARM_RESET, &bit));
	zassert_false(bit, "reset coil is momentary");
}

ZTEST(modbus_map, test_link_timeout)
{
	zassert_false(mb_link_timed_out(100000), "never polled must not time out");

	mb_link_activity(1000);
	zassert_false(mb_link_timed_out(1000 + MODBUS_MASTER_TIMEOUT_MS));
	zassert_true(mb_link_timed_out(1000 + MODBUS_MASTER_TIMEOUT_MS + 1));

	mb_link_activity(9000);
	zassert_false(mb_link_timed_out(9001));
}

ZTEST_SUITE(modbus_map, NULL, NULL, before, NULL, NULL);
