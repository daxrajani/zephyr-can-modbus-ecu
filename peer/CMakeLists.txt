# SPDX-License-Identifier: Apache-2.0
#
# Peer node for the two-machine Renode CANHub bench. It shares the CAN
# codec with the ECU so both ends agree on the frame layout by construction.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(peer_node LANGUAGES C)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../app/src)

target_include_directories(app PRIVATE ${APP_SRC})
target_sources(app PRIVATE
  src/main.c
  ${APP_SRC}/crc.c
  ${APP_SRC}/can_codec.c
)
