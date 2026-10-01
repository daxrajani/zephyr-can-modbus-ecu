/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>

#include "ecu_config.h"
#include "sensors_sim.h"

#define PI_F 3.14159265f

#define TEMP_BASE_DC      250.0f
#define TEMP_SWING_DC     50.0f
#define TEMP_PERIOD_MS    60000.0f
#define TEMP_NOISE_DC     2

#define VIB_WINDOW        64
#define VIB_SAMPLE_HZ     1000.0f
#define VIB_TONE_HZ       50.0f
#define VIB_RMS_MM_S      2.0f
#define VIB_NOISE_MM_S    0.2f
#define VIB_FAULT_GAIN    5.0f

static uint32_t rng_state = 1;

/* xorshift32: deterministic for a given seed, which keeps tests stable. */
static uint32_t rng_next(void)
{
	uint32_t x = rng_state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	rng_state = x;
	return x;
}

/* Uniform in [-1, 1). */
static float rng_unit(void)
{
	return ((float)(rng_next() & 0xFFFF) / 32768.0f) - 1.0f;
}

void sensors_sim_init(uint32_t seed)
{
	rng_state = seed ? seed : 1;
}

static uint16_t vibration_rms(float gain)
{
	const float amplitude = VIB_RMS_MM_S * 1.41421356f * gain;
	float sum_sq = 0.0f;

	for (int n = 0; n < VIB_WINDOW; n++) {
		float v = amplitude * sinf(2.0f * PI_F * VIB_TONE_HZ * (float)n / VIB_SAMPLE_HZ) +
			  VIB_NOISE_MM_S * rng_unit();
		sum_sq += v * v;
	}

	return (uint16_t)(sqrtf(sum_sq / VIB_WINDOW) * 100.0f + 0.5f);
}

void sensors_sim_sample(uint32_t t_ms, bool inject_fault, struct sensor_sample *out)
{
	float phase = 2.0f * PI_F * (float)(t_ms % (uint32_t)TEMP_PERIOD_MS) / TEMP_PERIOD_MS;
	int32_t temp = (int32_t)(TEMP_BASE_DC + TEMP_SWING_DC * sinf(phase));

	temp += (int32_t)(rng_next() % (2 * TEMP_NOISE_DC + 1)) - TEMP_NOISE_DC;

	if (inject_fault) {
		temp = SENSOR_FAULT_TEMPERATURE_DC;
	}

	out->temperature_dC = (int16_t)temp;
	out->vibration_cmm_s = vibration_rms(inject_fault ? VIB_FAULT_GAIN : 1.0f);
	out->out_of_range = temp < TEMP_VALID_MIN || temp > TEMP_VALID_MAX;
}
