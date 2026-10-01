/*
 * Modbus register map. Addresses below are the 0-based protocol addresses
 * Zephyr hands to the callbacks; the comments give the 1-based Modicon
 * numbers used in the README.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MODBUS_MAP_H_
#define MODBUS_MAP_H_

#include <stdbool.h>
#include <stdint.h>

/* Input registers (FC04) */
#define MB_IR_TEMPERATURE   0 /* 30001 temperature x10 (int16) */
#define MB_IR_VIBRATION     1 /* 30002 vibration RMS, 0.01 mm/s */
#define MB_IR_UPTIME_HI     2 /* 30003 uptime seconds, high word */
#define MB_IR_UPTIME_LO     3 /* 30004 uptime seconds, low word */
#define MB_IR_ACTIVE_DTCS   4 /* 30005 DTCs with testFailed set */
#define MB_IR_COUNT         5

/* Discrete inputs (FC02) */
#define MB_DI_ALARM         0 /* 10001 alarm active */
#define MB_DI_SENSOR_FAULT  1 /* 10002 sensor out of range */
#define MB_DI_COUNT         2

/* Holding registers (FC03/FC06/FC16) */
#define MB_HR_SAMPLE_PERIOD   0 /* 40001 sample period, ms */
#define MB_HR_ALARM_THRESHOLD 1 /* 40002 alarm threshold x10 (int16) */
#define MB_HR_COUNT           2

/* Coils (FC01/FC05/FC15) */
#define MB_COIL_ALARM_RESET     0 /* 00001 write 1 to reset the alarm */
#define MB_COIL_FAULT_INJECTION 1 /* 00002 fault injection on/off */
#define MB_COIL_COUNT           2

int mb_input_reg_rd(uint16_t addr, uint16_t *reg);
int mb_discrete_input_rd(uint16_t addr, bool *state);
int mb_holding_reg_rd(uint16_t addr, uint16_t *reg);
int mb_holding_reg_wr(uint16_t addr, uint16_t reg);
int mb_coil_rd(uint16_t addr, bool *state);
int mb_coil_wr(uint16_t addr, bool state);

/* Master supervision: note a valid request, ask whether the master went quiet. */
void mb_link_reset(void);
void mb_link_activity(int64_t now_ms);
bool mb_link_timed_out(int64_t now_ms);

#endif /* MODBUS_MAP_H_ */
