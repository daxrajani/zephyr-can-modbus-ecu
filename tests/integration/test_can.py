"""CAN front end on vcan0: periodic frames, DBC decoding, E2E checks, commands."""

from __future__ import annotations

import statistics
import time

import pytest

import ecu_common as ecu
from conftest import collect, requires_vcan, wait_status

pytestmark = requires_vcan


def _period_stats(frames) -> dict:
    gaps = [(b.timestamp - a.timestamp) * 1000 for a, b in zip(frames, frames[1:])]
    gaps_sorted = sorted(gaps)
    return {
        "frames": len(frames),
        "mean_ms": round(statistics.mean(gaps), 3),
        "stdev_ms": round(statistics.pstdev(gaps), 3),
        "p99_abs_error_ms": round(
            sorted(abs(g - statistics.mean(gaps)) for g in gaps)[int(0.99 * len(gaps)) - 1], 3),
        "min_ms": round(gaps_sorted[0], 3),
        "max_ms": round(gaps_sorted[-1], 3),
    }


def test_sensor_status_period(can_bus, metrics):
    frames = collect(can_bus, ecu.ID_SENSOR_STATUS, 5.0)
    stats = _period_stats(frames)
    metrics["sensor_status_period"] = stats
    assert stats["frames"] >= 45
    assert abs(stats["mean_ms"] - ecu.SENSOR_STATUS_PERIOD_MS) < 2.0
    assert stats["max_ms"] < ecu.SENSOR_STATUS_PERIOD_MS + 25


def test_heartbeat_period(can_bus, metrics):
    frames = collect(can_bus, ecu.ID_HEARTBEAT, 5.5)
    stats = _period_stats(frames)
    metrics["heartbeat_period"] = stats
    assert stats["frames"] >= 5
    assert abs(stats["mean_ms"] - ecu.HEARTBEAT_PERIOD_MS) < 10.0


def test_sensor_status_decodes_with_dbc_and_e2e_holds(can_bus, dbc):
    frames = collect(can_bus, ecu.ID_SENSOR_STATUS, 2.5)
    assert len(frames) >= 20
    counters = []
    for msg in frames:
        assert msg.dlc == 8
        assert ecu.frame_crc_ok(bytes(msg.data)), f"bad CRC: {bytes(msg.data).hex()}"
        s = dbc.decode_message(msg.arbitration_id, msg.data)
        assert 15.0 <= s["Temperature"] <= 35.0
        assert 1.0 <= s["VibrationRms"] <= 3.0
        assert s["AlarmActive"] == 0 and s["SensorFault"] == 0
        counters.append(s["RollingCounter"])
    for a, b in zip(counters, counters[1:]):
        assert b == (a + 1) % 16, f"counter {a} -> {b}"


def test_heartbeat_reports_run_state(can_bus, dbc):
    msg = collect(can_bus, ecu.ID_HEARTBEAT, 1.2)[0]
    hb = dbc.decode_message(msg.arbitration_id, msg.data, decode_choices=False)
    assert hb["FirmwareState"] == 1  # RUN
    assert hb["ActiveDtcCount"] == 0


def test_command_threshold_drives_alarm(can_bus, commander, dbc):
    commander.send(alarm_threshold_dC=100)  # 10.0 degC, below ambient
    wait_status(can_bus, dbc, lambda s: s["AlarmActive"] == 1)

    commander.send(alarm_threshold_dC=800, alarm_reset=True)
    wait_status(can_bus, dbc, lambda s: s["AlarmActive"] == 0)


def test_command_latency(can_bus, commander, dbc, metrics):
    """Time from ECU_COMMAND on the bus to the first SENSOR_STATUS showing it."""
    samples = []
    for i in range(10):
        low = i % 2 == 0
        t0 = time.monotonic()
        commander.send(alarm_threshold_dC=100 if low else 800, alarm_reset=not low)
        wait_status(can_bus, dbc, lambda s: s["AlarmActive"] == (1 if low else 0))
        samples.append((time.monotonic() - t0) * 1000)
    metrics["command_to_status_ms"] = {
        "cycles": len(samples),
        "mean": round(statistics.mean(samples), 2),
        "max": round(max(samples), 2),
    }
    # Bounded by one SENSOR_STATUS period plus scheduling
    assert max(samples) < ecu.SENSOR_STATUS_PERIOD_MS + 50


def test_command_with_bad_crc_is_rejected(can_bus, commander, ecu_proc, dbc):
    commander.send(alarm_threshold_dC=100, corrupt_crc=True)
    ecu_proc.wait_for_log(r"ECU_COMMAND rejected: CRC error")
    time.sleep(0.3)
    assert all(
        dbc.decode_message(m.arbitration_id, m.data)["AlarmActive"] == 0
        for m in collect(can_bus, ecu.ID_SENSOR_STATUS, 0.5)
    )


def test_repeated_counter_is_rejected(can_bus, commander, ecu_proc, dbc):
    commander.send(alarm_threshold_dC=100, counter=5)
    wait_status(can_bus, dbc, lambda s: s["AlarmActive"] == 1)
    # Same counter again: a replayed frame must be ignored
    commander.send(alarm_threshold_dC=800, alarm_reset=True, counter=5)
    ecu_proc.wait_for_log(r"rejected: repeated counter 5")
    time.sleep(0.3)
    assert dbc.decode_message(
        ecu.ID_SENSOR_STATUS, collect(can_bus, ecu.ID_SENSOR_STATUS, 0.3)[-1].data
    )["AlarmActive"] == 1


def test_wrong_dlc_is_rejected(can_bus, ecu_proc):
    import can

    can_bus.send(can.Message(arbitration_id=ecu.ID_ECU_COMMAND, data=b"\x00\x01",
                             is_extended_id=False))
    ecu_proc.wait_for_log(r"ECU_COMMAND rejected: DLC 2")


def test_fault_injection_degrades_ecu(can_bus, commander, dbc):
    commander.send(fault_injection=True)
    s = wait_status(can_bus, dbc, lambda s: s["SensorFault"] == 1)
    assert s["Temperature"] == pytest.approx(150.0)
    assert s["VibrationRms"] > 8.0

    hb_msg = collect(can_bus, ecu.ID_HEARTBEAT, 1.2)[-1]
    hb = dbc.decode_message(hb_msg.arbitration_id, hb_msg.data, decode_choices=False)
    assert hb["FirmwareState"] == 2  # DEGRADED
    assert hb["ActiveDtcCount"] == 1

    commander.send(fault_injection=False)
    wait_status(can_bus, dbc, lambda s: s["SensorFault"] == 0)
