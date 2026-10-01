#!/usr/bin/env python3
"""A second ECU on the bus: a supervisory node that watches SENSOR_STATUS and
manages the sensor ECU with ECU_COMMAND frames.

Policy: configure the alarm threshold at start-up; while the alarm is active,
sample faster; once the temperature is back below the threshold minus a
hysteresis band, reset the alarm and restore the normal sample period.

    peer_ecu.py --threshold 27.5
"""

from __future__ import annotations

import argparse
import sys
import time

import can

import ecu_common as ecu


class PeerEcu:
    def __init__(self, bus: can.BusABC, threshold_c: float, hysteresis_c: float,
                 normal_period_ms: int, alarm_period_ms: int):
        self.bus = bus
        self.db = ecu.load_dbc()
        self.status_msg = self.db.get_message_by_name("SENSOR_STATUS")
        self.threshold_c = threshold_c
        self.hysteresis_c = hysteresis_c
        self.normal_period_ms = normal_period_ms
        self.alarm_period_ms = alarm_period_ms
        self.counter = 0
        self.in_alarm = False

    def command(self, **kwargs) -> None:
        payload = ecu.build_ecu_command(counter=self.counter, **kwargs)
        self.counter = (self.counter + 1) & 0x0F
        self.bus.send(can.Message(arbitration_id=ecu.ID_ECU_COMMAND, data=payload,
                                  is_extended_id=False))

    def start(self) -> None:
        self.command(sample_period_ms=self.normal_period_ms,
                     alarm_threshold_dC=int(round(self.threshold_c * 10)))
        print(f"peer: threshold {self.threshold_c:.1f} C, period {self.normal_period_ms} ms")

    def on_status(self, data: bytes) -> None:
        if not ecu.frame_crc_ok(data):
            print("peer: dropped SENSOR_STATUS with bad CRC")
            return
        s = self.status_msg.decode(data)
        alarm = bool(s["AlarmActive"])
        temperature = s["Temperature"]

        if alarm and not self.in_alarm:
            self.in_alarm = True
            self.command(sample_period_ms=self.alarm_period_ms)
            print(f"peer: ALARM at {temperature:.1f} C -> sampling every {self.alarm_period_ms} ms")
        elif self.in_alarm and temperature < self.threshold_c - self.hysteresis_c:
            self.in_alarm = False
            self.command(sample_period_ms=self.normal_period_ms, alarm_reset=True)
            print(f"peer: recovered at {temperature:.1f} C -> alarm reset")

    def run(self, duration: float) -> None:
        self.start()
        end = time.monotonic() + duration if duration else None
        while end is None or time.monotonic() < end:
            msg = self.bus.recv(timeout=0.5)
            if msg is not None and msg.arbitration_id == ecu.ID_SENSOR_STATUS:
                self.on_status(bytes(msg.data))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--channel", default=ecu.CAN_CHANNEL)
    ap.add_argument("--threshold", type=float, default=27.5, help="alarm threshold, degC")
    ap.add_argument("--hysteresis", type=float, default=1.0, help="degC below threshold to reset")
    ap.add_argument("--normal-period", type=int, default=100, help="ms")
    ap.add_argument("--alarm-period", type=int, default=20, help="ms")
    ap.add_argument("--duration", type=float, default=0, help="seconds, 0 = until Ctrl+C")
    args = ap.parse_args()

    bus = can.Bus(interface="socketcan", channel=args.channel)
    try:
        PeerEcu(bus, args.threshold, args.hysteresis, args.normal_period,
                args.alarm_period).run(args.duration)
    except KeyboardInterrupt:
        pass
    finally:
        bus.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
