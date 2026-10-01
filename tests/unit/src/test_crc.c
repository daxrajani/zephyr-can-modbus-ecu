/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include "crc.h"

static const uint8_t check_input[] = "123456789";

ZTEST(crc, test_crc8_check_value)
{
	/* Published check value for CRC-8/SAE-J1850 */
	zassert_equal(crc8_sae_j1850(check_input, 9), 0x4B);
}

ZTEST(crc, test_crc8_empty_is_init_xor_out)
{
	zassert_equal(crc8_sae_j1850(NULL, 0), 0x00);
}

ZTEST(crc, test_crc8_detects_single_bit_flip)
{
	uint8_t data[7] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x07};
	uint8_t ref = crc8_sae_j1850(data, sizeof(data));

	for (int byte = 0; byte < 7; byte++) {
		for (int bit = 0; bit < 8; bit++) {
			data[byte] ^= BIT(bit);
			zassert_not_equal(crc8_sae_j1850(data, sizeof(data)), ref,
					  "flip byte %d bit %d undetected", byte, bit);
			data[byte] ^= BIT(bit);
		}
	}
}

ZTEST(crc, test_crc16_modbus_check_value)
{
	/* Published check value for CRC-16/MODBUS */
	zassert_equal(crc16_modbus(check_input, 9), 0x4B37);
}

ZTEST(crc, test_crc16_modbus_known_frame)
{
	/* 01 04 00 00 00 05: read 5 input registers from unit 1 -> CRC 30 09 */
	const uint8_t frame[] = {0x01, 0x04, 0x00, 0x00, 0x00, 0x05};
	uint16_t crc = crc16_modbus(frame, sizeof(frame));

	zassert_equal(crc & 0xFF, 0x30);
	zassert_equal(crc >> 8, 0x09);
}

ZTEST_SUITE(crc, NULL, NULL, NULL, NULL, NULL);
