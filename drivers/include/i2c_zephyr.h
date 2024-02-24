/*
 * Copyright (c) 2024, Level Home Inc.
 *
 * All rights reserved.
 *
 * Proprietary and confidential. Unauthorized copying of this file,
 * via any medium is strictly prohibited.
 *
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

typedef const void *i2c_handle_t;

int             I2CWrite(
                        const i2c_handle_t in_h_i2c,
                        const uint8_t in_reg_addr,
                        const uint8_t * const in_data,
                        const size_t in_count
                );
int             I2CWriteByte(
                        const i2c_handle_t in_h_i2c,
                        const uint8_t in_reg_addr,
                        const uint8_t in_data
                );
int             I2CRead(
                        const i2c_handle_t in_h_i2c,
                        const uint8_t in_reg_addr,
                        uint8_t * const out_data,
                        const size_t in_count
                );

i2c_handle_t    I2CGetHandle(const char *in_interface_name);

