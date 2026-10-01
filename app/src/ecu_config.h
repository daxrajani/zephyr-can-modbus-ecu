/*
 * Project-wide constants for the sensor ECU.
 *
 * Every bus identifier, period and limit lives here so that the firmware,
 * the DBC file, the README tables and the host tools have one place to be
 * checked against.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ECU_CONFIG_H_
#define ECU_CONFIG_H_

#define ECU_FW_VERSION    "1.0.0"
#define ECU_SERIAL_NUMBER "ZCM-ECU-000001"

/* CAN identifiers (11-bit, 500 kbit/s) */
#define CAN_ID_SENSOR_STATUS 0x100
#define CAN_ID_HEARTBEAT     0x101
#define CAN_ID_ECU_COMMAND   0x200
#define CAN_ID_UDS_REQUEST   0x7E0
#define CAN_ID_UDS_RESPONSE  0x7E8

#define CAN_BITRATE                500000
#define SENSOR_STATUS_PERIOD_MS    100
#define HEARTBEAT_PERIOD_MS        1000

/* ECU_COMMAND "leave unchanged" sentinels */
#define CMD_SAMPLE_PERIOD_KEEP   0U
#define CMD_ALARM_THRESHOLD_KEEP INT16_MAX

/* Modbus RTU server */
#define MODBUS_UNIT_ID            1
#define MODBUS_BAUDRATE           19200
#define MODBUS_MASTER_TIMEOUT_MS  5000

/* UDS */
#define UDS_S3_TIMEOUT_MS         5000
#define UDS_SECURITY_MAX_ATTEMPTS 3
#define UDS_SECURITY_LOCKOUT_MS   10000

/* Data model defaults and limits. Temperatures are in 0.1 degC. */
#define SAMPLE_PERIOD_DEFAULT_MS  100
#define SAMPLE_PERIOD_MIN_MS      10
#define SAMPLE_PERIOD_MAX_MS      10000
#define ALARM_THRESHOLD_DEFAULT   800
#define ALARM_THRESHOLD_MIN       -400
#define ALARM_THRESHOLD_MAX       1250
#define TEMP_VALID_MIN            -400
#define TEMP_VALID_MAX            1250

#endif /* ECU_CONFIG_H_ */
