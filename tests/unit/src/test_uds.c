/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "data_model.h"
#include "dtc.h"
#include "ecu_config.h"
#include "uds.h"

static uint8_t resp[UDS_MAX_MESSAGE];
static int64_t now;

#define REQ(...) ((const uint8_t[]){__VA_ARGS__}), sizeof((const uint8_t[]){__VA_ARGS__})

static size_t send(const uint8_t *req, size_t len)
{
	memset(resp, 0xAA, sizeof(resp));
	now += 10;
	return uds_process(req, len, resp, sizeof(resp), now);
}

static void assert_nrc(size_t len, uint8_t sid, uint8_t code)
{
	zassert_equal(len, 3, "expected NRC, got len %zu", len);
	zassert_equal(resp[0], 0x7F);
	zassert_equal(resp[1], sid);
	zassert_equal(resp[2], code, "NRC 0x%02X != 0x%02X", resp[2], code);
}

static void enter_extended(void)
{
	zassert_equal(send(REQ(0x10, 0x03)), 6);
}

static void unlock(void)
{
	enter_extended();
	zassert_equal(send(REQ(0x27, 0x01)), 6);

	uint32_t seed = sys_get_be32(&resp[2]);
	uint32_t key = uds_compute_key(seed);
	uint8_t req[6] = {0x27, 0x02};

	sys_put_be32(key, &req[2]);
	zassert_equal(send(req, sizeof(req)), 2);
	zassert_equal(resp[0], 0x67);
}

static void before(void *fixture)
{
	now = 0;
	dm_init();
	dtc_init();
	uds_init(0x1234);
}

/* --- generic --- */

ZTEST(uds, test_unknown_service)
{
	assert_nrc(send(REQ(0x31, 0x01, 0xFF, 0x00)), 0x31, UDS_NRC_SERVICE_NOT_SUPPORTED);
}

ZTEST(uds, test_empty_request_is_ignored)
{
	zassert_equal(uds_process(resp, 0, resp, sizeof(resp), 0), 0);
}

/* --- 0x10 DiagnosticSessionControl --- */

ZTEST(uds, test_session_control_extended)
{
	zassert_equal(send(REQ(0x10, 0x03)), 6);
	zassert_mem_equal(resp, ((uint8_t[]){0x50, 0x03, 0x00, 0x32, 0x01, 0xF4}), 6);
	zassert_equal(uds_active_session(), UDS_SESSION_EXTENDED);
}

