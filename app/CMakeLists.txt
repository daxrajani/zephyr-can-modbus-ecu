# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sensor_ecu LANGUAGES C)

target_include_directories(app PRIVATE src)

target_sources(app PRIVATE
  src/main.c
  src/data_model.c
  src/sensors_sim.c
  src/crc.c
  src/can_codec.c
  src/can_app.c
  src/dtc.c
  src/uds.c
  src/uds_server.c
  src/modbus_map.c
  src/modbus_rtu.c
  src/modbus_server.c
)
