/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "crc.h"

uint8_t crc8_sae_j1850(const uint8_t *data, size_t len)
{
	uint8_t crc = 0xFF;

	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; bit++) {
			crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x1D) : (uint8_t)(crc << 1);
		}
	}

	return crc ^ 0xFF;
}

uint16_t crc16_modbus(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xFFFF;

	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; bit++) {
			crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
		}
	}
	return crc;
}
