#!/usr/bin/env python3
"""Live CAN monitor: decodes every frame with the project DBC and checks the
E2E protection (rolling counter and CRC-8) on SENSOR_STATUS and ECU_COMMAND.

    can_monitor.py                      decode until Ctrl+C
    can_monitor.py --duration 5 --stats print period statistics at the end
"""

from __future__ import annotations

import argparse
import statistics
import sys
import time
from collections import defaultdict

import can

import ecu_common as ecu

E2E_IDS = {ecu.ID_SENSOR_STATUS, ecu.ID_ECU_COMMAND}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--channel", default=ecu.CAN_CHANNEL)
    ap.add_argument("--duration", type=float, default=0, help="seconds, 0 = until Ctrl+C")
    ap.add_argument("--stats", action="store_true", help="print per-ID period statistics")
    ap.add_argument("--quiet", action="store_true", help="only print E2E errors")
    args = ap.parse_args()

    db = ecu.load_dbc()
    known = {m.frame_id: m for m in db.messages}
    last_counter: dict[int, int] = {}
    stamps: dict[int, list[float]] = defaultdict(list)
    errors = 0

    bus = can.Bus(interface="socketcan", channel=args.channel)
    end = time.monotonic() + args.duration if args.duration else None
    try:
        while end is None or time.monotonic() < end:
            msg = bus.recv(timeout=0.5)
            if msg is None:
                continue
            stamps[msg.arbitration_id].append(msg.timestamp)
            data = bytes(msg.data)
            note = ""

            if msg.arbitration_id in E2E_IDS:
                if not ecu.frame_crc_ok(data):
                    note += "  CRC ERROR"
                    errors += 1
                counter = data[6] & 0x0F
                prev = last_counter.get(msg.arbitration_id)
                if msg.arbitration_id == ecu.ID_SENSOR_STATUS and prev is not None \
                        and counter != (prev + 1) & 0x0F:
                    note += f"  COUNTER JUMP {prev}->{counter}"
                    errors += 1
                last_counter[msg.arbitration_id] = counter

            if args.quiet and not note:
                continue

            message = known.get(msg.arbitration_id)
            if message is not None and message.name not in ("UDS_REQUEST", "UDS_RESPONSE"):
                decoded = message.decode(data, decode_choices=True)
                fields = " ".join(f"{k}={v}" for k, v in decoded.items() if k != "Crc8")
                print(f"{msg.timestamp:.3f} {message.name:<14} {fields}{note}")
            else:
                name = message.name if message else f"0x{msg.arbitration_id:03X}"
                print(f"{msg.timestamp:.3f} {name:<14} {data.hex(' ')}{note}")
    except KeyboardInterrupt:
        pass
    finally:
        bus.shutdown()

    if args.stats:
        print("\nID     name            frames  mean period   stdev    min     max  (ms)")
        for arb_id, ts in sorted(stamps.items()):
            if len(ts) < 3:
                continue
            gaps = [(b - a) * 1000 for a, b in zip(ts, ts[1:])]
            name = known[arb_id].name if arb_id in known else ""
            print(f"0x{arb_id:03X}  {name:<14} {len(ts):6d}  {statistics.mean(gaps):11.2f} "
                  f"{statistics.pstdev(gaps):7.2f} {min(gaps):7.2f} {max(gaps):7.2f}")
    if errors:
        print(f"\n{errors} E2E errors")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
