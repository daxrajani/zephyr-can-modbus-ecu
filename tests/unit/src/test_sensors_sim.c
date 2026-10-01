/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include "ecu_config.h"
#include "sensors_sim.h"

ZTEST(sensors_sim, test_nominal_signal_in_range)
{
	struct sensor_sample s;

	sensors_sim_init(1234);
	for (uint32_t t = 0; t < 120000; t += 250) {
		sensors_sim_sample(t, false, &s);
		zassert_false(s.out_of_range);
		zassert_within(s.temperature_dC, 250, 53, "T=%d at %u", s.temperature_dC, t);
		zassert_within(s.vibration_cmm_s, 200, 30, "vib=%u", s.vibration_cmm_s);
	}
}

ZTEST(sensors_sim, test_temperature_follows_slow_sine)
{
	struct sensor_sample peak, trough;

	sensors_sim_init(1);
	sensors_sim_sample(15000, false, &peak);   /* quarter period */
	sensors_sim_sample(45000, false, &trough); /* three quarters */
	zassert_true(peak.temperature_dC > 290);
	zassert_true(trough.temperature_dC < 210);
}

ZTEST(sensors_sim, test_fault_injection)
{
	struct sensor_sample s;

	sensors_sim_init(99);
	sensors_sim_sample(5000, true, &s);
	zassert_true(s.out_of_range);
	zassert_equal(s.temperature_dC, SENSOR_FAULT_TEMPERATURE_DC);
	zassert_true(s.vibration_cmm_s > 800, "vibration should jump, got %u", s.vibration_cmm_s);
}

ZTEST(sensors_sim, test_deterministic_for_seed)
{
	struct sensor_sample a, b;

	sensors_sim_init(42);
	sensors_sim_sample(777, false, &a);
	sensors_sim_init(42);
	sensors_sim_sample(777, false, &b);
	zassert_equal(a.temperature_dC, b.temperature_dC);
	zassert_equal(a.vibration_cmm_s, b.vibration_cmm_s);
}

ZTEST_SUITE(sensors_sim, NULL, NULL, NULL, NULL, NULL);
