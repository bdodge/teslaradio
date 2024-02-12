
#include "tr_ble.h"
#include "ble_internal.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble_cmd, LOG_LEVEL_INF);

#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/gatt_dm.h>

typedef struct
{
    const struct bt_gatt_attr *response_attr;
}
ble_cmdchar_t;

static ble_cmdchar_t mBLEcommand;

ssize_t BLEinternalWriteCommand(
                            struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            const void *buf,
                            uint16_t len,
                            uint16_t offset,
                            uint8_t flags
                            )
{
    ble_conn_context_t *bleconn;

    bleconn = BLEinternalConnectionOf(conn);
    if (bleconn && len > 0)
    {
        if (mBLE.rxdataCallback)
        {
            //printk("ble sending %u to callback %p\n", len, mBLE.rxdataCallback);
            mBLE.rxdataCallback(conn, buf, len);
        }
    }

    return len;
}

ssize_t BLEinternalReadResponse(
                            struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            void *buf,
                            uint16_t len,
                            uint16_t offset
                            )
{
    ble_conn_context_t *bleconn;

    bleconn = BLEinternalConnectionOf(conn);
    return 0;
}

ssize_t BLEinternalWriteDFUrx(
                            struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            const void *buf,
                            uint16_t len,
                            uint16_t offset,
                            uint8_t flags
                            )
{
    ble_conn_context_t *bleconn;

    bleconn = BLEinternalConnectionOf(conn);
    if (bleconn && len > 0)
    {
        if (mBLE.rxdataCallback)
        {
            //printk("ble sending %u DFU data to callback %p\n", len, mBLE.rxdataCallback);
            mBLE.rxdataCallback(conn, buf, len);
        }
    }

    return 0;
}

static void _onNotificationComplete(struct bt_conn *conn, void *user_data)
{
    ble_conn_context_t *bleconn = (ble_conn_context_t *)user_data;

    if (bleconn)
    {
        k_sem_give(&bleconn->notify_sem);
    }

    return;
}

int BLEwriteResponseCharacteristic(const void *inConnHandle, const uint8_t *const inData, const size_t inDataLen)
{
    const struct bt_gatt_attr *attr = mBLEcommand.response_attr;
    ble_conn_context_t *bleconn;
    int ret = -1;

    bleconn = BLEinternalConnectionOf(inConnHandle);
    if (bleconn && attr)
    {
        struct bt_gatt_notify_params params =
        {
            .uuid   = BT_UUID_RESPONSE,
            .attr   = attr,
            .data   = inData,
            .len    = inDataLen,
            .user_data = bleconn,
            .func   = _onNotificationComplete
        };

        // Check whether notifications are enabled or not
        if(bt_gatt_is_subscribed(bleconn->conn, attr, BT_GATT_CCC_NOTIFY))
        {
            // block for a bit to make sure previous notification has finished
            //
            int result = k_sem_take(&bleconn->notify_sem, K_MSEC(500));

            if (result)
            {
                LOG_ERR("Notify callback not fired");
            }

            // Send the notification
            int err = bt_gatt_notify_cb(bleconn->conn, &params);
            if(err)
            {
                LOG_ERR("Error, unable to send notification. Error %d", err);
            }
            else
            {
                LOG_DBG("Notified central of %u resp", inDataLen);
                ret = inDataLen;
            }
        }
        else
        {
            LOG_WRN("Notification not enabled on the selected attribute");
        }
    }

    return ret;
}

int BLECharCommandInit(const struct bt_gatt_attr *in_attr)
{
    mBLEcommand.response_attr = in_attr;
    return 0;
}


