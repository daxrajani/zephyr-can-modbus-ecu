/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include "dtc.h"

static void before(void *fixture)
{
	dtc_init();
}

ZTEST(dtc, test_starts_clean)
{
	zassert_equal(dtc_count_by_mask(0xFF), 0);
	for (int i = 0; i < DTC_COUNT; i++) {
		zassert_equal(dtc_status(i), 0);
	}
}

ZTEST(dtc, test_failed_sets_all_status_bits)
{
	dtc_report(DTC_SENSOR_OUT_OF_RANGE, true);
	zassert_equal(dtc_status(DTC_SENSOR_OUT_OF_RANGE), 0x0F);
	zassert_equal(dtc_count_by_mask(DTC_STATUS_TEST_FAILED), 1);
}

ZTEST(dtc, test_passed_keeps_confirmed)
{
	dtc_report(DTC_CAN_BUS_OFF, true);
	dtc_report(DTC_CAN_BUS_OFF, false);

	uint8_t st = dtc_status(DTC_CAN_BUS_OFF);

	zassert_false(st & DTC_STATUS_TEST_FAILED);
	zassert_true(st & DTC_STATUS_CONFIRMED);
	zassert_equal(dtc_count_by_mask(DTC_STATUS_TEST_FAILED), 0);
	zassert_equal(dtc_count_by_mask(DTC_STATUS_CONFIRMED), 1);
}

ZTEST(dtc, test_passed_on_clean_dtc_is_noop)
{
	dtc_report(DTC_MODBUS_MASTER_TIMEOUT, false);
	zassert_equal(dtc_status(DTC_MODBUS_MASTER_TIMEOUT), 0);
}

ZTEST(dtc, test_get_by_mask_returns_codes)
{
	struct dtc_record recs[DTC_COUNT];

	dtc_report(DTC_SENSOR_OUT_OF_RANGE, true);
	dtc_report(DTC_COMMAND_CRC_ERROR, true);

	size_t n = dtc_get_by_mask(DTC_STATUS_CONFIRMED, recs, DTC_COUNT);

	zassert_equal(n, 2);
	zassert_equal(recs[0].code, 0x0A1011);
	zassert_equal(recs[1].code, 0xC41481);
	zassert_equal(recs[1].status, 0x0F);
}

ZTEST(dtc, test_get_by_mask_respects_max)
{
	struct dtc_record recs[1];

	dtc_report(DTC_SENSOR_OUT_OF_RANGE, true);
	dtc_report(DTC_CAN_BUS_OFF, true);
	zassert_equal(dtc_get_by_mask(0xFF, recs, 1), 1);
}

ZTEST(dtc, test_clear_all)
{
	dtc_report(DTC_SENSOR_OUT_OF_RANGE, true);
	dtc_report(DTC_CAN_BUS_OFF, true);
	dtc_clear_all();
	zassert_equal(dtc_count_by_mask(0xFF), 0);
}

ZTEST(dtc, test_out_of_range_id_is_ignored)
{
	dtc_report(DTC_COUNT, true);
	zassert_equal(dtc_count_by_mask(0xFF), 0);
	zassert_equal(dtc_code(DTC_COUNT), 0);
}

ZTEST_SUITE(dtc, NULL, NULL, before, NULL, NULL);
