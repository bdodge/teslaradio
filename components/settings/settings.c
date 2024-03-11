/*
 * Copyright (c) 2024, Level Home Inc.
 *
 * All rights reserved.
 *
 * Proprietary and confidential. Unauthorized copying of this file,
 * via any medium is strictly prohibited.
 *
 */

#include "settings.h"
#include "asserts.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(setting, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <stdio.h>
#include <errno.h>

/// How long a key with all parent branches can be, max
#define SETTINGS_MAX_KEYPATH    (64)

/// max index for array setting
#define SETTINGS_MAX_INDEX      (255)

//#define UNIT_TEST 1

typedef struct
{
    char        key_path[SETTINGS_MAX_KEYPATH];
    size_t      size;
    size_t      count;
    uint8_t     *data;
    bool        found;
}
setting_desc_t;

static int _OnSettingLoaded(
        const char *in_key,
        size_t in_count,
        settings_read_cb in_callback,
        void *in_cb_arg,
        void *in_param
      )
{
    int ret = -EINVAL;
    setting_desc_t *setting = (setting_desc_t*)in_param;

    require(in_callback, exit);
    require(setting, exit);
    require(setting->key_path[0], exit);
    require(setting->data, exit);

    // key will be NULL for exact-match of keypath which is what we expect here
    if (in_key)
    {
        LOG_ERR("Attempt to read key %s which is a branch, and parent of key %s",
                setting->key_path, in_key);
    }

    require(!in_key, exit);

    in_callback(in_cb_arg, (uint8_t*)setting->data, in_count);
    setting->count = in_count;
    setting->found = true;
    LOG_DBG("setting %s loaded", setting->key_path);
exit:
    return ret;
}

static int _OnDeleteSetting(
        const char *in_key,
        size_t in_count,
        settings_read_cb in_callback,
        void *in_cb_arg,
        void *in_param
      )
{
    int ret = -EINVAL;
    setting_desc_t *setting = (setting_desc_t*)in_param;
    char key_path[SETTINGS_MAX_KEYPATH + 16];

    require(in_callback, exit);
    require(setting, exit);
    require(setting->key_path[0], exit);

    if (in_key)
    {
        // this is a sub-key of the branch setting->key
        snprintf(key_path, sizeof(key_path), "%s/%s", setting->key_path, in_key);
        ret = settings_delete(key_path);
    }
    else
    {
        // exact match of setting key, delete it directly
        ret = settings_delete(setting->key_path);
    }

    setting->found = true;
exit:
    return ret;
}

static int _InitSetting(
                        const char *in_key,
                        const size_t in_index,
                        uint8_t * const out_data,
                        const size_t in_size,
                        setting_desc_t *out_setting
                        )
{
    int ret = -EINVAL;
    int len;

    require(in_key, exit);
    require(out_setting, exit);

    len = snprintf(out_setting->key_path, sizeof(out_setting->key_path), "%s/%u", in_key, in_index);
    require(len > 0 && len < sizeof(out_setting->key_path), exit);

    out_setting->data    = out_data;
    out_setting->size    = in_size;
    out_setting->count   = 0;
    out_setting->found   = false;

    ret = 0;
exit:
    return ret;
}

int SettingsDeleteKey(const char *in_key, const size_t in_index)
{
    int ret = -EINVAL;
    setting_desc_t setting;

    ret = _InitSetting(in_key, in_index, NULL, 0, &setting);
    require_noerr(ret, exit);

    ret = settings_load_subtree_direct(setting.key_path, _OnDeleteSetting, (void *)&setting);
    require_action_quiet(ret == 0, exit, ret = -ENOENT);

    ret = setting.found ? 0 : -ENOENT;
exit:
    return ret;
}

int SettingsDeleteBranch(const char *in_key)
{
    int ret = -EINVAL;
    int len;
    setting_desc_t setting;

    ret = _InitSetting(in_key, 0, NULL, 0, &setting);
    require_noerr(ret, exit);

    // use just the key as path, no index, or sub key
    len = snprintf(setting.key_path, sizeof(setting.key_path), "%s", in_key);
    require(len > 0 && len < sizeof(setting.key_path), exit);

    ret = settings_load_subtree_direct(in_key, _OnDeleteSetting, (void *)&setting);
    require_action_quiet(ret == 0, exit, ret = -ENOENT);

    ret = setting.found ? 0 : -ENOENT;
exit:
    return ret;
}

