/**
 * @file BLEComm.h
 * @brief Generic BLE Communication Module Interface
 * @details Provides BLE Server and BLE Client communication for ESP32-S3.
 *
 *          SERVER mode : ESP32 advertises, phone/PyScript connects to ESP32
 *          CLIENT mode : ESP32 scans and connects TO another BLE device
 *
 *          Designed as a fully independent, reusable module.
 *          Not tied to any specific application, sensor, or display.
 *          Mirrors WiFiComm.h pattern for consistency.
 *
 * USAGE:
 * ======
 *   setup():
 *     BLEComm__Init();
 *
 *   Scheduler Task (100ms):
 *     BLEComm__Handler();
 *
 *   Send sensor data (both modes -- safe to call always):
 *     BLEComm__SendSensorData("{\"t\":25,\"h\":60}");        // server NOTIFY
 *     BLEComm__SendSensorDataToClient("{\"t\":25,\"h\":60}"); // client WRITE
 *
 *   Scan and connect (called by BLEScreenProcess UI):
 *     BLEComm__StartScan();
 *     BLEComm__ConnectToDevice(index);
 *
 *   Query state (called by BLEScreenProcess for rendering):
 *     BLEComm__IsConnected();
 *     BLEComm__GetDeviceCount();
 *     BLEComm__GetDeviceName(index);
 *
 * @version 1.1.0
 *
 * v1.1.0 changes (review fixes):
 *   - BLEComm_Status enum: negative values replaced with positive (MISRA Rule 10.3)
 *   - BLEComm__SetAdvSuppressed() now implemented in .cpp (was declared only)
 *   - Header comments: encoding artefacts replaced
 */

#ifndef BLECOMM_H
#define BLECOMM_H

/*==============================================================================
 *                              INCLUDES
 *============================================================================*/
#include <stdint.h>
#include <stdbool.h>
#include "BLEComm_Cfg.h"

/*==============================================================================
 *                              PUBLIC TYPES
 *============================================================================*/

/**
 * @brief BLE operation status
 * @note  All values are non-negative (MISRA C:2012 Rule 10.3).
 */
typedef enum
{
    BLECOMM_STATUS_OK      = 0U,  /**< Operation successful           */
    BLECOMM_STATUS_ERROR   = 1U,  /**< Operation failed               */
    BLECOMM_STATUS_BUSY    = 2U,  /**< Module busy (connect pending)  */
    BLECOMM_STATUS_NO_CONN = 3U   /**< No active BLE connection       */
} BLEComm_Status;

/**
 * @brief BLE server connection state
 */
typedef enum
{
    BLECOMM_SERVER_DISCONNECTED = 0U, /**< No client connected to server     */
    BLECOMM_SERVER_CONNECTED    = 1U, /**< Client connected, not identified  */
    BLECOMM_SERVER_IDENTIFIED   = 2U  /**< PyScript authenticated -- data ON */
} BLEComm_ServerState;

/**
 * @brief BLE client connection state
 */
typedef enum
{
    BLECOMM_CLIENT_DISCONNECTED = 0U, /**< Not connected to any device    */
    BLECOMM_CLIENT_CONNECTING   = 1U, /**< Connect in progress            */
    BLECOMM_CLIENT_CONNECTED    = 2U  /**< Connected to remote device     */
} BLEComm_ClientState;

/*==============================================================================
 *                          PUBLIC FUNCTION DECLARATIONS
 *============================================================================*/

/*------------------------------------------------------------------------------
 *  Lifecycle
 *----------------------------------------------------------------------------*/

/**
 * @brief Initialize BLE communication module.
 * @details Initializes BLE stack, creates server, service, and characteristics.
 *          Loads AES key and auth token from NVS (falls back to compiled
 *          defaults if NVS not provisioned -- see BLECOMM_DEFAULT_AES_KEY).
 *          Restores last fan settings from NVS.
 *          Starts advertising so remote devices can find and connect.
 *          Call once from setup() before Scheduler_Init().
 */
void BLEComm__Init(void);

/**
 * @brief BLE communication periodic handler -- call every 100ms from scheduler.
 * @details Polls connect task completion and flushes deferred NVS saves.
 *          BLE stack itself is event-driven via callbacks.
 */
void BLEComm__Handler(void);

/*------------------------------------------------------------------------------
 *  Data Send API
 *----------------------------------------------------------------------------*/

/**
 * @brief Send sensor data via BLE server NOTIFY (SERVER mode).
 * @details Safe to call always -- no-op if not connected or not authenticated.
 * @param[in] json Null-terminated JSON string to send.
 */
void BLEComm__SendSensorData(const char *json);