ZTEST(uds, test_session_control_bad_subfunction_and_length)
{
	assert_nrc(send(REQ(0x10, 0x02)), 0x10, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
	assert_nrc(send(REQ(0x10)), 0x10, UDS_NRC_INCORRECT_LENGTH);
	assert_nrc(send(REQ(0x10, 0x03, 0x00)), 0x10, UDS_NRC_INCORRECT_LENGTH);
}

ZTEST(uds, test_session_control_suppress_positive_response)
{
	zassert_equal(send(REQ(0x10, 0x83)), 0);
	zassert_equal(uds_active_session(), UDS_SESSION_EXTENDED);
}

/* --- 0x3E TesterPresent and S3 --- */

ZTEST(uds, test_tester_present)
{
	zassert_equal(send(REQ(0x3E, 0x00)), 2);
	zassert_equal(resp[0], 0x7E);
	zassert_equal(send(REQ(0x3E, 0x80)), 0);
	assert_nrc(send(REQ(0x3E, 0x01)), 0x3E, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
	assert_nrc(send(REQ(0x3E)), 0x3E, UDS_NRC_INCORRECT_LENGTH);
}

ZTEST(uds, test_s3_timeout_returns_to_default)
{
	enter_extended();
	uds_tick(now + UDS_S3_TIMEOUT_MS - 1);
	zassert_equal(uds_active_session(), UDS_SESSION_EXTENDED);
	uds_tick(now + UDS_S3_TIMEOUT_MS + 1);
	zassert_equal(uds_active_session(), UDS_SESSION_DEFAULT);
}

ZTEST(uds, test_tester_present_keeps_session_alive)
{
	enter_extended();
	for (int i = 0; i < 5; i++) {
		now += UDS_S3_TIMEOUT_MS - 1000;
		zassert_equal(send(REQ(0x3E, 0x80)), 0);
	}
	zassert_equal(uds_active_session(), UDS_SESSION_EXTENDED);
}

/* --- 0x22 ReadDataByIdentifier --- */

ZTEST(uds, test_read_fw_version)
{
	size_t len = send(REQ(0x22, 0xF1, 0x89));

	zassert_equal(len, 3 + strlen(ECU_FW_VERSION));
	zassert_equal(resp[0], 0x62);
	zassert_mem_equal(&resp[3], ECU_FW_VERSION, strlen(ECU_FW_VERSION));
}

ZTEST(uds, test_read_multiple_dids)
{
	dm_update_sensors(-15, 250, false);
	size_t len = send(REQ(0x22, 0x01, 0x00, 0x01, 0x01, 0xF1, 0x86));

	zassert_equal(len, 1 + 4 + 4 + 3);
	zassert_equal(sys_get_be16(&resp[1]), 0x0100);
	zassert_equal((int16_t)sys_get_be16(&resp[3]), -15);
	zassert_equal(sys_get_be16(&resp[5]), 0x0101);
	zassert_equal(sys_get_be16(&resp[7]), 250);
	zassert_equal(resp[11], UDS_SESSION_DEFAULT);
}

ZTEST(uds, test_read_unknown_did)
{
	assert_nrc(send(REQ(0x22, 0x12, 0x34)), 0x22, UDS_NRC_REQUEST_OUT_OF_RANGE);
}

ZTEST(uds, test_read_did_bad_length)
{
	assert_nrc(send(REQ(0x22, 0xF1)), 0x22, UDS_NRC_INCORRECT_LENGTH);
	assert_nrc(send(REQ(0x22)), 0x22, UDS_NRC_INCORRECT_LENGTH);
}

/* --- 0x27 SecurityAccess --- */

ZTEST(uds, test_security_requires_extended_session)
{
	assert_nrc(send(REQ(0x27, 0x01)), 0x27, UDS_NRC_SERVICE_NOT_IN_SESSION);
}

ZTEST(uds, test_security_unlock_and_zero_seed_when_unlocked)
{
	unlock();
	zassert_equal(send(REQ(0x27, 0x01)), 6);
	zassert_equal(sys_get_be32(&resp[2]), 0, "unlocked ECU must return a zero seed");
}

ZTEST(uds, test_security_key_without_seed)
{
	enter_extended();
	assert_nrc(send(REQ(0x27, 0x02, 0, 0, 0, 0)), 0x27, UDS_NRC_REQUEST_SEQUENCE_ERROR);
}

ZTEST(uds, test_security_invalid_key_then_lockout)
{
	enter_extended();
	for (int i = 1; i <= UDS_SECURITY_MAX_ATTEMPTS; i++) {
		zassert_equal(send(REQ(0x27, 0x01)), 6);
		size_t len = send(REQ(0x27, 0x02, 0xDE, 0xAD, 0xBE, 0xEF));

		assert_nrc(len, 0x27, i < UDS_SECURITY_MAX_ATTEMPTS ? UDS_NRC_INVALID_KEY
								    : UDS_NRC_EXCEEDED_ATTEMPTS);
	}
	assert_nrc(send(REQ(0x27, 0x01)), 0x27, UDS_NRC_REQUIRED_TIME_DELAY);

	/* Waiting out the lockout also expires S3, so re-enter the session. */
	now += UDS_SECURITY_LOCKOUT_MS;
	zassert_equal(uds_active_session(), UDS_SESSION_EXTENDED);
	enter_extended();
	zassert_equal(send(REQ(0x27, 0x01)), 6, "lockout should expire");
}

ZTEST(uds, test_session_change_relocks)
{
	unlock();
	enter_extended();
	assert_nrc(send(REQ(0x2E, 0x01, 0x04, 0x01, 0x00)), 0x2E,
		   UDS_NRC_SECURITY_ACCESS_DENIED);
}

/* --- 0x2E WriteDataByIdentifier --- */

ZTEST(uds, test_write_needs_extended_session)
{
	assert_nrc(send(REQ(0x2E, 0x01, 0x04, 0x01, 0x00)), 0x2E,
		   UDS_NRC_SERVICE_NOT_IN_SESSION);
}

ZTEST(uds, test_write_needs_security)
{
	enter_extended();
	assert_nrc(send(REQ(0x2E, 0x01, 0x04, 0x01, 0x00)), 0x2E,
		   UDS_NRC_SECURITY_ACCESS_DENIED);
}

ZTEST(uds, test_write_threshold_after_unlock)
{
	struct dm_snapshot s;

	unlock();
	zassert_equal(send(REQ(0x2E, 0x01, 0x04, 0x01, 0x2C)), 3); /* 30.0 degC */
	zassert_mem_equal(resp, ((uint8_t[]){0x6E, 0x01, 0x04}), 3);
	dm_get(&s);
	zassert_equal(s.alarm_threshold_dC, 300);
}

ZTEST(uds, test_write_rejects_out_of_range_and_unknown)
{
	unlock();
	assert_nrc(send(REQ(0x2E, 0x01, 0x03, 0x00, 0x01)), 0x2E,
		   UDS_NRC_REQUEST_OUT_OF_RANGE); /* 1 ms period */
	assert_nrc(send(REQ(0x2E, 0xF1, 0x89, 0x00, 0x01)), 0x2E,
		   UDS_NRC_REQUEST_OUT_OF_RANGE); /* read-only DID */
	assert_nrc(send(REQ(0x2E, 0x01, 0x03, 0x00)), 0x2E, UDS_NRC_INCORRECT_LENGTH);
}

/* --- 0x19 / 0x14 DTCs --- */

ZTEST(uds, test_read_dtc_count)
{
	dtc_report(DTC_SENSOR_OUT_OF_RANGE, true);
	dtc_report(DTC_CAN_BUS_OFF, true);
	dtc_report(DTC_CAN_BUS_OFF, false);

	zassert_equal(send(REQ(0x19, 0x01, 0x01)), 6); /* testFailed only */
	zassert_mem_equal(resp, ((uint8_t[]){0x59, 0x01, 0x0F, 0x01, 0x00, 0x01}), 6);

	zassert_equal(send(REQ(0x19, 0x01, 0x08)), 6); /* confirmed */
	zassert_equal(resp[5], 2);
}

ZTEST(uds, test_read_dtc_by_status_mask)
{
	dtc_report(DTC_COMMAND_CRC_ERROR, true);

	size_t len = send(REQ(0x19, 0x02, 0xFF));

	zassert_equal(len, 3 + 4);
	zassert_mem_equal(resp, ((uint8_t[]){0x59, 0x02, 0x0F, 0xC4, 0x14, 0x81, 0x0F}), 7);
}

ZTEST(uds, test_read_supported_dtcs)
{
	zassert_equal(send(REQ(0x19, 0x0A)), 3 + 4 * DTC_COUNT);
	zassert_equal(resp[3], 0x0A);
	zassert_equal(resp[6], 0x00, "status of a never-failed DTC");
}

ZTEST(uds, test_read_dtc_bad_requests)
{
	assert_nrc(send(REQ(0x19, 0x04, 0x00)), 0x19, UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
	assert_nrc(send(REQ(0x19, 0x02)), 0x19, UDS_NRC_INCORRECT_LENGTH);
	assert_nrc(send(REQ(0x19)), 0x19, UDS_NRC_INCORRECT_LENGTH);
}

ZTEST(uds, test_clear_dtc)
{
	dtc_report(DTC_SENSOR_OUT_OF_RANGE, true);
	zassert_equal(send(REQ(0x14, 0xFF, 0xFF, 0xFF)), 1);
	zassert_equal(resp[0], 0x54);
	zassert_equal(dtc_count_by_mask(0xFF), 0);
}

ZTEST(uds, test_clear_dtc_bad_group_and_length)
{
	assert_nrc(send(REQ(0x14, 0x00, 0x00, 0x01)), 0x14, UDS_NRC_REQUEST_OUT_OF_RANGE);
	assert_nrc(send(REQ(0x14, 0xFF, 0xFF)), 0x14, UDS_NRC_INCORRECT_LENGTH);
}

ZTEST_SUITE(uds, NULL, NULL, before, NULL, NULL);
