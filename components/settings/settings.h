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
#include <stdbool.h>
#include <stddef.h>

typedef struct
{
    const char *key;
    size_t index;
}
settings_iterator_t;

int SettingsDeleteKeyOrBranch(const char *in_key, const size_t in_index);
int SettingsDeleteBranch(const char *in_key);

int SettingsReadBytes(const char *in_key, const size_t in_index, uint8_t * const out_bytes, const size_t in_size, size_t *out_count);
int SettingsReadUint32(const char *in_key, const size_t in_index, uint32_t * const out_value);
int SettingsReadInt32(const char *in_key, const size_t in_index, int32_t * const out_value);
int SettingsReadBool(const char *in_key, const size_t in_index, bool * const out_value);
int SettingsReadString(const char *in_key, const size_t in_index, char * const out_string, const size_t in_size);

int SettingsWriteBytes(const char *in_key, const size_t in_index, const uint8_t * const in_bytes, size_t in_count);
int SettingsWriteUint32(const char *in_key, const size_t in_index, const uint32_t in_value);
int SettingsWriteInt32(const char *in_key, const size_t in_index, const int32_t in_value);
int SettingsWriteBool(const char *in_key, const size_t in_index, const bool in_value);
int SettingsWriteString(const char *in_key, const size_t in_index, const char * const in_string);

int SettingsFirstEmptyIndex(const char *in_key, size_t in_max_index, size_t *out_index);
int SettingsFirstSetIndex(const char *in_key, settings_iterator_t *out_iterator, size_t *out_index);
int SettingsNextSetIndex(settings_iterator_t *in_iterator, size_t *out_index);

int SettingsInit(void);

