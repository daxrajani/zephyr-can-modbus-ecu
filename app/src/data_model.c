/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>

#include "data_model.h"
#include "ecu_config.h"

static K_MUTEX_DEFINE(dm_lock);

static struct {
	int16_t temperature_dC;
	uint16_t vibration_cmm_s;
	bool alarm_active;
	bool sensor_fault;
	bool fault_injection;
	uint16_t sample_period_ms;
	int16_t alarm_threshold_dC;
	enum fw_state state;
} dm;

/* Caller holds dm_lock. The alarm latches: crossing the threshold sets it,
 * only dm_reset_alarm() clears it.
 */
static void evaluate_alarm(void)
{
	if (dm.temperature_dC > dm.alarm_threshold_dC) {
		dm.alarm_active = true;
	}
}

void dm_init(void)
{
	k_mutex_lock(&dm_lock, K_FOREVER);
	dm.temperature_dC = 0;
	dm.vibration_cmm_s = 0;
	dm.alarm_active = false;
	dm.sensor_fault = false;
	dm.fault_injection = false;
	dm.sample_period_ms = SAMPLE_PERIOD_DEFAULT_MS;
	dm.alarm_threshold_dC = ALARM_THRESHOLD_DEFAULT;
	dm.state = FW_STATE_INIT;
	k_mutex_unlock(&dm_lock);
}

void dm_get(struct dm_snapshot *out)
{
	k_mutex_lock(&dm_lock, K_FOREVER);
	out->temperature_dC = dm.temperature_dC;
	out->vibration_cmm_s = dm.vibration_cmm_s;
	out->alarm_active = dm.alarm_active;
	out->sensor_fault = dm.sensor_fault;
	out->fault_injection = dm.fault_injection;
	out->sample_period_ms = dm.sample_period_ms;
	out->alarm_threshold_dC = dm.alarm_threshold_dC;
	out->state = dm.state;
	k_mutex_unlock(&dm_lock);

	out->uptime_s = (uint32_t)(k_uptime_get() / 1000);
}

int dm_set_sample_period(uint16_t period_ms)
{
	if (period_ms < SAMPLE_PERIOD_MIN_MS || period_ms > SAMPLE_PERIOD_MAX_MS) {
		return -EINVAL;
	}
	k_mutex_lock(&dm_lock, K_FOREVER);
	dm.sample_period_ms = period_ms;
	k_mutex_unlock(&dm_lock);
	return 0;
}

int dm_set_alarm_threshold(int16_t threshold_dC)
{
	if (threshold_dC < ALARM_THRESHOLD_MIN || threshold_dC > ALARM_THRESHOLD_MAX) {
		return -EINVAL;
	}
	k_mutex_lock(&dm_lock, K_FOREVER);
	dm.alarm_threshold_dC = threshold_dC;
	evaluate_alarm();
	k_mutex_unlock(&dm_lock);
	return 0;
}

void dm_set_fault_injection(bool enable)
{
	k_mutex_lock(&dm_lock, K_FOREVER);
	dm.fault_injection = enable;
	k_mutex_unlock(&dm_lock);
}

void dm_set_state(enum fw_state state)
{
	k_mutex_lock(&dm_lock, K_FOREVER);
	dm.state = state;
	k_mutex_unlock(&dm_lock);
}

void dm_update_sensors(int16_t temperature_dC, uint16_t vibration_cmm_s, bool sensor_fault)
{
	k_mutex_lock(&dm_lock, K_FOREVER);
	dm.temperature_dC = temperature_dC;
	dm.vibration_cmm_s = vibration_cmm_s;
	dm.sensor_fault = sensor_fault;
	evaluate_alarm();
	k_mutex_unlock(&dm_lock);
}

void dm_reset_alarm(void)
{
	k_mutex_lock(&dm_lock, K_FOREVER);
	dm.alarm_active = false;
	evaluate_alarm();
	k_mutex_unlock(&dm_lock);
}