int SettingsReadBytes(const char *in_key, const size_t in_index, uint8_t * const out_bytes, const size_t in_size, size_t *out_count)
{
    int ret = -EINVAL;
    setting_desc_t setting;

    require(in_key, exit);
    require(out_bytes, exit);
    require(in_size, exit);

    ret = _InitSetting(in_key, in_index, out_bytes, in_size, &setting);
    require_noerr(ret, exit);

    ret = settings_load_subtree_direct(setting.key_path, _OnSettingLoaded, (void *)&setting);
    require_action_quiet(ret == 0, exit, ret = -ENOENT);

    ret = setting.found ? 0 : -ENOENT;
exit:
    return ret;
}

int SettingsReadUint32(const char *in_key, const size_t in_index, uint32_t * const out_value)
{
    return SettingsReadBytes(in_key, in_index, (uint8_t * const)out_value, sizeof(uint32_t), NULL);
}

int SettingsReadInt32(const char *in_key, const size_t in_index, int32_t * const out_value)
{
    return SettingsReadBytes(in_key, in_index, (uint8_t * const)out_value, sizeof(int32_t), NULL);
}

int SettingsReadBool(const char *in_key, const size_t in_index, bool * const out_value)
{
    return SettingsReadBytes(in_key, in_index, (uint8_t * const)out_value, sizeof(bool), NULL);
}

int SettingsReadString(const char *in_key, const size_t in_index, char * const out_string, const size_t in_size)
{
    int ret = -EINVAL;
    size_t count = 0;

    ret = SettingsReadBytes(in_key, in_index, out_string, in_size, &count);
    require_noerr(ret, exit);

    if (count > (in_size - 1))
    {
        count = in_size - 1;
    }

    out_string[count] = '\0';
    ret = 0;
exit:
    return ret;
}

int SettingsWriteBytes(const char *in_key, const size_t in_index, const uint8_t * const in_bytes, size_t in_count)
{
    int ret = -EINVAL;
    setting_desc_t setting;

    require(in_key, exit);
    require(in_bytes, exit);
    require(in_count, exit);

    ret = _InitSetting(in_key, in_index, (uint8_t * const)in_bytes, in_count, &setting);
    require_noerr(ret, exit);

    ret = settings_save_one(setting.key_path, in_bytes, in_count);
    verify_noerr(ret);
exit:
    return ret;
}

int SettingsWriteUint32(const char *in_key, const size_t in_index, const uint32_t in_value)
{
    return SettingsWriteBytes(in_key, in_index, (const uint8_t*)&in_value, sizeof(uint32_t));
}

int SettingsWriteInt32(const char *in_key, const size_t in_index, const int32_t in_value)
{
    return SettingsWriteBytes(in_key, in_index, (const uint8_t*)&in_value, sizeof(int32_t));
}

int SettingsWriteBool(const char *in_key, const size_t in_index, const bool in_value)
{
    return SettingsWriteBytes(in_key, in_index, (const uint8_t*)&in_value, sizeof(bool));
}

int SettingsWriteString(const char *in_key, const size_t in_index, const char *in_string)
{
    int ret = -EINVAL;
    size_t length;

    require(in_key && in_string, exit);
    length = strlen(in_string);

    ret = SettingsWriteBytes(in_key, in_index, in_string, length);
exit:
    return ret;
}

int SettingsFirstEmptyIndex(const char *in_key, size_t in_max_index, size_t *out_index)
{
    int ret = -EINVAL;
    size_t num_read;
    uint8_t dummy[128];
    int index;

    require(in_key, exit);
    require(out_index, exit);

    for (index = 0; index < SETTINGS_MAX_INDEX && index < in_max_index; index++)
    {
        ret = SettingsReadBytes(in_key, index, dummy, sizeof(dummy), &num_read);
        if (ret == -ENOENT)
        {
            break;
        }
    }

    if (index >= SETTINGS_MAX_INDEX)
    {
        *out_index = SETTINGS_MAX_INDEX;
        ret = -ENOENT;
    }
    else
    {
        *out_index = index;
        ret = 0;
    }

exit:
    return ret;
}

int SettingsFirstSetIndex(const char *in_key, settings_iterator_t *out_iterator, size_t *out_index)
{
    settings_iterator_t iterator;

    if (!out_iterator)
    {
        out_iterator = &iterator;
    }

    out_iterator->index = 0;
    out_iterator->key = in_key;

    return SettingsNextSetIndex(out_iterator, out_index);
}

