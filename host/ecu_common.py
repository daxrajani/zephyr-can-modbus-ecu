"""Shared constants and helpers for the host tools and tests.

Everything here mirrors app/src/ecu_config.h and dbc/sensor_ecu.dbc. The
integration tests are what keep the two sides honest.
"""

from __future__ import annotations

import pathlib
import struct

import cantools

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
DBC_PATH = REPO_ROOT / "dbc" / "sensor_ecu.dbc"

# CAN
CAN_CHANNEL = "vcan0"
ID_SENSOR_STATUS = 0x100
ID_HEARTBEAT = 0x101
ID_ECU_COMMAND = 0x200
ID_UDS_REQUEST = 0x7E0
ID_UDS_RESPONSE = 0x7E8
SENSOR_STATUS_PERIOD_MS = 100
HEARTBEAT_PERIOD_MS = 1000

CMD_SAMPLE_PERIOD_KEEP = 0
CMD_ALARM_THRESHOLD_KEEP = 0x7FFF

# Modbus (0-based protocol addresses; README uses 1-based Modicon numbers)
MODBUS_UNIT_ID = 1
MODBUS_BAUDRATE = 19200
IR_TEMPERATURE = 0      # 30001
IR_VIBRATION = 1        # 30002
IR_UPTIME_HI = 2        # 30003
IR_UPTIME_LO = 3        # 30004
IR_ACTIVE_DTCS = 4      # 30005
DI_ALARM = 0            # 10001
DI_SENSOR_FAULT = 1     # 10002
HR_SAMPLE_PERIOD = 0    # 40001
HR_ALARM_THRESHOLD = 1  # 40002
COIL_ALARM_RESET = 0    # 00001
COIL_FAULT_INJECTION = 1  # 00002

# UDS
DID_ACTIVE_SESSION = 0xF186
DID_FW_VERSION = 0xF189
DID_SERIAL_NUMBER = 0xF18C
DID_TEMPERATURE = 0x0100
DID_VIBRATION = 0x0101
DID_STATUS_FLAGS = 0x0102
DID_SAMPLE_PERIOD = 0x0103
DID_ALARM_THRESHOLD = 0x0104
DID_UPTIME = 0x0105

DTC_SENSOR_OUT_OF_RANGE = 0x0A1011
DTC_CAN_BUS_OFF = 0xC07388
DTC_MODBUS_MASTER_TIMEOUT = 0xC10087
DTC_COMMAND_CRC_ERROR = 0xC41481
DTC_NAMES = {
    DTC_SENSOR_OUT_OF_RANGE: "P0A10-11 temperature sensor out of range",
    DTC_CAN_BUS_OFF: "U0073-88 CAN bus off",
    DTC_MODBUS_MASTER_TIMEOUT: "U0100-87 Modbus master timeout",
    DTC_COMMAND_CRC_ERROR: "U0414-81 ECU_COMMAND CRC error",
}


def load_dbc() -> cantools.database.Database:
    return cantools.database.load_file(str(DBC_PATH))


def crc8_sae_j1850(data: bytes) -> int:
    """CRC-8 SAE J1850: poly 0x1D, init 0xFF, xorout 0xFF."""
    crc = 0xFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1D) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc ^ 0xFF


def build_ecu_command(
    *,
    sample_period_ms: int = CMD_SAMPLE_PERIOD_KEEP,
    alarm_threshold_dC: int = CMD_ALARM_THRESHOLD_KEEP,
    fault_injection: bool = False,
    alarm_reset: bool = False,
    counter: int = 0,
    corrupt_crc: bool = False,
) -> bytes:
    """Encode ECU_COMMAND with the DBC, then seal it with counter and CRC."""
    db = load_dbc()
    msg = db.get_message_by_name("ECU_COMMAND")
    payload = bytearray(
        msg.encode(
            {
                "SamplePeriod": sample_period_ms,
                "AlarmThreshold": alarm_threshold_dC / 10.0,
                "FaultInjection": int(fault_injection),
                "AlarmReset": int(alarm_reset),
                "RollingCounter": counter & 0x0F,
                "Crc8": 0,
            },
            scaling=True,
            strict=False,
        )
    )
    payload[7] = crc8_sae_j1850(bytes(payload[:7]))
    if corrupt_crc:
        payload[7] ^= 0xFF
    return bytes(payload)


def frame_crc_ok(data: bytes) -> bool:
    return len(data) == 8 and crc8_sae_j1850(data[:7]) == data[7]


def uds_compute_key(seed: int) -> int:
    """Mirror of uds_compute_key() in app/src/uds.c (demo algorithm)."""
    x = (seed ^ 0x5A3C96E1) & 0xFFFFFFFF
    return ((((x << 7) | (x >> 25)) & 0xFFFFFFFF) + 0x1F2E3D4C) & 0xFFFFFFFF


def uds_security_algo(level: int, seed: bytes, params=None) -> bytes:
    """udsoncan security_algo hook."""
    (value,) = struct.unpack(">I", seed)
    return struct.pack(">I", uds_compute_key(value))


def to_int16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value
