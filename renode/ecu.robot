*** Settings ***
Documentation       Emulated-target tests: the real ARM binaries on two STM32F4
...                 Discovery boards in Renode, joined by a CANHub. The peer node
...                 (peer/) drives the ECU over CAN and UDS and reports each step
...                 on its console.
...
...                 Run from the repository root after building both images:
...                 renode-test renode/ecu.robot

Resource            ${RENODEKEYWORDS}

Suite Setup         Setup
Suite Teardown      Teardown
Test Setup          Reset Emulation
Test Teardown       Test Teardown


*** Variables ***
${ROOT}             ${CURDIR}/..
${ECU_ELF}          ${ROOT}/build/stm32/zephyr/zephyr.elf
${PEER_ELF}         ${ROOT}/build/peer/zephyr/zephyr.elf
${CONSOLE}          sysbus.usart2


*** Keywords ***
Create Bench
    Execute Command             $ecu_bin=@${ECU_ELF}
    Execute Command             $peer_bin=@${PEER_ELF}
    Execute Command             include @${CURDIR}/ecu.resc
    ${ecu}=                     Create Terminal Tester    ${CONSOLE}    machine=ecu    timeout=10
    ${peer}=                    Create Terminal Tester    ${CONSOLE}    machine=peer    timeout=10
    Set Test Variable           ${ecu}
    Set Test Variable           ${peer}
    Start Emulation


*** Test Cases ***
ECU Boots And Brings Up Both Buses
    Create Bench
    Wait For Line On Uart       Sensor ECU 1.0.0 starting    testerId=${ecu}
    Wait For Line On Uart       CAN up on can@40006400, 500000 bit/s    testerId=${ecu}
    Wait For Line On Uart       UDS server on 0x7E0/0x7E8    testerId=${ecu}
    Wait For Line On Uart       Modbus RTU on serial@40004800, 19200 baud 8N1, unit 1    testerId=${ecu}
    Wait For Line On Uart       ECU running (CAN up)    testerId=${ecu}

Peer Receives Heartbeat
    Create Bench
    Wait For Line On Uart       peer: HEARTBEAT uptime=0 state=1    testerId=${peer}

Peer Receives E2E Protected Sensor Status
    Create Bench
    Wait For Line On Uart       peer: SENSOR_STATUS temp=\\d+ vib=\\d+ alarm=0 crc=ok
    ...                         testerId=${peer}    treatAsRegex=true

ECU Answers UDS On The ARM Binary
    [Documentation]             F189 is a multi-frame response (FF + CF + flow control).
    Create Bench
    Wait For Line On Uart       peer: UDS F189 = 1.0.0    testerId=${peer}
    Wait For Line On Uart       peer: UDS extended session ok    testerId=${peer}

Two Node CAN Command Exchange
    [Documentation]             The peer lowers the alarm threshold over CAN, sees the
    ...                         alarm flag in SENSOR_STATUS, then raises it and resets.
    Create Bench
    Wait For Line On Uart       ECU_COMMAND applied: period=0 threshold=100 fault=0 reset=0    testerId=${ecu}
    Wait For Line On Uart       peer: alarm active after ECU_COMMAND    testerId=${peer}
    Wait For Line On Uart       ECU_COMMAND applied: period=0 threshold=800 fault=0 reset=1    testerId=${ecu}
    Wait For Line On Uart       peer: alarm cleared after reset    testerId=${peer}

No Faults Are Reported In Normal Operation
    Create Bench
    Wait For Line On Uart       peer: active DTCs=0    testerId=${peer}
    Should Not Be On Uart       FAIL    testerId=${peer}    timeout=1

Full Peer Check Sequence Passes
    Create Bench
    Wait For Line On Uart       peer: ALL CHECKS PASSED    testerId=${peer}    timeout=30
