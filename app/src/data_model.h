/*
 * Shared sensor state. CAN, UDS and Modbus are all front ends onto this one
 * model; every access goes through a mutex so a threshold written over
 * Modbus is seen atomically by the CAN transmitter and vice versa.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DATA_MODEL_H_
#define DATA_MODEL_H_

#include <stdbool.h>
#include <stdint.h>

enum fw_state {
	FW_STATE_INIT = 0,
	FW_STATE_RUN = 1,
	FW_STATE_DEGRADED = 2,
};

struct dm_snapshot {
	int16_t temperature_dC;
	uint16_t vibration_cmm_s;
	bool alarm_active;
	bool sensor_fault;
	bool fault_injection;
	uint16_t sample_period_ms;
	int16_t alarm_threshold_dC;
	uint32_t uptime_s;
	enum fw_state state;
};

void dm_init(void);

/* Consistent copy of the whole model, taken under the lock. */
void dm_get(struct dm_snapshot *out);

/* Setters validate ranges and return -EINVAL without changing anything. */
int dm_set_sample_period(uint16_t period_ms);
int dm_set_alarm_threshold(int16_t threshold_dC);
void dm_set_fault_injection(bool enable);
void dm_set_state(enum fw_state state);

/* New sensor values; re-evaluates the latched alarm. */
void dm_update_sensors(int16_t temperature_dC, uint16_t vibration_cmm_s, bool sensor_fault);

/*
 * Clear the latched alarm. The alarm only clears if the temperature is back
 * at or below the threshold, otherwise it stays active.
 */
void dm_reset_alarm(void);

#endif /* DATA_MODEL_H_ */
