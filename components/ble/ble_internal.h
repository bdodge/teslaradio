
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

// meta-data on a single current ble connection
//
typedef struct
{
    struct bt_conn *conn;
    bool            unlocked;
    struct k_sem    notify_sem;
}
ble_conn_context_t;

// context of entire ble system
//
typedef struct
{
    uint8_t     device_name[32];
    uint8_t     device_short[9];
    bool        has_advertised;
    bool        is_advertising;
    uint32_t    adv_stop_time;
    bool        shell_inited;

    ble_connect_callback_t      connectCallback;
    ble_rxdata_callback_t       rxdataCallback;
    ble_mtu_update_callback_t   mtuUpdateCallback;

    ble_conn_context_t connections[BT_MAX_CONCURRENT];
}
ble_context_t;

extern ble_context_t mBLE;

// connection
//
ble_conn_context_t *BLEinternalConnectionOf(const void * const in_conn_handle);

// advertising
//
int BLEAdvertisingSlice(void);

// service
//
int BLEServiceInit(void);

// char_command
//
ssize_t BLEinternalWriteCommand(
                            struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            const void *buf,
                            uint16_t len,
                            uint16_t offset,
                            uint8_t flags
                       );

ssize_t BLEinternalReadResponse(
                            struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            void *buf,
                            uint16_t len,
                            uint16_t offset
                       );

int BLECharCommandInit(const struct bt_gatt_attr *in_attr);

