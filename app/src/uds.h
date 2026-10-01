/*
 * UDS (ISO 14229-1) service handlers. Transport independent: the ISO-TP
 * glue in uds_server.c feeds complete requests in and sends the response
 * out, which lets the unit tests drive every service and NRC directly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UDS_H_
#define UDS_H_

#include <stddef.h>
#include <stdint.h>

/* Service IDs */
#define UDS_SID_DIAG_SESSION_CONTROL 0x10
#define UDS_SID_CLEAR_DTC            0x14
#define UDS_SID_READ_DTC             0x19
#define UDS_SID_READ_DID             0x22
#define UDS_SID_SECURITY_ACCESS      0x27
#define UDS_SID_WRITE_DID            0x2E
#define UDS_SID_TESTER_PRESENT       0x3E
#define UDS_NEGATIVE_RESPONSE        0x7F
#define UDS_POSITIVE_OFFSET          0x40

/* Negative response codes */
#define UDS_NRC_SERVICE_NOT_SUPPORTED          0x11
#define UDS_NRC_SUBFUNCTION_NOT_SUPPORTED      0x12
#define UDS_NRC_INCORRECT_LENGTH               0x13
#define UDS_NRC_RESPONSE_TOO_LONG              0x14
#define UDS_NRC_CONDITIONS_NOT_CORRECT         0x22
#define UDS_NRC_REQUEST_SEQUENCE_ERROR         0x24
#define UDS_NRC_REQUEST_OUT_OF_RANGE           0x31
#define UDS_NRC_SECURITY_ACCESS_DENIED         0x33
#define UDS_NRC_INVALID_KEY                    0x35
#define UDS_NRC_EXCEEDED_ATTEMPTS              0x36
#define UDS_NRC_REQUIRED_TIME_DELAY            0x37
#define UDS_NRC_SERVICE_NOT_IN_SESSION         0x7F

/* Sessions */
#define UDS_SESSION_DEFAULT  0x01
#define UDS_SESSION_EXTENDED 0x03

/* Data identifiers */
#define UDS_DID_ACTIVE_SESSION   0xF186
#define UDS_DID_FW_VERSION       0xF189
#define UDS_DID_SERIAL_NUMBER    0xF18C
#define UDS_DID_TEMPERATURE      0x0100
#define UDS_DID_VIBRATION        0x0101
#define UDS_DID_STATUS_FLAGS     0x0102
#define UDS_DID_SAMPLE_PERIOD    0x0103
#define UDS_DID_ALARM_THRESHOLD  0x0104
#define UDS_DID_UPTIME           0x0105

#define UDS_MAX_MESSAGE 128

void uds_init(uint32_t seed);

/*
 * Handle one complete request. Writes the response into resp and returns
 * its length; 0 means no response (suppressPosRspMsgIndicationBit set).
 */
size_t uds_process(const uint8_t *req, size_t req_len, uint8_t *resp, size_t resp_size,
		   int64_t now_ms);

/* Fall back to the default session when S3 expires. Call periodically. */
void uds_tick(int64_t now_ms);

uint8_t uds_active_session(void);

/*
 * Demo seed/key algorithm. It is deliberately simple and public: real ECUs
 * use a secret, usually HSM backed, algorithm.
 */
uint32_t uds_compute_key(uint32_t seed);

#endif /* UDS_H_ */