/**
 * @brief Send sensor data via BLE client WRITE (CLIENT mode).
 * @details Safe to call always -- no-op if not in client mode.
 * @param[in] json Null-terminated JSON string to send.
 */
void BLEComm__SendSensorDataToClient(const char *json);

/*------------------------------------------------------------------------------
 *  Scan & Connect API  (called by BLEScreenProcess UI layer)
 *----------------------------------------------------------------------------*/

/**
 * @brief Start BLE scan for nearby devices.
 * @details Scans for BLECOMM_SCAN_DURATION_SEC seconds.
 */
void BLEComm__StartScan(void);

/** @brief Stop active BLE scan. */
void BLEComm__StopScan(void);

/**
 * @brief Connect to scanned device by index (non-blocking).
 * @details Spawns background FreeRTOS task for connection.
 *          If already connected to this device, disconnects instead.
 * @param[in] index Device index from scan results (0 to GetDeviceCount()-1).
 */
void BLEComm__ConnectToDevice(uint8_t index);

/** @brief Disconnect from currently connected client device. */
void BLEComm__Disconnect(void);

/*------------------------------------------------------------------------------
 *  State Query API  (called by BLEScreenProcess for rendering)
 *----------------------------------------------------------------------------*/

/** @brief Get BLE server state. */
BLEComm_ServerState BLEComm__GetServerState(void);

/** @brief Get BLE client state. */
BLEComm_ClientState BLEComm__GetClientState(void);

/** @brief Check if ESP32 is connected to any BLE device (client mode). */
bool BLEComm__IsConnected(void);

/** @brief Check if BLE server is actively sending data to authenticated client. */
bool BLEComm__IsSendingData(void);

/** @brief Check if BLE client mode is active and sending data to PyScript. */
bool BLEComm__IsClientSendingData(void);

/** @brief Check if BLE scan is in progress. */
bool BLEComm__IsScanning(void);

/** @brief Check if last BLE scan has completed. */
bool BLEComm__IsScanComplete(void);

/** @brief Get number of devices found in last scan. */
uint8_t BLEComm__GetDeviceCount(void);

/**
 * @brief Get device name from scan results.
 * @param[in] index Device index (0 to GetDeviceCount()-1).
 * @return Pointer to device name string, or NULL if index invalid.
 */
const char *BLEComm__GetDeviceName(uint8_t index);

/**
 * @brief Get device address from scan results.
 * @param[in] index Device index.
 * @return Pointer to address string, or NULL if index invalid.
 */
const char *BLEComm__GetDeviceAddress(uint8_t index);

/**
 * @brief Get device RSSI from scan results.
 * @param[in] index Device index.
 * @return RSSI in dBm, or 0 if index invalid.
 */
int32_t BLEComm__GetDeviceRSSI(uint8_t index);

/**
 * @brief Check if specific scanned device is currently connected.
 * @param[in] index Device index.
 */
bool BLEComm__IsDeviceConnected(uint8_t index);

/**
 * @brief Check if specific scanned device is currently connecting.
 * @param[in] index Device index.
 */
bool BLEComm__IsDeviceConnecting(uint8_t index);

/** @brief Get name of device connected to server (phone/PyScript to ESP32). */
const char *BLEComm__GetServerClientName(void);

/** @brief Check if any device is connected to BLE server. */
bool BLEComm__IsServerClientConnected(void);

/** @brief Check if PyScript is authenticated and receiving sensor data. */
bool BLEComm__IsPyScriptConnected(void);

/** @brief Get name of device connected as client (ESP32 to remote). */
const char *BLEComm__GetConnectedDeviceName(void);

/*------------------------------------------------------------------------------
 *  Dirty Flag API  (cross-task safe display signal)
 *----------------------------------------------------------------------------*/

/** @brief Check if BLE state changed and display needs update. */
bool BLEComm__IsStatusDirty(void);

/** @brief Clear the dirty flag after display has been updated. */
void BLEComm__ClearStatusDirty(void);

/** @brief Set the dirty flag -- called from BLE callbacks to signal redraw. */
void BLEComm__SetStatusDirty(void);

/**
 * @brief Suppress or allow BLEComm__Handler auto-restart of advertising.
 * @details Retained for API compatibility. Advertising auto-restart was removed
 *          from BLEComm__Handler() in v1.0.0; App.cpp state machine is the
 *          single authority for advertising start/stop. This function is now
 *          a no-op but remains to avoid breaking callers.
 * @param[in] suppress true = suppress, false = allow (currently no effect).
 */
void BLEComm__SetAdvSuppressed(bool suppress);

#endif /* BLECOMM_H */