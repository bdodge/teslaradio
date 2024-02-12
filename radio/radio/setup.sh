#!/bin/sh
REL_BASE=../../../mead_ncs/zephyr
cd ${REL_BASE}
export ZEPHYR_BASE=${PWD}
cd ${OLDPWD}
export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb
export GCC_CROSS_VERSION=10.3-2021.10
export GNU_INSTALL_ROOT=/usr/local/GNU-Tools-ARM-Embedded/gcc-arm-none-eabi-${GCC_CROSS_VERSION}
export GNUARMEMB_TOOLCHAIN_PATH=${GNU_INSTALL_ROOT}/
