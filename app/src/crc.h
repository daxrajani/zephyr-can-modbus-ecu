/*
 * Checksums used on the two buses.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CRC_H_
#define CRC_H_

#include <stddef.h>
#include <stdint.h>

/* CRC-8 SAE J1850 (poly 0x1D, init 0xFF, xorout 0xFF), as in AUTOSAR E2E. */
uint8_t crc8_sae_j1850(const uint8_t *data, size_t len);

/* Modbus RTU CRC-16 (poly 0xA001 reflected, init 0xFFFF). */
uint16_t crc16_modbus(const uint8_t *data, size_t len);

#endif /* CRC_H_ */
