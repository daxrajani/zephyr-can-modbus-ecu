/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <string.h>

#include "data_model.h"
#include "dtc.h"
#include "ecu_config.h"
#include "uds.h"

#define SUPPRESS_POS_RSP   0x80
#define MAX_DIDS_PER_READ  8
#define DTC_FORMAT_14229_1 0x01

/* P2 = 50 ms, P2* = 5000 ms (sent in 10 ms units) */
#define P2_SERVER_MS       50
#define P2_STAR_SERVER_10MS 500

static struct {
	uint8_t session;
	int64_t last_request_ms;
	bool unlocked;
	bool seed_pending;
	uint32_t seed;
	uint8_t failed_attempts;
	int64_t lockout_until_ms;
	uint32_t rng;
} uds;

static uint32_t rng_next(void)
{
	uint32_t x = uds.rng;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	uds.rng = x;
	return x;
}

uint32_t uds_compute_key(uint32_t seed)
{
	uint32_t x = seed ^ 0x5A3C96E1u;

	return ((x << 7) | (x >> 25)) + 0x1F2E3D4Cu;
}

void uds_init(uint32_t seed)
{
	memset(&uds, 0, sizeof(uds));
	uds.session = UDS_SESSION_DEFAULT;
	uds.rng = seed ? seed : 0xACE1u;
}

uint8_t uds_active_session(void)
{
	return uds.session;
}

static void enter_session(uint8_t session)
{
	uds.session = session;
	/* Any session change relocks the ECU (ISO 14229-1 9.2.1). */
	uds.unlocked = false;
	uds.seed_pending = false;
}

void uds_tick(int64_t now_ms)
{
	if (uds.session != UDS_SESSION_DEFAULT &&
	    now_ms - uds.last_request_ms > UDS_S3_TIMEOUT_MS) {
		enter_session(UDS_SESSION_DEFAULT);
	}
}

static size_t nrc(uint8_t *resp, uint8_t sid, uint8_t code)
{
	resp[0] = UDS_NEGATIVE_RESPONSE;
	resp[1] = sid;
	resp[2] = code;
	return 3;
}

static void put_be16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

static void put_be32(uint8_t *p, uint32_t v)
{
	put_be16(p, (uint16_t)(v >> 16));
	put_be16(p + 2, (uint16_t)v);
}

