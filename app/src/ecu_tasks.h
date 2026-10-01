/*
 * Start functions for the bus front ends. Each returns 0 or a negative
 * errno; main() keeps the ECU running if one bus fails to come up.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ECU_TASKS_H_
#define ECU_TASKS_H_

#include <zephyr/device.h>

int can_app_start(void);
const struct device *can_app_device(void);
int uds_server_start(void);
int modbus_server_start(void);

#endif /* ECU_TASKS_H_ */
