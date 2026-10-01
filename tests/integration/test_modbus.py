"""Modbus RTU server over the native_sim pseudo terminal (no vcan needed)."""

from __future__ import annotations

import statistics
import time

import pytest
from pymodbus.exceptions import ModbusIOException

import ecu_common as ecu


def test_input_registers_are_plausible(modbus):
    values = modbus.read_inputs()
    assert 15.0 <= values["temperature_c"] <= 35.0
    assert 1.0 <= values["vibration_mm_s"] <= 3.0
    assert values["alarm"] is False
    assert values["sensor_fault"] is False
    assert values["active_dtcs"] == 0


def test_uptime_counts_up(modbus):
    first = modbus.read_inputs()["uptime_s"]
    time.sleep(2.2)
    assert modbus.read_inputs()["uptime_s"] >= first + 2


def test_holding_registers_default_and_write(modbus):
    assert modbus.read_holding() == {"sample_period_ms": 100, "alarm_threshold_c": 80.0}
    modbus.set_sample_period(250)
    modbus.set_threshold(-12.5)
    assert modbus.read_holding() == {"sample_period_ms": 250, "alarm_threshold_c": -12.5}


def test_out_of_range_write_is_rejected(modbus):
    with pytest.raises(IOError):
        modbus.set_sample_period(5)
    with pytest.raises(IOError):
        modbus.set_threshold(200.0)
    assert modbus.read_holding() == {"sample_period_ms": 100, "alarm_threshold_c": 80.0}


def test_illegal_address_returns_exception(modbus):
    rr = modbus.client.read_input_registers(10, count=1, device_id=ecu.MODBUS_UNIT_ID)
    assert rr.isError()
    assert rr.exception_code == 0x02  # ILLEGAL DATA ADDRESS


def test_other_unit_id_is_ignored(modbus):
    # A server must stay silent for frames addressed to another unit
    with pytest.raises(ModbusIOException):
        modbus.client.read_input_registers(0, count=1, device_id=7)
    # ...and still answer its own unit afterwards
    assert modbus.read_holding()["sample_period_ms"] == 100


def test_threshold_write_raises_latched_alarm_and_coil_resets(modbus):
    modbus.set_threshold(5.0)
    assert modbus.read_inputs()["alarm"] is True

    modbus.set_threshold(80.0)
    assert modbus.read_inputs()["alarm"] is True, "alarm must latch"

    modbus.reset_alarm()
    assert modbus.read_inputs()["alarm"] is False


def test_fault_injection_coil(modbus):
    modbus.set_fault_injection(True)
    time.sleep(0.5)
    values = modbus.read_inputs()
    assert values["sensor_fault"] is True
    assert values["temperature_c"] == 150.0
    assert values["active_dtcs"] >= 1

    modbus.set_fault_injection(False)
    time.sleep(0.5)
    assert modbus.read_inputs()["sensor_fault"] is False


def test_response_time(modbus, metrics):
    samples = []
    for _ in range(50):
        t0 = time.perf_counter()
        modbus.client.read_input_registers(0, count=5, device_id=ecu.MODBUS_UNIT_ID)
        samples.append((time.perf_counter() - t0) * 1000)
    samples.sort()
    metrics["modbus_response_ms"] = {
        "transactions": len(samples),
        "mean": round(statistics.mean(samples), 2),
        "p95": round(samples[int(0.95 * len(samples)) - 1], 2),
        "max": round(samples[-1], 2),
    }
    # 5 ms framing gap on the PTY plus host scheduling; generous bound
    assert samples[int(0.95 * len(samples)) - 1] < 50.0
