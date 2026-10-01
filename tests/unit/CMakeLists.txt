# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sensor_ecu_unit_tests LANGUAGES C)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../app/src)

target_include_directories(app PRIVATE ${APP_SRC})

# Only the hardware-independent modules; the Zephyr bus glue is covered by
# the integration and Renode tests.
target_sources(app PRIVATE
  ${APP_SRC}/crc.c
  ${APP_SRC}/can_codec.c
  ${APP_SRC}/data_model.c
  ${APP_SRC}/dtc.c
  ${APP_SRC}/uds.c
  ${APP_SRC}/modbus_map.c
  ${APP_SRC}/sensors_sim.c
  src/test_crc.c
  src/test_can_codec.c
  src/test_data_model.c
  src/test_dtc.c
  src/test_uds.c
  src/test_modbus_map.c
  src/test_sensors_sim.c
)
