/*
 * Virtual industrial and automotive sensor node.
 *
 * main() owns the sampling loop and the slow supervision checks; CAN, UDS
 * and Modbus run in their own threads and only meet in the data model.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "data_model.h"
#include "dtc.h"
#include "ecu_config.h"
#include "ecu_tasks.h"
#include "modbus_map.h"
#include "sensors_sim.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("Sensor ECU %s starting (serial %s)", ECU_FW_VERSION, ECU_SERIAL_NUMBER);

	dm_init();
	dtc_init();
	sensors_sim_init(0xC0FFEE);

	bool can_ok = can_app_start() == 0;

	if (can_ok && uds_server_start() != 0) {
		LOG_ERR("UDS server failed to start");
	}
	if (modbus_server_start() != 0) {
		LOG_ERR("Modbus server failed to start");
	}

	dm_set_state(FW_STATE_RUN);
	LOG_INF("ECU running (CAN %s)", can_ok ? "up" : "DOWN");

	bool sensor_fault_logged = false;

	while (true) {
		struct dm_snapshot s;
		struct sensor_sample sample;

		dm_get(&s);
		sensors_sim_sample(k_uptime_get_32(), s.fault_injection, &sample);
		dm_update_sensors(sample.temperature_dC, sample.vibration_cmm_s,
				  sample.out_of_range);

		dtc_report(DTC_SENSOR_OUT_OF_RANGE, sample.out_of_range);
		dtc_report(DTC_MODBUS_MASTER_TIMEOUT, mb_link_timed_out(k_uptime_get()));

		if (sample.out_of_range != sensor_fault_logged) {
			sensor_fault_logged = sample.out_of_range;
			LOG_INF("Sensor %s", sensor_fault_logged ? "out of range" : "back in range");
		}

		k_sleep(K_MSEC(s.sample_period_ms));
	}

	return 0;
}
