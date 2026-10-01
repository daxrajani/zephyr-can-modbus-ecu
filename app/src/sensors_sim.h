/*
 * Synthetic temperature and vibration sensors with fault injection.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SENSORS_SIM_H_
#define SENSORS_SIM_H_

#include <stdbool.h>
#include <stdint.h>

/* Raw value the faulty temperature sensor sticks at (150.0 degC). */
#define SENSOR_FAULT_TEMPERATURE_DC 1500

struct sensor_sample {
	int16_t temperature_dC;
	uint16_t vibration_cmm_s;
	bool out_of_range;
};

void sensors_sim_init(uint32_t seed);

/*
 * Produce one sample for time t_ms. Temperature is a slow 60 s sine around
 * 25 degC; vibration is the RMS of a 64-point window of a 50 Hz tone plus
 * noise. With inject_fault the temperature sticks out of range and the
 * vibration amplitude jumps five-fold.
 */
void sensors_sim_sample(uint32_t t_ms, bool inject_fault, struct sensor_sample *out);

#endif /* SENSORS_SIM_H_ */