int SettingsNextSetIndex(settings_iterator_t *in_iterator, size_t *out_index)
{
    int ret = -EINVAL;
    size_t num_read;
    uint8_t dummy[128];
    int index;

    require(in_iterator, exit);
    require(in_iterator->key, exit);
    require(out_index, exit);

    for (index = in_iterator->index; index < SETTINGS_MAX_INDEX; index++)
    {
        ret = SettingsReadBytes(in_iterator->key, index, dummy, sizeof(dummy), &num_read);
        if (ret == 0)
        {
            break;
        }
    }

    if (index >= SETTINGS_MAX_INDEX)
    {
        *out_index = SETTINGS_MAX_INDEX;
        ret = -ENOENT;
    }
    else
    {
        *out_index = index;
        in_iterator->index++;
    }

exit:
    return ret;
}

#ifdef UNIT_TEST
static int _UnitTestSettings(void)
{
    int ret;
    uint32_t testData;
    settings_iterator_t iterator;
    int index;

    ret = SettingsDeleteBranch("ut");
    require(ret == 0 || ret == -ENOENT, exit);

    // basic write/read
    ret = SettingsWriteUint32("ut/t1", 0, 0x12345678);
    require_noerr(ret, exit);

    testData = 0;
    ret = SettingsReadUint32("ut/t1", 0, &testData);
    require_noerr(ret, exit);
    require_action(testData == 0x12345678, exit, ret = -1);

    // write a bunch
    for (index = 0; index < 3; index++)
    {
        ret = SettingsWriteUint32("ut/t2", index, index);
        require_noerr(ret, exit);
    }

    // read them back
    for (index = 0; index < 3; index++)
    {
        ret = SettingsReadUint32("ut/t2", index, &testData);
        require_noerr(ret, exit);
        require_action(testData == index, exit, ret = -1);
    }

    // delete the middle
    ret = SettingsDeleteKey("ut/t2", 1);
    require_noerr(ret, exit);

    // make sure first empty slot is 1
    ret = SettingsFirstEmptyIndex("ut/t2", SETTINGS_MAX_INDEX, &index);
    require_noerr(ret, exit);
    require_action(index == 1, exit, ret = -1);

    // iterate and make sure second found index is 2
    ret = SettingsFirstSetIndex("ut/t2", &iterator, &index);
    require_noerr(ret, exit);
    require_action(index == 0, exit, ret = -1);

    ret = SettingsNextSetIndex(&iterator, &index);
    require_noerr(ret, exit);
    require_action(index == 2, exit, ret = -1);

    // delete the branch
    ret = SettingsDeleteBranch("ut/t2");
    require_noerr(ret, exit);

    // make sure first key is untouched
    testData = 0;
    ret = SettingsReadUint32("ut/t1", 0, &testData);
    require_noerr(ret, exit);
    require_action(testData == 0x12345678, exit, ret = -1);

    // delete the single key as branch and check it fails
    ret = SettingsDeleteKey("ut", 0);
    require(ret != 0, exit);

    // delete the single key and insist it is gone
    ret = SettingsDeleteKey("ut/t1", 0);
    require_noerr(ret, exit);

    testData = 0;
    ret = SettingsReadUint32("ut/t1", 0, &testData);
    require(ret == -ENOENT, exit);

    LOG_INF("Unit Test passed");
    ret = 0;

exit:
    return ret;
}
#endif

/*
static int _OnPrintSetting(
        const char *in_key,
        size_t in_count,
        settings_read_cb in_callback,
        void *in_cb_arg,
        void *in_param
      )
{
    int ret = -EINVAL;

    // key will be NULL for exact-match of keypath which is what we expect here
    if (in_key)
    {
        LOG_INF("setting =%s= loaded", in_key);
    }

    return ret;
}
*/

int SettingsInit(void)
{
    int ret;

    ret = settings_subsys_init();
    require_noerr(ret, exit);

    ret = settings_load();
    require_noerr(ret, exit);

    //ret = settings_load_subtree_direct(NULL, _OnPrintSetting, (void *)NULL);
#ifdef UNIT_TEST
    ret = _UnitTestSettings();
    require_noerr(ret, exit);
#endif
exit:
    return ret;
}

