
#include "tr_ble.h"
#include "ble_internal.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble_adv, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <bluetooth/gatt_dm.h>

// how long to advertise after startup. seconds (0 means forever)
//
#define BLE_ADV_PERIOD_SECS  (0) /*(60*60)*/
#define BT_GAP_ADV_VERY_SLOW_INT_MIN   0x0C80 /* 0.625ms * 0xC80 = 2s */
#define BT_GAP_ADV_VERY_SLOW_INT_MAX   0x0DC0 /* 0.625ms * 0xDC0 = 2.2s */

#if defined(CONFIG_IEEE802154)
// fast advertising for non-multiprotocol builds
#define BLE_ADV_PARAM BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE, \
                       BT_GAP_ADV_FAST_INT_MIN_1, \
                       BT_GAP_ADV_FAST_INT_MAX_1, NULL)
#else
// slow advertising to free up radio time for Thread
// do not advertise on channel 39 (2480MHz) as per Stargate FCC ceritifcation
#define BLE_ADV_PARAM BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_DISABLE_CHAN_39, \
                       BT_GAP_ADV_VERY_SLOW_INT_MIN, \
                       BT_GAP_ADV_VERY_SLOW_INT_MAX, NULL)
#endif

#define BT_UUID_LEVEL \
    BT_UUID_DECLARE_16(BT_UUID_LEVEL_VAL)

static struct bt_data s_adv_data[] =
{
    BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE,
                  (CONFIG_BT_DEVICE_APPEARANCE >> 0) & 0xff,
                  (CONFIG_BT_DEVICE_APPEARANCE >> 8) & 0xff),
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL,
                  BT_UUID_16_ENCODE(BT_UUID_SERVICE)),
        { BT_DATA_NAME_SHORTENED, sizeof(mBLE.device_short) - 1, mBLE.device_short }
};

static struct bt_data s_scan_data[] =
{
    { BT_DATA_NAME_COMPLETE, sizeof(mBLE.device_name) - 1, mBLE.device_name }
};

int BLEstartAdvertising(void)
{
    int ret = -1;

    LOG_INF("Starting BLE Advertising");
    ret = bt_le_adv_start(
                 BLE_ADV_PARAM,
                 s_adv_data,
                 ARRAY_SIZE(s_adv_data),
                 s_scan_data,
                 ARRAY_SIZE(s_scan_data)
             );
    if (ret)
    {
        goto exit;
    }
    if (BLE_ADV_PERIOD_SECS > 0)
    {
        mBLE.adv_stop_time = k_uptime_get() + (1000 * BLE_ADV_PERIOD_SECS);
    }
    else
    {
        mBLE.adv_stop_time = 0;
    }

    mBLE.is_advertising = false;
exit:
    return ret;
}

int BLEstopAdvertising(void)
{
    int ret = -1;

    LOG_INF("Stopping BLE Advertising");
    mBLE.is_advertising = false;
    ret = bt_le_adv_stop();
    return ret;
}

int BLEsetDeviceName(const char *in_device_name)
{
    int ret = -EINVAL;
    size_t len;

    if (!in_device_name)
    {
        goto exit;
    }

    len = strlen(in_device_name);
    if (len == 0)
    {
        goto exit;
    }

    if (len >= sizeof(mBLE.device_name))
    {
        len = sizeof(mBLE.device_name) - 1;
    }
    else
    {
        ret = 0;
    }

    memcpy(mBLE.device_name, in_device_name, len);
    mBLE.device_name[len] = '\0';

    if (len >= sizeof(mBLE.device_short))
    {
        len = sizeof(mBLE.device_short) - 1;
    }

    memcpy(mBLE.device_short, in_device_name, len);
    mBLE.device_short[len] = '\0';

exit:
    return ret;
}

int BLEAdvertisingSlice(void)
{
    if (mBLE.is_advertising && mBLE.adv_stop_time < k_uptime_get())
    {
        LOG_INF("Adv time over");
        BLEstopAdvertising();
    }
    else if (!mBLE.has_advertised)
    {
        int ret;

        // cant advertise until device is ready
        //
        if (bt_is_ready())
        {
            mBLE.has_advertised = true;
            ret = BLEstartAdvertising();
        }
    }

    return 0;
}

