# ShotPuck firmware - Zephyr 4.1 (also builds in nRF Connect SDK v3.0+)
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(shotpuck)

target_sources(app PRIVATE
  src/main.c
  src/shot_detect.c
  src/accel.c
  src/capture.c
  src/capture_tx.c
  src/chg_led.c
  src/evlog.c
  src/factory.c
  src/event_tx.c
  src/claim.c
  src/identity.c
  src/ota.c
  src/power.c
  src/recovery.c
  src/store.c
  src/ble.c
)
