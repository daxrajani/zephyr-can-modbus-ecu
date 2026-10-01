"""UDS over ISO-TP on vcan0, driven by udsoncan exactly as host/uds_tester.py does."""

from __future__ import annotations

import time

import pytest
import udsoncan
from udsoncan.services import DiagnosticSessionControl

import ecu_common as ecu
from conftest import CommandSender, requires_vcan

pytestmark = requires_vcan

EXTENDED = DiagnosticSessionControl.Session.extendedDiagnosticSession


def test_read_version_and_serial(uds):
    assert uds.read_data_by_identifier_first(ecu.DID_FW_VERSION) == "1.0.0"
    assert uds.read_data_by_identifier_first(ecu.DID_SERIAL_NUMBER) == "ZCM-ECU-000001"


def test_read_live_values(uds):
    values = uds.read_data_by_identifier(
        [ecu.DID_TEMPERATURE, ecu.DID_VIBRATION, ecu.DID_SAMPLE_PERIOD,
         ecu.DID_ALARM_THRESHOLD, ecu.DID_UPTIME]
    ).service_data.values
    assert 150 <= values[ecu.DID_TEMPERATURE] <= 350
    assert 100 <= values[ecu.DID_VIBRATION] <= 300
    assert values[ecu.DID_SAMPLE_PERIOD] == 100
    assert values[ecu.DID_ALARM_THRESHOLD] == 800
    assert values[ecu.DID_UPTIME] >= 0


def test_session_control_and_s3_timeout(uds):
    assert uds.read_data_by_identifier_first(ecu.DID_ACTIVE_SESSION) == 0x01
    response = uds.change_session(EXTENDED)
    assert response.service_data.p2_server_max == pytest.approx(0.05)
    assert uds.read_data_by_identifier_first(ecu.DID_ACTIVE_SESSION) == 0x03

    # TesterPresent keeps the session past S3 (5 s)
    for _ in range(4):
        time.sleep(2.0)
        uds.tester_present()
    assert uds.read_data_by_identifier_first(ecu.DID_ACTIVE_SESSION) == 0x03

    # Silence for longer than S3 drops back to the default session
    time.sleep(5.5)
    assert uds.read_data_by_identifier_first(ecu.DID_ACTIVE_SESSION) == 0x01


def _nrc(response) -> int:
    assert not response.positive, f"expected NRC, got {response}"
    return response.code


def _raw(client, request: bytes) -> bytes:
    client.conn.empty_rxqueue()
    client.conn.send(request)
    return client.conn.wait_frame(timeout=1)


def test_negative_response_codes(uds_raw):
    c = uds_raw
    assert _raw(c, bytes.fromhex("221234")) == bytes.fromhex("7f2231")  # unknown DID: requestOutOfRange
    assert _raw(c, bytes.fromhex("22f1")) == bytes.fromhex("7f2213")  # incorrectMessageLength
    assert _raw(c, bytes.fromhex("1002")) == bytes.fromhex("7f1012")  # programming session not supported
    assert _nrc(c.write_data_by_identifier(ecu.DID_ALARM_THRESHOLD, 300)) == 0x7F  # not in session
    c.change_session(EXTENDED)
    assert _nrc(c.write_data_by_identifier(ecu.DID_ALARM_THRESHOLD, 300)) == 0x33  # locked
    assert _nrc(c.clear_dtc(0x000001)) == 0x31


def test_unknown_service_is_rejected(uds_raw):
    # RoutineControl is not implemented: serviceNotSupported
    assert _raw(uds_raw, bytes.fromhex("3101ff00")) == bytes.fromhex("7f3111")


def test_invalid_key_is_rejected(uds_raw):
    c = uds_raw
    c.change_session(EXTENDED)
    seed = c.request_seed(1).service_data.seed
    assert len(seed) == 4
    response = c.send_key(1, b"\x00\x00\x00\x00")
    assert _nrc(response) == 0x35  # invalidKey


def test_security_unlock_then_write(uds):
    uds.change_session(EXTENDED)
    uds.unlock_security_access(1)
    uds.write_data_by_identifier(ecu.DID_ALARM_THRESHOLD, 255)  # 25.5 degC
    uds.write_data_by_identifier(ecu.DID_SAMPLE_PERIOD, 50)
    assert uds.read_data_by_identifier_first(ecu.DID_ALARM_THRESHOLD) == 255
    assert uds.read_data_by_identifier_first(ecu.DID_SAMPLE_PERIOD) == 50


def test_dtc_lifecycle(uds, can_bus):
    """Fault -> DTC confirmed and active -> fault gone -> confirmed only -> cleared."""
    commander = CommandSender(can_bus)
    assert uds.get_number_of_dtc_by_status_mask(0xFF).service_data.dtc_count == 0

    commander.send(fault_injection=True)
    time.sleep(0.5)
    dtcs = uds.get_dtc_by_status_mask(0x01).service_data.dtcs
    assert [d.id for d in dtcs] == [ecu.DTC_SENSOR_OUT_OF_RANGE]
    assert dtcs[0].status.test_failed and dtcs[0].status.confirmed

    commander.send(fault_injection=False)
    time.sleep(0.5)
    assert uds.get_number_of_dtc_by_status_mask(0x01).service_data.dtc_count == 0
    stored = uds.get_dtc_by_status_mask(0x08).service_data.dtcs
    assert [d.id for d in stored] == [ecu.DTC_SENSOR_OUT_OF_RANGE]

    uds.clear_dtc(0xFFFFFF)
    assert uds.get_number_of_dtc_by_status_mask(0xFF).service_data.dtc_count == 0


def test_supported_dtcs(uds):
    dtcs = uds.get_supported_dtc().service_data.dtcs
    assert {d.id for d in dtcs} == set(ecu.DTC_NAMES)


def test_crc_error_sets_dtc(uds, can_bus):
    CommandSender(can_bus).send(alarm_threshold_dC=100, corrupt_crc=True)
    time.sleep(0.3)
    ids = [d.id for d in uds.get_dtc_by_status_mask(0x08).service_data.dtcs]
    assert ecu.DTC_COMMAND_CRC_ERROR in ids


def test_multi_frame_response_back_to_back(uds):
    """Multi-frame responses followed immediately by further requests.

    Exercises the request-binding release in uds_server.c."""
    for _ in range(20):
        assert uds.read_data_by_identifier_first(ecu.DID_SERIAL_NUMBER) == "ZCM-ECU-000001"
        assert uds.read_data_by_identifier_first(ecu.DID_ACTIVE_SESSION) == 0x01
