
#include "tr_ble.h"
#include "ble_internal.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble_svc, LOG_LEVEL_INF);

#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/gatt_dm.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/settings/settings.h>

#include <stdlib.h>

const uint16_t kBLEProfile_CommandCharacteristicShortID             = BLEProfile_CommandCharacteristicShortID;
const uint16_t kBLEProfile_ResponseCharacteristicShortID            = BLEProfile_ResponseCharacteristicShortID;

static struct bt_uuid_16 kCalthingsServiceID                = BT_UUID_INIT_16( APP_BLE_PROFILE_SERVICE_SHORT_UUID );
static struct bt_uuid_16 kCommandCharacteristicID           = BT_UUID_INIT_16( BLEProfile_CommandCharacteristicShortID );
static struct bt_uuid_16 kResponseCharacteristicID          = BT_UUID_INIT_16( BLEProfile_ResponseCharacteristicShortID );

static void _cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("notify changed");
}

BT_GATT_SERVICE_DEFINE(mLevelService,
    BT_GATT_PRIMARY_SERVICE(&kCalthingsServiceID),
    BT_GATT_CHARACTERISTIC(&kCommandCharacteristicID.uuid,
        BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP, BT_GATT_PERM_WRITE,
        NULL, BLEinternalWriteCommand, NULL),
    BT_GATT_CHARACTERISTIC(&kResponseCharacteristicID.uuid,
        BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_READ,
        BLEinternalReadResponse, NULL, NULL),
    BT_GATT_CCC(_cccd_changed,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

int BLEServiceInit(void)
{
    int ret;

    ret = BLECharCommandInit(&mLevelService.attrs[4]);
    return ret;
}

