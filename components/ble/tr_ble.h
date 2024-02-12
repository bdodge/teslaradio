
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/// external representation of a ble connection for use by
/// any other subsystem this interacts with
//
typedef const void *ble_conn_handle_t;

/// max number of concurrent BLE connections
//
#define BT_MAX_CONCURRENT               (1)

#define BT_UUID_VAL                     (0xF00D)
#define BT_UUID_SERVICE                 BT_UUID_VAL

// Base UUID - use the SIG UUID here since we're using our SIG-assigned 16-bit UUID for the service
#define APP_BLE_PROFILE_BASE_UUID                           { 0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define APP_BLE_PROFILE_SERVICE_SHORT_UUID                  ( 0xFDBF )

#define BLEProfile_CommandCharacteristicShortID             ( 0x0010 )
#define BLEProfile_ResponseCharacteristicShortID            ( 0x0012 )

#define RESPONSE_CHARACTERISTIC_UUID    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00
#define BT_UUID_RESPONSE   BT_UUID_DECLARE_128(RESPONSE_CHARACTERISTIC_UUID)

typedef const int   (*ble_connect_callback_t)(ble_conn_handle_t in_conn, const uint16_t inMTU, const bool isConnected);
typedef const int   (*ble_rxdata_callback_t)(ble_conn_handle_t in_conn, const uint8_t *inData, const size_t inDataLen);
typedef const int   (*ble_mtu_update_callback_t)(ble_conn_handle_t in_conn, const uint16_t inMTU);

// connection
//
int     BLEdisconnect(ble_conn_handle_t in_conn);
bool    BLEisShellEnabled(void);
int     BLEslice(uint32_t *outDelay);
int     BLEinit(const char *in_device_name);

int     BLEwriteResponseCharacteristic(ble_conn_handle_t in_conn, const uint8_t *const inData, const size_t inDataLen);
int     BLEsetDelegate(
                        ble_connect_callback_t    inConnectCallback,
                        ble_rxdata_callback_t     inRxDataCallback,
                        ble_mtu_update_callback_t inMTUupdateCallback
                        );

// advertising
//
int     BLEstartAdvertising(void);
int     BLEstopAdvertising(void);
int     BLEsetDeviceName(const char *inDeviceName);

