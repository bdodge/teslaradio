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
#include <stdbool.h>

int AudioGetSamples(void **out_sample_block, size_t *out_sample_bytes);
bool AudioActive(void);
int AudioStart(void);
int AudioStop(void);
int AudioInit(void);

