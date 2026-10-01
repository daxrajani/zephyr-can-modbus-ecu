#!/usr/bin/env python3
"""UDS tester for the sensor ECU (udsoncan over python-can-isotp).

    uds_tester.py info                 version, serial, live values
    uds_tester.py dtc                  list stored DTCs
    uds_tester.py clear                clear all DTCs
    uds_tester.py write-threshold 30   extended session, unlock, write 0x0104
"""

from __future__ import annotations

import argparse
import contextlib
import struct
import sys

import can
import isotp
import udsoncan
import udsoncan.configs
import udsoncan.services
from udsoncan.client import Client
from udsoncan.connections import PythonIsoTpConnection

import ecu_common as ecu

class ScalarCodec(udsoncan.DidCodec):
    """One big-endian integer per DID. udsoncan's plain struct-format codecs
    decode to a 1-tuple; this returns the number itself."""

    def __init__(self, fmt: str):
        self.fmt = fmt

    def encode(self, value) -> bytes:
        return struct.pack(self.fmt, value)

    def decode(self, payload: bytes):
        return struct.unpack(self.fmt, payload)[0]

    def __len__(self) -> int:
        return struct.calcsize(self.fmt)


DID_CODECS = {
    ecu.DID_ACTIVE_SESSION: ScalarCodec("B"),
    ecu.DID_FW_VERSION: udsoncan.AsciiCodec(5),
    ecu.DID_SERIAL_NUMBER: udsoncan.AsciiCodec(14),
    ecu.DID_TEMPERATURE: ScalarCodec(">h"),
    ecu.DID_VIBRATION: ScalarCodec(">H"),
    ecu.DID_STATUS_FLAGS: ScalarCodec("B"),
    ecu.DID_SAMPLE_PERIOD: ScalarCodec(">H"),
    ecu.DID_ALARM_THRESHOLD: ScalarCodec(">h"),
    ecu.DID_UPTIME: ScalarCodec(">I"),
}


def client_config() -> dict:
    config = dict(udsoncan.configs.default_client_config)
    config["data_identifiers"] = DID_CODECS
    config["security_algo"] = ecu.uds_security_algo
    config["request_timeout"] = 2.0
    config["p2_timeout"] = 1.0
    # The ECU announces P2 = 50 ms; keep the tester tolerant of a loaded host
    config["use_server_timing"] = False
    return config


@contextlib.contextmanager
def open_client(channel: str = ecu.CAN_CHANNEL, *, raise_on_nrc: bool = True):
    """Yield a connected udsoncan Client on the given SocketCAN channel."""
    bus = can.Bus(interface="socketcan", channel=channel)
    address = isotp.Address(
        isotp.AddressingMode.Normal_11bits, txid=ecu.ID_UDS_REQUEST, rxid=ecu.ID_UDS_RESPONSE
    )
    stack = isotp.CanStack(
        bus,
        address=address,
        params={"stmin": 0, "blocksize": 0, "tx_padding": 0x00, "rx_flowcontrol_timeout": 1000},
    )
    conn = PythonIsoTpConnection(stack)
    config = client_config()
    config["exception_on_negative_response"] = raise_on_nrc
    try:
        with Client(conn, config=config) as client:
            yield client
    finally:
        bus.shutdown()


def read(client: Client, did: int):
    return client.read_data_by_identifier_first(did)


def print_info(client: Client) -> None:
    print(f"firmware version : {read(client, ecu.DID_FW_VERSION)}")
    print(f"serial number    : {read(client, ecu.DID_SERIAL_NUMBER)}")
    print(f"active session   : 0x{read(client, ecu.DID_ACTIVE_SESSION):02X}")
    print(f"temperature      : {read(client, ecu.DID_TEMPERATURE) / 10:.1f} C")
    print(f"vibration RMS    : {read(client, ecu.DID_VIBRATION) / 100:.2f} mm/s")
    flags = read(client, ecu.DID_STATUS_FLAGS)
    print(f"status flags     : alarm={flags & 1} sensor_fault={(flags >> 1) & 1} "
          f"fault_injection={(flags >> 2) & 1}")
    print(f"sample period    : {read(client, ecu.DID_SAMPLE_PERIOD)} ms")
    print(f"alarm threshold  : {read(client, ecu.DID_ALARM_THRESHOLD) / 10:.1f} C")
    print(f"uptime           : {read(client, ecu.DID_UPTIME)} s")


def read_dtcs(client: Client, mask: int = 0xFF) -> list[udsoncan.Dtc]:
    response = client.get_dtc_by_status_mask(mask)
    return list(response.service_data.dtcs)


def print_dtcs(client: Client) -> None:
    dtcs = read_dtcs(client)
    if not dtcs:
        print("no DTCs stored")
    for dtc in dtcs:
        name = ecu.DTC_NAMES.get(dtc.id, "unknown")
        print(f"0x{dtc.id:06X}  status=0x{dtc.status.get_byte_as_int():02X}  {name}")


def unlock(client: Client) -> None:
    client.change_session(udsoncan.services.DiagnosticSessionControl.Session.extendedDiagnosticSession)
    client.unlock_security_access(1)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--channel", default=ecu.CAN_CHANNEL)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("info")
    sub.add_parser("dtc")
    sub.add_parser("clear")
    p = sub.add_parser("write-threshold")
    p.add_argument("celsius", type=float)
    args = ap.parse_args()

    with open_client(args.channel) as client:
        if args.cmd == "info":
            print_info(client)
        elif args.cmd == "dtc":
            print_dtcs(client)
        elif args.cmd == "clear":
            client.clear_dtc(0xFFFFFF)
            print("DTCs cleared")
        elif args.cmd == "write-threshold":
            unlock(client)
            client.write_data_by_identifier(ecu.DID_ALARM_THRESHOLD, int(round(args.celsius * 10)))
            print(f"threshold now {read(client, ecu.DID_ALARM_THRESHOLD) / 10:.1f} C")
    return 0


if __name__ == "__main__":
    sys.exit(main())
