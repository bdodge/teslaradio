
cmake_minimum_required(VERSION 3.20.0)

set(PROJ_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(TREE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(COMPONENTS_DIR ${TREE_ROOT}/components)
set(DRIVERS_DIR ${TREE_ROOT}/drivers)
set(APPLICATION_DIR ${PROJ_ROOT}/src)

# include our common cmake functions
include(${TREE_ROOT}/helpers.cmake)

#list(APPEND OVERLAY_CONFIG "${PROJ_ROOT}/prj.conf")

list(APPEND DTS_ROOT ${TREE_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(maven_testbed)

if(CONFIG_BT)
  add_component(ble)
endif()
if (CONFIG_OPTION_AUDIO)
  add_component(audio)
  add_component(tones)
  add_driver(ti_tas2505 zephyr)
endif()

set(APPLICATION_SRCS
  ${APPLICATION_DIR}/main.c
  ${APPLICATION_DIR}/msc.c
  ${APPLICATION_DIR}/vfs.c
  ${APPLICATION_DIR}/vdisk.c
)

set(COMPONENTS_SRCS
)

target_sources(app PRIVATE
  ${APPLICATION_SRCS}
  ${COMPONENTS_SRCS}
)

target_include_directories(app PRIVATE
  ${APPLICATION_DIR}
)
