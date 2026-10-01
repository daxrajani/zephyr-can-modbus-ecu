# Findings from running one firmware on three targets

The same Zephyr application runs on `native_sim` (SocketCAN), on an STM32F4
in Renode (emulated bxCAN), and is designed to move to real hardware. Most of
the interesting bugs only appeared when the targets disagreed.

## 1. ISO-TP filter overlap on hardware-filtering CAN controllers

**Symptom.** On the emulated STM32F4, a UDS response longer than 7 bytes
(for example `22 F1 89`, firmware version, 8 bytes) stalled or was followed
by `isotp: Got unexpected PDU`. The same firmware on `native_sim` was fine.

**Cause.** The ECU binds an ISO-TP receive context to `0x7E0` for requests.
To send a multi-frame response, `isotp_send()` installs another rx filter on
`0x7E0` to catch the tester's flow-control frame. Now two filters match
`0x7E0`.

* `native_sim`'s SocketCAN driver filters in software and calls **every**
  matching callback.
* bxCAN (and M_CAN) report a single *filter match index* per received frame,
  and Zephyr's drivers dispatch to **exactly one** callback: the one with the
  highest hardware priority (lowest filter number on bxCAN), which is the
  request context installed first. The flow-control frame is swallowed there,
  and the sender waits until its BS timeout.

**Fix** ([`app/src/uds_server.c`](../app/src/uds_server.c)). UDS is half
duplex: a tester must wait for the response before sending a new request. So
while a multi-frame response is in flight the server releases its request
binding (`isotp_unbind`) and rebinds once `isotp_send()` returns. Only one
`0x7E0` filter exists at any time. Single-frame responses skip this.

**Test.** `test_multi_frame_response_back_to_back` (pytest) and
`ECU Answers UDS On The ARM Binary` (Robot) send multi-frame reads followed
immediately by further requests.

## 2. Renode's STM32 bxCAN model (`STMCAN`)

Zephyr's `st,stm32-bxcan` driver did not work against Renode 1.16.1's model.
Each issue was traced from register-access logs
(`sysbus LogPeripheralAccess sysbus.can1`) and the model source, and fixed in
[`renode/STMCAN_Zephyr.cs`](../renode/STMCAN_Zephyr.cs), a copy of the
upstream model with every change marked `ZEPHYR FIX n`. Renode compiles it
at load time (`include @STMCAN_Zephyr.cs`), and the scripts swap it in for
`can1`/`can2`.

| # | Symptom | Model bug | Silicon behaviour |
|---|---|---|---|
| 1 | `can_stm32: Failed to enter init mode` | INRQ ignored while MCR.SLEEP is set | bxCAN leaves reset asleep and enters init mode on INRQ; Zephyr sets INRQ, then clears SLEEP |
| 2 | frames reached the FIFO but no callback ran | `RDTxR` was built before filter matching, so FMI always read 0 | FMI identifies the matching filter; Zephyr dispatches on it |
| 3 | second 16-bit filter of a bank never matched | match loop stopped after the first id/mask pair | a 16-bit mask bank holds two filters |
| 4 | `0x7E0` frames delivered to the `0x200` callback | 16-bit halves read high-first, swapping id and mask | bits 15:0 are filter *n* (id), bits 31:16 the mask / filter *n+1* |
| 5 | overlapping filters picked the wrong one | priority comparator inverted and inconsistent | 32-bit over 16-bit, list over mask, then lowest filter number |
| 6 | ISO-TP send never finished after the last CF | TX completed inside the register write, so the TX-complete IRQ ran before the driver's send call returned | a frame takes ~0.1–0.25 ms on the wire; completion is now scheduled after the frame time at 500 kbit/s |

Bug 6 deserves a note. With instant completion, the last consecutive frame's
TX-complete callback runs inside `can_send()`, before Zephyr's ISO-TP sender
has switched to `ISOTP_TX_WAIT_BACKLOG`. The callback is consumed in the wrong
state and the sender waits forever. On silicon the race is practically
impossible, because the sender would have to be preempted for a whole frame
time between `can_send()` and the state change. Emulation made it
deterministic.

Not fixed: for the CAN2 *slave*, the model numbers filters across all 28
shared banks rather than from `CAN2SB`. The STM32 build therefore uses CAN1.

## 3. Modbus RTU on `native_sim`

Zephyr's RTU backend calls `uart_configure()` and refuses to start if it
fails. The `native_sim` PTY UART does not implement it. Instead of
special-casing the simulator, the application does RTU framing itself
(silent-interval detection with a `k_timer`, CRC-16, unit filtering, no reply
to broadcasts) and feeds Zephyr's Modbus server through its raw ADU
interface. The same code runs on the STM32. On a PTY the frame gap is raised
to 5 ms (`CONFIG_ECU_MODBUS_MIN_FRAME_GAP_US`), because host scheduling
jitter would otherwise split frames; on a real UART it is t3.5 from the baud
rate.

## 4. Emulator scheduling in the two-node bench

The Renode peer node leaves 10 ms between UDS transactions. Two lockstep
emulated CPUs can otherwise answer the ECU's last consecutive frame before
the ECU has processed its own TX-complete, which no physical tester can do.
Real testers leave a gap anyway.
