"""The key design point: one data model behind two buses.

A threshold written over Modbus changes the alarm seen on CAN, a CAN command
is visible to the Modbus master, and a Modbus master going silent shows up
as a UDS DTC.
"""

from __future__ import annotations

import time

import ecu_common as ecu
from conftest import requires_vcan, wait_status

pytestmark = requires_vcan


def test_modbus_threshold_write_raises_can_alarm(modbus, can_bus, dbc):
    wait_status(can_bus, dbc, lambda s: s["AlarmActive"] == 0)
    modbus.set_threshold(10.0)
    wait_status(can_bus, dbc, lambda s: s["AlarmActive"] == 1, timeout=0.5)


def test_can_command_is_visible_over_modbus(modbus, commander):
    commander.send(sample_period_ms=40, alarm_threshold_dC=-55)
    time.sleep(0.2)
    assert modbus.read_holding() == {"sample_period_ms": 40, "alarm_threshold_c": -5.5}
    assert modbus.read_inputs()["alarm"] is True


def test_modbus_coil_resets_alarm_raised_over_can(modbus, commander, can_bus, dbc):
    commander.send(alarm_threshold_dC=100)
    wait_status(can_bus, dbc, lambda s: s["AlarmActive"] == 1)
    commander.send(alarm_threshold_dC=800)
    modbus.reset_alarm()
    wait_status(can_bus, dbc, lambda s: s["AlarmActive"] == 0, timeout=0.5)


def test_uds_write_is_visible_over_modbus(modbus, uds):
    from udsoncan.services import DiagnosticSessionControl

    uds.change_session(DiagnosticSessionControl.Session.extendedDiagnosticSession)
    uds.unlock_security_access(1)
    uds.write_data_by_identifier(ecu.DID_SAMPLE_PERIOD, 500)
    assert modbus.read_holding()["sample_period_ms"] == 500


def test_modbus_master_timeout_sets_dtc(modbus, uds):
    modbus.read_inputs()  # the master has been seen
    assert uds.get_number_of_dtc_by_status_mask(0x01).service_data.dtc_count == 0

    time.sleep(ecu_timeout_s() + 1.0)
    active = uds.get_dtc_by_status_mask(0x01).service_data.dtcs
    assert [d.id for d in active] == [ecu.DTC_MODBUS_MASTER_TIMEOUT]

    modbus.read_inputs()  # master is back
    time.sleep(0.3)
    assert uds.get_number_of_dtc_by_status_mask(0x01).service_data.dtc_count == 0
    stored = [d.id for d in uds.get_dtc_by_status_mask(0x08).service_data.dtcs]
    assert ecu.DTC_MODBUS_MASTER_TIMEOUT in stored


def ecu_timeout_s() -> float:
    return 5.0  # MODBUS_MASTER_TIMEOUT_MS in app/src/ecu_config.h