/* 0x10 DiagnosticSessionControl */
static size_t diag_session_control(const uint8_t *req, size_t len, uint8_t *resp)
{
	if (len != 2) {
		return nrc(resp, req[0], UDS_NRC_INCORRECT_LENGTH);
	}

	uint8_t sub = req[1] & ~SUPPRESS_POS_RSP;

	if (sub != UDS_SESSION_DEFAULT && sub != UDS_SESSION_EXTENDED) {
		return nrc(resp, req[0], UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
	}

	enter_session(sub);

	if (req[1] & SUPPRESS_POS_RSP) {
		return 0;
	}
	resp[0] = req[0] + UDS_POSITIVE_OFFSET;
	resp[1] = sub;
	put_be16(&resp[2], P2_SERVER_MS);
	put_be16(&resp[4], P2_STAR_SERVER_10MS);
	return 6;
}

/* 0x3E TesterPresent */
static size_t tester_present(const uint8_t *req, size_t len, uint8_t *resp)
{
	if (len != 2) {
		return nrc(resp, req[0], UDS_NRC_INCORRECT_LENGTH);
	}
	if ((req[1] & ~SUPPRESS_POS_RSP) != 0x00) {
		return nrc(resp, req[0], UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
	}
	if (req[1] & SUPPRESS_POS_RSP) {
		return 0;
	}
	resp[0] = req[0] + UDS_POSITIVE_OFFSET;
	resp[1] = 0x00;
	return 2;
}

/*
 * Write one DID record (identifier + value) at out. Returns bytes written,
 * 0 for an unknown DID or -1 when it does not fit.
 */
static int read_did(uint16_t did, uint8_t *out, size_t room)
{
	struct dm_snapshot s;
	uint8_t value[24];
	size_t vlen;

	dm_get(&s);

	switch (did) {
	case UDS_DID_ACTIVE_SESSION:
		value[0] = uds.session;
		vlen = 1;
		break;
	case UDS_DID_FW_VERSION:
		vlen = strlen(ECU_FW_VERSION);
		memcpy(value, ECU_FW_VERSION, vlen);
		break;
	case UDS_DID_SERIAL_NUMBER:
		vlen = strlen(ECU_SERIAL_NUMBER);
		memcpy(value, ECU_SERIAL_NUMBER, vlen);
		break;
	case UDS_DID_TEMPERATURE:
		put_be16(value, (uint16_t)s.temperature_dC);
		vlen = 2;
		break;
	case UDS_DID_VIBRATION:
		put_be16(value, s.vibration_cmm_s);
		vlen = 2;
		break;
	case UDS_DID_STATUS_FLAGS:
		value[0] = (s.alarm_active ? 0x01 : 0) | (s.sensor_fault ? 0x02 : 0) |
			   (s.fault_injection ? 0x04 : 0);
		vlen = 1;
		break;
	case UDS_DID_SAMPLE_PERIOD:
		put_be16(value, s.sample_period_ms);
		vlen = 2;
		break;
	case UDS_DID_ALARM_THRESHOLD:
		put_be16(value, (uint16_t)s.alarm_threshold_dC);
		vlen = 2;
		break;
	case UDS_DID_UPTIME:
		put_be32(value, s.uptime_s);
		vlen = 4;
		break;
	default:
		return 0;
	}

	if (vlen + 2 > room) {
		return -1;
	}
	put_be16(out, did);
	memcpy(out + 2, value, vlen);
	return (int)(vlen + 2);
}

/* 0x22 ReadDataByIdentifier, one or more DIDs per request */
static size_t read_data_by_id(const uint8_t *req, size_t len, uint8_t *resp, size_t resp_size)
{
	size_t n_dids = (len - 1) / 2;

	if (len < 3 || ((len - 1) % 2) != 0 || n_dids > MAX_DIDS_PER_READ) {
		return nrc(resp, req[0], UDS_NRC_INCORRECT_LENGTH);
	}

	size_t pos = 1;

	resp[0] = req[0] + UDS_POSITIVE_OFFSET;
	for (size_t i = 0; i < n_dids; i++) {
		uint16_t did = (uint16_t)((req[1 + 2 * i] << 8) | req[2 + 2 * i]);
		int written = read_did(did, &resp[pos], resp_size - pos);

		if (written == 0) {
			return nrc(resp, req[0], UDS_NRC_REQUEST_OUT_OF_RANGE);
		}
		if (written < 0) {
			return nrc(resp, req[0], UDS_NRC_RESPONSE_TOO_LONG);
		}
		pos += (size_t)written;
	}
	return pos;
}

/* 0x2E WriteDataByIdentifier: extended session and unlocked ECU only */
static size_t write_data_by_id(const uint8_t *req, size_t len, uint8_t *resp)
{
	if (len < 4) {
		return nrc(resp, req[0], UDS_NRC_INCORRECT_LENGTH);
	}
	if (uds.session != UDS_SESSION_EXTENDED) {
		return nrc(resp, req[0], UDS_NRC_SERVICE_NOT_IN_SESSION);
	}

	uint16_t did = (uint16_t)((req[1] << 8) | req[2]);

	if (did != UDS_DID_SAMPLE_PERIOD && did != UDS_DID_ALARM_THRESHOLD) {
		return nrc(resp, req[0], UDS_NRC_REQUEST_OUT_OF_RANGE);
	}
	if (len != 5) {
		return nrc(resp, req[0], UDS_NRC_INCORRECT_LENGTH);
	}
	if (!uds.unlocked) {
		return nrc(resp, req[0], UDS_NRC_SECURITY_ACCESS_DENIED);
	}

	uint16_t raw = (uint16_t)((req[3] << 8) | req[4]);
	int err = (did == UDS_DID_SAMPLE_PERIOD) ? dm_set_sample_period(raw)
						 : dm_set_alarm_threshold((int16_t)raw);

	if (err) {
		return nrc(resp, req[0], UDS_NRC_REQUEST_OUT_OF_RANGE);
	}

	resp[0] = req[0] + UDS_POSITIVE_OFFSET;
	put_be16(&resp[1], did);
	return 3;
}

/* 0x27 SecurityAccess, level 1 (0x01 seed / 0x02 key) */
static size_t security_access(const uint8_t *req, size_t len, uint8_t *resp, int64_t now_ms)
{
	if (len < 2) {
		return nrc(resp, req[0], UDS_NRC_INCORRECT_LENGTH);
	}

	uint8_t sub = req[1] & ~SUPPRESS_POS_RSP;

	if (sub != 0x01 && sub != 0x02) {
		return nrc(resp, req[0], UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
	}
	if (uds.session != UDS_SESSION_EXTENDED) {
		return nrc(resp, req[0], UDS_NRC_SERVICE_NOT_IN_SESSION);
	}

	resp[0] = req[0] + UDS_POSITIVE_OFFSET;
	resp[1] = sub;

	if (sub == 0x01) {
		if (len != 2) {
			return nrc(resp, req[0], UDS_NRC_INCORRECT_LENGTH);
		}
		if (now_ms < uds.lockout_until_ms) {
			return nrc(resp, req[0], UDS_NRC_REQUIRED_TIME_DELAY);
		}
		if (uds.unlocked) {
			/* Already unlocked: ISO 14229 answers with an all-zero seed. */
			put_be32(&resp[2], 0);
			return 6;
		}
		do {
			uds.seed = rng_next();
		} while (uds.seed == 0);
		uds.seed_pending = true;
		put_be32(&resp[2], uds.seed);
		return 6;
	}

	/* sub == 0x02, sendKey */
	if (len != 6) {
		return nrc(resp, req[0], UDS_NRC_INCORRECT_LENGTH);
	}
	if (!uds.seed_pending) {
		return nrc(resp, req[0], UDS_NRC_REQUEST_SEQUENCE_ERROR);
	}

	uint32_t key = ((uint32_t)req[2] << 24) | ((uint32_t)req[3] << 16) |
		       ((uint32_t)req[4] << 8) | req[5];

	uds.seed_pending = false;
	if (key != uds_compute_key(uds.seed)) {
		if (++uds.failed_attempts >= UDS_SECURITY_MAX_ATTEMPTS) {
			uds.failed_attempts = 0;
			uds.lockout_until_ms = now_ms + UDS_SECURITY_LOCKOUT_MS;
			return nrc(resp, req[0], UDS_NRC_EXCEEDED_ATTEMPTS);
		}
		return nrc(resp, req[0], UDS_NRC_INVALID_KEY);
	}

	uds.failed_attempts = 0;
	uds.unlocked = true;
	return 2;
}

static size_t put_dtc_records(uint8_t mask, uint8_t *resp, size_t pos, size_t resp_size)
{
	struct dtc_record recs[DTC_COUNT];
	size_t n = dtc_get_by_mask(mask, recs, DTC_COUNT);

	for (size_t i = 0; i < n && pos + 4 <= resp_size; i++) {
		resp[pos++] = (uint8_t)(recs[i].code >> 16);
		resp[pos++] = (uint8_t)(recs[i].code >> 8);
		resp[pos++] = (uint8_t)recs[i].code;
		resp[pos++] = recs[i].status;
	}
	return pos;
}

/* 0x19 ReadDTCInformation: 0x01, 0x02 and 0x0A */
static size_t read_dtc_info(const uint8_t *req, size_t len, uint8_t *resp, size_t resp_size)
{
	if (len < 2) {
		return nrc(resp, req[0], UDS_NRC_INCORRECT_LENGTH);
	}

	uint8_t sub = req[1];

	resp[0] = req[0] + UDS_POSITIVE_OFFSET;
	resp[1] = sub;
	resp[2] = DTC_STATUS_AVAILABILITY_MASK;

	switch (sub) {
	case 0x01: { /* reportNumberOfDTCByStatusMask */
		if (len != 3) {
			return nrc(resp, req[0], UDS_NRC_INCORRECT_LENGTH);
		}
		size_t count = dtc_count_by_mask(req[2] & DTC_STATUS_AVAILABILITY_MASK);

		resp[3] = DTC_FORMAT_14229_1;
		put_be16(&resp[4], (uint16_t)count);
		return 6;
	}
	case 0x02: /* reportDTCByStatusMask */
		if (len != 3) {
			return nrc(resp, req[0], UDS_NRC_INCORRECT_LENGTH);
		}
		return put_dtc_records(req[2] & DTC_STATUS_AVAILABILITY_MASK, resp, 3, resp_size);
	case 0x0A: /* reportSupportedDTC */
		if (len != 2) {
			return nrc(resp, req[0], UDS_NRC_INCORRECT_LENGTH);
		}
		/* Every supported DTC, including those with status 0x00 */
		{
			size_t pos = 3;

			for (int i = 0; i < DTC_COUNT && pos + 4 <= resp_size; i++) {
				uint32_t code = dtc_code(i);

				resp[pos++] = (uint8_t)(code >> 16);
				resp[pos++] = (uint8_t)(code >> 8);
				resp[pos++] = (uint8_t)code;
				resp[pos++] = dtc_status(i);
			}
			return pos;
		}
	default:
		return nrc(resp, req[0], UDS_NRC_SUBFUNCTION_NOT_SUPPORTED);
	}
}

/* 0x14 ClearDiagnosticInformation, groupOfDTC 0xFFFFFF (all) only */
static size_t clear_dtc(const uint8_t *req, size_t len, uint8_t *resp)
{
	if (len != 4) {
		return nrc(resp, req[0], UDS_NRC_INCORRECT_LENGTH);
	}

	uint32_t group = ((uint32_t)req[1] << 16) | ((uint32_t)req[2] << 8) | req[3];

	if (group != 0xFFFFFF) {
		return nrc(resp, req[0], UDS_NRC_REQUEST_OUT_OF_RANGE);
	}
	dtc_clear_all();
	resp[0] = req[0] + UDS_POSITIVE_OFFSET;
	return 1;
}

size_t uds_process(const uint8_t *req, size_t req_len, uint8_t *resp, size_t resp_size,
		   int64_t now_ms)
{
	if (req_len == 0 || resp_size < UDS_MAX_MESSAGE) {
		return 0;
	}

	/* S3 is checked before the request restarts it. */
	uds_tick(now_ms);
	uds.last_request_ms = now_ms;

	switch (req[0]) {
	case UDS_SID_DIAG_SESSION_CONTROL:
		return diag_session_control(req, req_len, resp);
	case UDS_SID_TESTER_PRESENT:
		return tester_present(req, req_len, resp);
	case UDS_SID_READ_DID:
		return read_data_by_id(req, req_len, resp, resp_size);
	case UDS_SID_WRITE_DID:
		return write_data_by_id(req, req_len, resp);
	case UDS_SID_SECURITY_ACCESS:
		return security_access(req, req_len, resp, now_ms);
	case UDS_SID_READ_DTC:
		return read_dtc_info(req, req_len, resp, resp_size);
	case UDS_SID_CLEAR_DTC:
		return clear_dtc(req, req_len, resp);
	default:
		return nrc(resp, req[0], UDS_NRC_SERVICE_NOT_SUPPORTED);
	}
}
