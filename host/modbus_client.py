#!/usr/bin/env python3
"""Modbus RTU client playing the PLC/SCADA side.

    modbus_client.py /dev/pts/3 poll
    modbus_client.py /dev/pts/3 set-threshold 30.0
    modbus_client.py /dev/pts/3 reset-alarm
"""

from __future__ import annotations

import argparse
import sys
import time

from pymodbus.client import ModbusSerialClient

import ecu_common as ecu


class EcuModbus:
    def __init__(self, port: str, timeout: float = 1.0):
        self.client = ModbusSerialClient(
            port=port,
            baudrate=ecu.MODBUS_BAUDRATE,
            bytesize=8,
            parity="N",
            stopbits=1,
            timeout=timeout,
            retries=1,
        )
        if not self.client.connect():
            raise ConnectionError(f"cannot open {port}")

    def close(self) -> None:
        self.client.close()

    def _check(self, rr):
        if rr.isError():
            raise IOError(f"Modbus error: {rr}")
        return rr

    def read_inputs(self) -> dict:
        regs = self._check(
            self.client.read_input_registers(0, count=5, device_id=ecu.MODBUS_UNIT_ID)
        ).registers
        bits = self._check(
            self.client.read_discrete_inputs(0, count=2, device_id=ecu.MODBUS_UNIT_ID)
        ).bits
        return {
            "temperature_c": ecu.to_int16(regs[ecu.IR_TEMPERATURE]) / 10.0,
            "vibration_mm_s": regs[ecu.IR_VIBRATION] / 100.0,
            "uptime_s": (regs[ecu.IR_UPTIME_HI] << 16) | regs[ecu.IR_UPTIME_LO],
            "active_dtcs": regs[ecu.IR_ACTIVE_DTCS],
            "alarm": bool(bits[ecu.DI_ALARM]),
            "sensor_fault": bool(bits[ecu.DI_SENSOR_FAULT]),
        }

    def read_holding(self) -> dict:
        regs = self._check(
            self.client.read_holding_registers(0, count=2, device_id=ecu.MODBUS_UNIT_ID)
        ).registers
        return {
            "sample_period_ms": regs[ecu.HR_SAMPLE_PERIOD],
            "alarm_threshold_c": ecu.to_int16(regs[ecu.HR_ALARM_THRESHOLD]) / 10.0,
        }

    def set_threshold(self, celsius: float) -> None:
        raw = int(round(celsius * 10)) & 0xFFFF
        self._check(
            self.client.write_register(
                ecu.HR_ALARM_THRESHOLD, raw, device_id=ecu.MODBUS_UNIT_ID
            )
        )

    def set_sample_period(self, ms: int) -> None:
        self._check(
            self.client.write_register(ecu.HR_SAMPLE_PERIOD, ms, device_id=ecu.MODBUS_UNIT_ID)
        )

    def reset_alarm(self) -> None:
        self._check(
            self.client.write_coil(ecu.COIL_ALARM_RESET, True, device_id=ecu.MODBUS_UNIT_ID)
        )

    def set_fault_injection(self, on: bool) -> None:
        self._check(
            self.client.write_coil(ecu.COIL_FAULT_INJECTION, on, device_id=ecu.MODBUS_UNIT_ID)
        )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("port", help="serial port, e.g. the native_sim uart_1 pty")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("poll")
    p.add_argument("--interval", type=float, default=1.0)
    p.add_argument("--count", type=int, default=0, help="0 = forever")
    sub.add_parser("show")
    p = sub.add_parser("set-threshold")
    p.add_argument("celsius", type=float)
    p = sub.add_parser("set-period")
    p.add_argument("ms", type=int)
    sub.add_parser("reset-alarm")
    p = sub.add_parser("fault")
    p.add_argument("state", choices=["on", "off"])
    args = ap.parse_args()

    mb = EcuModbus(args.port)
    try:
        if args.cmd == "poll":
            n = 0
            while args.count == 0 or n < args.count:
                t0 = time.perf_counter()
                values = mb.read_inputs()
                dt_ms = (time.perf_counter() - t0) * 1000
                print(
                    f"T={values['temperature_c']:6.1f} C  vib={values['vibration_mm_s']:5.2f} mm/s  "
                    f"alarm={int(values['alarm'])}  fault={int(values['sensor_fault'])}  "
                    f"dtcs={values['active_dtcs']}  up={values['uptime_s']} s  ({dt_ms:.1f} ms)"
                )
                n += 1
                time.sleep(args.interval)
        elif args.cmd == "show":
            print(mb.read_inputs())
            print(mb.read_holding())
        elif args.cmd == "set-threshold":
            mb.set_threshold(args.celsius)
            print(mb.read_holding())
        elif args.cmd == "set-period":
            mb.set_sample_period(args.ms)
            print(mb.read_holding())
        elif args.cmd == "reset-alarm":
            mb.reset_alarm()
            print(mb.read_inputs())
        elif args.cmd == "fault":
            mb.set_fault_injection(args.state == "on")
    except KeyboardInterrupt:
        pass
    finally:
        mb.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
