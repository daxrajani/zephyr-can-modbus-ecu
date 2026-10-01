"""Fixtures for the native_sim integration tests.

Each test gets a freshly started zephyr.exe, so state (thresholds, DTCs,
sessions) never leaks between tests. CAN tests need the vcan0 interface and
are skipped without it; the Modbus tests run anywhere, since Modbus goes over
a pseudo terminal.
"""

from __future__ import annotations

import json
import os
import pathlib
import re
import subprocess
import sys
import time

import can
import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "host"))

import ecu_common as ecu  # noqa: E402
from modbus_client import EcuModbus  # noqa: E402
from uds_tester import open_client  # noqa: E402

ECU_EXE = pathlib.Path(os.environ.get("ECU_EXE", REPO / "build/native/zephyr/zephyr.exe"))
RESULTS_DIR = REPO / "tests/integration/results"
VCAN_PRESENT = pathlib.Path(f"/sys/class/net/{ecu.CAN_CHANNEL}").exists()

requires_vcan = pytest.mark.skipif(
    not VCAN_PRESENT, reason=f"{ecu.CAN_CHANNEL} not present (run scripts/setup_vcan.sh)"
)


class EcuProcess:
    """A running zephyr.exe with its console captured to a log file."""

    def __init__(self, log_path: pathlib.Path):
        self.log_path = log_path
        self.log = open(log_path, "w")
        self.proc = subprocess.Popen(
            [str(ECU_EXE)], stdout=self.log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL
        )
        self.modbus_port = self._wait_for(r"uart_1 connected to pseudotty: (\S+)").group(1)
        self._wait_for(r"ECU running")

    def output(self) -> str:
        return self.log_path.read_text(errors="replace")

    def _wait_for(self, pattern: str, timeout: float = 10.0) -> re.Match:
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            m = re.search(pattern, self.output())
            if m:
                return m
            if self.proc.poll() is not None:
                raise RuntimeError(f"zephyr.exe exited:\n{self.output()}")
            time.sleep(0.05)
        raise TimeoutError(f"'{pattern}' not seen:\n{self.output()}")

    def wait_for_log(self, pattern: str, timeout: float = 5.0) -> re.Match:
        return self._wait_for(pattern, timeout)

    def stop(self) -> None:
        self.proc.terminate()
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        self.log.close()


@pytest.fixture
def ecu_proc(request, tmp_path):
    if not ECU_EXE.exists():
        pytest.fail(f"{ECU_EXE} not built (west build -b native_sim/native/64 app -d build/native)")
    proc = EcuProcess(tmp_path / "ecu.log")
    yield proc
    proc.stop()
    # Keep the console of every test for CI artifacts
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    (RESULTS_DIR / f"{request.node.name}.log").write_text(proc.output())


@pytest.fixture
def can_bus(ecu_proc):
    if not VCAN_PRESENT:
        pytest.skip(f"{ecu.CAN_CHANNEL} not present")
    bus = can.Bus(interface="socketcan", channel=ecu.CAN_CHANNEL)
    yield bus
    bus.shutdown()


@pytest.fixture
def uds(ecu_proc):
    if not VCAN_PRESENT:
        pytest.skip(f"{ecu.CAN_CHANNEL} not present")
    with open_client(ecu.CAN_CHANNEL) as client:
        yield client


@pytest.fixture
def uds_raw(ecu_proc):
    """Client that returns negative responses instead of raising."""
    if not VCAN_PRESENT:
        pytest.skip(f"{ecu.CAN_CHANNEL} not present")
    with open_client(ecu.CAN_CHANNEL, raise_on_nrc=False) as client:
        yield client


@pytest.fixture
def modbus(ecu_proc):
    mb = EcuModbus(ecu_proc.modbus_port)
    yield mb
    mb.close()


@pytest.fixture
def dbc():
    return ecu.load_dbc()


class CommandSender:
    """Sends ECU_COMMAND frames with a correctly advancing rolling counter."""

    def __init__(self, bus: can.BusABC):
        self.bus = bus
        self.counter = 0

    def send(self, *, counter: int | None = None, **kwargs) -> None:
        if counter is None:
            counter = self.counter
            self.counter = (self.counter + 1) & 0x0F
        payload = ecu.build_ecu_command(counter=counter, **kwargs)
        self.bus.send(can.Message(arbitration_id=ecu.ID_ECU_COMMAND, data=payload,
                                  is_extended_id=False))


@pytest.fixture
def commander(can_bus):
    return CommandSender(can_bus)


def collect(bus: can.BusABC, arb_id: int, duration: float) -> list[can.Message]:
    """All frames with arb_id seen within duration seconds."""
    frames = []
    end = time.monotonic() + duration
    while time.monotonic() < end:
        msg = bus.recv(timeout=0.1)
        if msg is not None and msg.arbitration_id == arb_id:
            frames.append(msg)
    return frames


def wait_status(bus: can.BusABC, dbc, predicate, timeout: float = 2.0) -> dict:
    """Wait for a SENSOR_STATUS frame whose decoded signals satisfy predicate."""
    end = time.monotonic() + timeout
    last = None
    while time.monotonic() < end:
        msg = bus.recv(timeout=0.1)
        if msg is None or msg.arbitration_id != ecu.ID_SENSOR_STATUS:
            continue
        last = dbc.decode_message(msg.arbitration_id, msg.data)
        if predicate(last):
            return last
    raise AssertionError(f"no matching SENSOR_STATUS within {timeout}s, last: {last}")


# --- measurements for the README / resume numbers -------------------------

_metrics: dict = {}


@pytest.fixture
def metrics():
    return _metrics


def pytest_sessionfinish(session, exitstatus):
    if _metrics:
        RESULTS_DIR.mkdir(parents=True, exist_ok=True)
        (RESULTS_DIR / "metrics.json").write_text(json.dumps(_metrics, indent=2))
