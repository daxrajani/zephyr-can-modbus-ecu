/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/ztest.h>

#include "data_model.h"
#include "ecu_config.h"

static void before(void *fixture)
{
	dm_init();
}

ZTEST(data_model, test_defaults)
{
	struct dm_snapshot s;

	dm_get(&s);
	zassert_equal(s.sample_period_ms, SAMPLE_PERIOD_DEFAULT_MS);
	zassert_equal(s.alarm_threshold_dC, ALARM_THRESHOLD_DEFAULT);
	zassert_false(s.alarm_active);
	zassert_false(s.fault_injection);
	zassert_equal(s.state, FW_STATE_INIT);
}

ZTEST(data_model, test_sample_period_bounds)
{
	struct dm_snapshot s;

	zassert_equal(dm_set_sample_period(SAMPLE_PERIOD_MIN_MS - 1), -EINVAL);
	zassert_equal(dm_set_sample_period(SAMPLE_PERIOD_MAX_MS + 1), -EINVAL);
	zassert_ok(dm_set_sample_period(SAMPLE_PERIOD_MIN_MS));
	zassert_ok(dm_set_sample_period(SAMPLE_PERIOD_MAX_MS));
	dm_get(&s);
	zassert_equal(s.sample_period_ms, SAMPLE_PERIOD_MAX_MS);
}

ZTEST(data_model, test_threshold_bounds)
{
	struct dm_snapshot s;

	zassert_equal(dm_set_alarm_threshold(ALARM_THRESHOLD_MIN - 1), -EINVAL);
	zassert_equal(dm_set_alarm_threshold(ALARM_THRESHOLD_MAX + 1), -EINVAL);
	dm_get(&s);
	zassert_equal(s.alarm_threshold_dC, ALARM_THRESHOLD_DEFAULT, "rejected write changed state");
	zassert_ok(dm_set_alarm_threshold(-400));
}

ZTEST(data_model, test_alarm_sets_above_threshold_and_latches)
{
	struct dm_snapshot s;

	zassert_ok(dm_set_alarm_threshold(300));
	dm_update_sensors(300, 0, false); /* equal is not above */
	dm_get(&s);
	zassert_false(s.alarm_active);

	dm_update_sensors(301, 0, false);
	dm_get(&s);
	zassert_true(s.alarm_active);

	dm_update_sensors(250, 0, false); /* back below: still latched */
	dm_get(&s);
	zassert_true(s.alarm_active);
}

ZTEST(data_model, test_reset_only_clears_when_condition_gone)
{
	struct dm_snapshot s;

	zassert_ok(dm_set_alarm_threshold(300));
	dm_update_sensors(350, 0, false);
	dm_reset_alarm();
	dm_get(&s);
	zassert_true(s.alarm_active, "reset must not clear an alarm whose cause is present");

	dm_update_sensors(250, 0, false);
	dm_reset_alarm();
	dm_get(&s);
	zassert_false(s.alarm_active);
}

ZTEST(data_model, test_lowering_threshold_raises_alarm_immediately)
{
	struct dm_snapshot s;

	dm_update_sensors(250, 0, false);
	dm_get(&s);
	zassert_false(s.alarm_active);

	/* The cross-bus scenario: a Modbus write must show up on CAN at once. */
	zassert_ok(dm_set_alarm_threshold(100));
	dm_get(&s);
	zassert_true(s.alarm_active);
}

ZTEST(data_model, test_fault_injection_flag)
{
	struct dm_snapshot s;

	dm_set_fault_injection(true);
	dm_get(&s);
	zassert_true(s.fault_injection);
	dm_set_fault_injection(false);
	dm_get(&s);
	zassert_false(s.fault_injection);
}

ZTEST_SUITE(data_model, NULL, NULL, before, NULL, NULL);
