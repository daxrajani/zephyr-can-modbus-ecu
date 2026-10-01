"""The same host tools against the ARM binary: the STM32F4 build runs in
Renode, bridged to vcan0 (renode/ecu_bridge.resc).

Needs vcan0 and RENODE=/path/to/renode; skipped otherwise. The emulation
runs freely rather than being stepped, so host frames are never dropped
while it is paused.
"""

from __future__ import annotations

import os
import pathlib
import subprocess
import time

import can
import pytest

import ecu_common as ecu
from conftest import REPO, RESULTS_DIR, VCAN_PRESENT, collect
from uds_tester import open_client

RENODE = os.environ.get("RENODE")
ECU_ELF = REPO / "build/stm32/zephyr/zephyr.elf"

pytestmark = pytest.mark.skipif(
    not (VCAN_PRESENT and RENODE and ECU_ELF.exists()),
    reason="needs vcan0, RENODE and build/stm32",
)


@pytest.fixture(scope="module")
def renode_ecu():
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    uart_log = RESULTS_DIR / "renode_bridge_uart.log"
    script = (
        f"$ecu_bin=@{ECU_ELF}; "
        f"include @{REPO}/renode/ecu_bridge.resc; "
        f"sysbus.usart2 CreateFileBackend @{uart_log} true; "
        "start"
    )
    log = open(RESULTS_DIR / "renode_bridge.log", "w")
    proc = subprocess.Popen(
        [RENODE, "--disable-gui", "--plain", "-e", script],
        stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
    )
    try:
        end = time.monotonic() + 60
        while time.monotonic() < end:
            if uart_log.exists() and "ECU running (CAN up)" in uart_log.read_text(errors="replace"):
                break
            if proc.poll() is not None:
                raise RuntimeError("Renode exited early, see renode_bridge.log")
            time.sleep(0.5)
        else:
            raise TimeoutError("ECU did not boot in Renode")
        yield proc
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()


def test_arm_binary_frames_decode_on_host(renode_ecu):
    db = ecu.load_dbc()
    bus = can.Bus(interface="socketcan", channel=ecu.CAN_CHANNEL)
    try:
        frames = collect(bus, ecu.ID_SENSOR_STATUS, 3.0)
    finally:
        bus.shutdown()
    assert len(frames) >= 5
    for msg in frames:
        assert ecu.frame_crc_ok(bytes(msg.data))
        assert 15.0 <= db.decode_message(msg.arbitration_id, msg.data)["Temperature"] <= 35.0


def test_arm_binary_answers_uds_tester(renode_ecu):
    with open_client(ecu.CAN_CHANNEL) as client:
        assert client.read_data_by_identifier_first(ecu.DID_FW_VERSION) == "1.0.0"
        assert client.read_data_by_identifier_first(ecu.DID_SERIAL_NUMBER) == "ZCM-ECU-000001"
        assert client.get_number_of_dtc_by_status_mask(0x01).service_data.dtc_count == 0
