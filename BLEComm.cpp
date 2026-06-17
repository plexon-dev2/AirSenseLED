// /**
//  * @file BLEComm.cpp
//  * @brief Generic BLE Communication Module Implementation
//  * @details Implements BLE Server and BLE Client communication for ESP32-S3.
//  *
//  *          SERVER mode : ESP32 advertises as BLECOMM_DEVICE_NAME.
//  *                        PyScript/phone connects and writes identify token
//  *                        to enable sensor NOTIFY data stream.
//  *
//  *          CLIENT mode : ESP32 scans, user selects device from UI,
//  *                        ESP32 connects and pushes sensor data via WRITE.
//  *
//  *          All BLE stack objects are owned exclusively by this module.
//  *          BLEScreenProcess accesses state only via the public API below —
//  *          never touches BLE stack objects directly. No conflicts possible.
//  *
//  *          Mirrors WiFiComm.cpp pattern for consistency and reusability.
//  *
//  * @version 1.0.0
//  */

// /*==============================================================================
//  *                              INCLUDES
//  * Arduino.h and BLE headers MUST come before all others —
//  * BLEServer, BLECharacteristic etc. are defined here.
//  *============================================================================*/
// #include <Arduino.h>
// #include <BLEDevice.h>
// #include <BLEServer.h>
// #include <BLEService.h>
// #include <BLECharacteristic.h>
// #include <BLEAdvertising.h>
// #include <BLE2902.h>
// #include <BLEScan.h>
// #include <BLEAdvertisedDevice.h>
// #include <BLEClient.h>
// #include <BLERemoteCharacteristic.h>
// #include <BLERemoteService.h>
// #include <freertos/FreeRTOS.h>
// #include <freertos/task.h>

// #include "BLEComm.h"
// #include "BLEComm_Cfg.h"
// #include "AQ_LEDHMI.h"

// /*==============================================================================
//  *                              PRIVATE TYPES
//  *============================================================================*/

// /**
//  * @brief Scanned device descriptor
//  */
// typedef struct
// {
//     char    name[32U];     /**< Advertised device name          */
//     char    address[18U];  /**< BLE MAC address string          */
//     int32_t rssi;          /**< Signal strength in dBm          */
//     bool    valid;         /**< Entry is populated              */
// } BLEComm_Device_t;

// /*==============================================================================
//  *                          PRIVATE VARIABLES
//  *============================================================================*/

// /* ── Server state ─────────────────────────────────────────────────────────── */
// static BLEServer*         s_pServer           = NULL;
// static BLECharacteristic* s_pSensorChar       = NULL;
// static BLECharacteristic* s_pIdentifyChar     = NULL;
// static bool               s_serverStarted     = false;
// static bool               s_clientConnected   = false;  /**< Any client on server   */
// static bool               s_pyScriptConnected = false;  /**< PyScript identified     */
// static char               s_serverClientName[32U] = {0U};

// /* ── Client state ─────────────────────────────────────────────────────────── */
// static BLEClient*               s_pBLEClient    = NULL;
// static BLERemoteCharacteristic* s_pPyScriptChar = NULL;
// static bool                     s_isConnected   = false;
// static bool                     s_pyClientReady = false;
// static char                     s_connectedName[32U] = {0U};
// static char                     s_connectedAddr[18U] = {0U};

// /* ── Scan state ───────────────────────────────────────────────────────────── */
// static BLEScan*        s_pBLEScan      = NULL;
// static BLEComm_Device_t s_deviceList[BLECOMM_MAX_DEVICES];
// static uint8_t         s_deviceCount   = 0U;
// static bool            s_isScanning    = false;
// static bool            s_scanComplete  = false;

// /* ── Connect task state ───────────────────────────────────────────────────── */
// static TaskHandle_t  s_connectTaskHandle = NULL;
// static char          s_connectAddr[18U]  = {0U};
// static volatile bool s_connectPending    = false;
// static bool          s_isConnecting      = false;
// static int8_t        s_connectingIndex   = -1;

// /* ── Cross-task dirty flag ────────────────────────────────────────────────── */
// static volatile bool s_bleStatusDirty = false;
// static volatile bool s_bleConnectedEvent = false;
// /* ── Parent screen (for UI back navigation) ───────────────────────────────── */
// static int32_t s_parentScreen = 1;

// /*==============================================================================
//  *                      PRIVATE FUNCTION PROTOTYPES
//  *============================================================================*/
// static void BLEComm__SpawnConnectTask(void);
// static void BLEComm__ConnectTask(void *pvParameters);

// /*==============================================================================
//  *                      BLE CALLBACK CLASSES
//  * All callbacks set s_bleStatusDirty = true instead of calling display
//  * functions directly — BLE callbacks run in the BLE stack task, NOT in
//  * loop(). Calling SPI/TFT from here races with loop() and blanks the display.
//  *============================================================================*/

// /**
//  * @brief Server connection callbacks — tracks phone/PyScript → ESP32
//  */
// class BLEComm_ServerCallbacks : public BLEServerCallbacks
// {
//     void onConnect(BLEServer *pServer) override
//     {
//         s_clientConnected   = true;
//         s_pyScriptConnected = false;
//         strncpy(s_serverClientName, "Device", 31U);
//         s_serverClientName[31U] = '\0';
//         BLECOMM_DEBUG_PRINTLN("[BLEComm] Server: client connected — waiting for identification");
//         s_bleStatusDirty = true;
//         LEDHMI__BleConnected();
//     }

//     void onDisconnect(BLEServer *pServer) override
//     {
//         s_clientConnected   = false;
//         s_pyScriptConnected = false;
//         memset(s_serverClientName, 0, sizeof(s_serverClientName));
//         BLECOMM_DEBUG_PRINTLN("[BLEComm] Server: client disconnected — restarting advertising");
//         BLEDevice::startAdvertising();
//         s_bleStatusDirty = true;
//         LEDHMI__BleDisconnected();
//     }
// };

// /**
//  * @brief Identify characteristic write callback — enables sensor data stream
//  * @details PyScript writes BLECOMM_IDENTIFY_TOKEN to activate NOTIFY.
//  *          Generic phone connections are accepted but receive no sensor data.
//  */
// class BLEComm_IdentifyCallback : public BLECharacteristicCallbacks
// {
//     void onWrite(BLECharacteristic *pChar) override
//     {
//         String value = pChar->getValue().c_str();
//         BLECOMM_DEBUG_PRINTF("[BLEComm] Identify received: %s\n", value.c_str());

//         if (value == BLECOMM_IDENTIFY_TOKEN)
//         {
//             s_pyScriptConnected = true;
//             strncpy(s_serverClientName, "PyScript", 31U);
//             s_serverClientName[31U] = '\0';
//             BLECOMM_DEBUG_PRINTLN("[BLEComm] PyScript identified — sensor data ON");
//         }
//         else
//         {
//             s_pyScriptConnected = false;
//             BLECOMM_DEBUG_PRINTF("[BLEComm] Unknown client token — sensor data OFF\n");
//         }

//         s_bleStatusDirty = true;
//     }
// };

// /**
//  * @brief BLE client connection callbacks — tracks ESP32 → remote device
//  */
// class BLEComm_ClientCallbacks : public BLEClientCallbacks
// {
//     void onConnect(BLEClient *pClient) override
//     {
//         s_isConnected = true;
//         BLECOMM_DEBUG_PRINTF("[BLEComm] Client connected to: %s\n", s_connectedName);
//         s_bleStatusDirty = true;
//          LEDHMI__BleConnected();   
//     }

//     void onDisconnect(BLEClient *pClient) override
//     {
//         s_isConnected   = false;
//         s_pyClientReady = false;
//         s_pPyScriptChar = NULL;
//         BLECOMM_DEBUG_PRINTLN("[BLEComm] Client disconnected");
//         s_bleStatusDirty = true;
//         LEDHMI__BleDisconnected(); 
//     }
// };

// /**
//  * @brief BLE scan result callback — populates device list
//  */
// class BLEComm_ScanCallback : public BLEAdvertisedDeviceCallbacks
// {
//     void onResult(BLEAdvertisedDevice advertisedDevice) override
//     {
//         if (s_deviceCount >= BLECOMM_MAX_DEVICES)
//         {
//             return;
//         }

//         String devName = advertisedDevice.getName();
//         String devAddr = advertisedDevice.getAddress().toString();

//         if (devName.length() == 0U)
//         {
//             devName = devAddr;
//         }

//         /* Duplicate address check */
//         for (uint8_t i = 0U; i < s_deviceCount; i++)
//         {
//             if (strncmp(s_deviceList[i].address, devAddr.c_str(), 17U) == 0)
//             {
//                 return;
//             }
//         }

//         strncpy(s_deviceList[s_deviceCount].name,    devName.c_str(), 31U);
//         strncpy(s_deviceList[s_deviceCount].address, devAddr.c_str(), 17U);
//         s_deviceList[s_deviceCount].name[31U]    = '\0';
//         s_deviceList[s_deviceCount].address[17U] = '\0';
//         s_deviceList[s_deviceCount].rssi         = (int32_t)advertisedDevice.getRSSI();
//         s_deviceList[s_deviceCount].valid        = true;
//         s_deviceCount++;

//         BLECOMM_DEBUG_PRINTF("[BLEComm] Found: %s  RSSI: %d\n",
//                              devName.c_str(), (int)advertisedDevice.getRSSI());
//         s_bleStatusDirty = true;
//     }
// };

// /* Static callback instances — one of each, owned by this module */
// static BLEComm_ServerCallbacks  s_serverCallbacks;
// static BLEComm_IdentifyCallback s_identifyCallback;
// static BLEComm_ClientCallbacks  s_clientCallbacks;
// static BLEComm_ScanCallback     s_scanCallback;

// /*==============================================================================
//  *                      PUBLIC FUNCTION IMPLEMENTATIONS
//  *============================================================================*/

// /**
//  * @brief Initialize BLE communication module
//  */
// void BLEComm__Init(void)
// {
//     if (s_serverStarted)
//     {
//         return;
//     }

//     BLECOMM_DEBUG_PRINTLN("\n[BLEComm] ========== INITIALIZING ==========");
//     BLECOMM_DEBUG_PRINTF("[BLEComm] Device name : %s\n", BLECOMM_DEVICE_NAME);
//     BLECOMM_DEBUG_PRINTF("[BLEComm] Service UUID : %s\n", BLECOMM_SERVICE_UUID);

//     /* Initialize BLE stack — once per boot */
//     BLEDevice::init(BLECOMM_DEVICE_NAME);

//     /* Create server */
//     s_pServer = BLEDevice::createServer();
//     s_pServer->setCallbacks(&s_serverCallbacks);

//     /* Create sensor data service */
//     BLEService *pService = s_pServer->createService(BLECOMM_SERVICE_UUID);

//     /* Sensor characteristic — READ + NOTIFY
//      * ESP32 pushes sensor JSON to identified client */
//     s_pSensorChar = pService->createCharacteristic(
//         BLECOMM_SENSOR_CHAR_UUID,
//         BLECharacteristic::PROPERTY_READ   |
//         BLECharacteristic::PROPERTY_NOTIFY
//     );
//     s_pSensorChar->addDescriptor(new BLE2902());

//     /* Identify characteristic — WRITE
//      * Client writes BLECOMM_IDENTIFY_TOKEN to enable data stream */
//     s_pIdentifyChar = pService->createCharacteristic(
//         BLECOMM_IDENTIFY_CHAR_UUID,
//         BLECharacteristic::PROPERTY_WRITE
//     );
//     s_pIdentifyChar->setCallbacks(&s_identifyCallback);

//     pService->start();

//     /* Start advertising */
//     BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
//     pAdvertising->addServiceUUID(BLECOMM_SERVICE_UUID);
//     pAdvertising->setScanResponse(true);
//     pAdvertising->setMinPreferred(0x06);
//     pAdvertising->setMinPreferred(0x12);
//     BLEDevice::startAdvertising();

//     s_serverStarted  = true;
//     s_bleStatusDirty = false;  /* Clear any flag set during init */

//     BLECOMM_DEBUG_PRINTF("[BLEComm] Advertising as '%s' — ready\n", BLECOMM_DEVICE_NAME);
//     BLECOMM_DEBUG_PRINTLN("[BLEComm] ========== INIT COMPLETE ==========\n");
// }

// /**
//  * @brief BLE communication periodic handler — call every 100ms from scheduler
//  */
// void BLEComm__Handler(void)
// {

//      static bool s_prevBleConnected = false;
//     bool        nowConnected       = s_bleConnectedEvent;

//     if (nowConnected != s_prevBleConnected)
//     {
//         if (nowConnected) { LEDHMI__BleConnected();    }
//         else              { LEDHMI__BleDisconnected(); }
//         s_prevBleConnected = nowConnected;
//     }
    
//     /* Poll connect task completion — clear connecting state when task finishes */
//     if (s_connectPending && (s_connectTaskHandle == NULL))
//     {
//         s_connectPending  = false;
//         s_isConnecting    = false;
//         s_connectingIndex = -1;
//     }

//     /* If server started but WiFi/BLE advertising stopped unexpectedly, restart */
//     if (s_serverStarted && !s_clientConnected && !BLEDevice::getAdvertising()->isAdvertising())
//     {
//         BLECOMM_DEBUG_PRINTLN("[BLEComm] Advertising stopped unexpectedly — restarting");
//         BLEDevice::startAdvertising();
//     }
// }

// /*──────────────────────────────────────────────────────────────────────────────
//  *  Data Send API
//  *────────────────────────────────────────────────────────────────────────────*/

// /**
//  * @brief Send sensor data via BLE server NOTIFY (SERVER mode)
//  */
// void BLEComm__SendSensorData(const char *json)
// {
//     if (!s_serverStarted)
//     {
//         return;
//     }
//     if (s_pSensorChar == NULL)
//     {
//         return;
//     }
//     if (!s_pyScriptConnected)
//     {
//         return;
//     }

//     s_pSensorChar->setValue((uint8_t *)json, strlen(json));
//     s_pSensorChar->notify();
//     BLECOMM_DEBUG_PRINTF("[BLEComm] Server NOTIFY: %s\n", json);
// }

// /**
//  * @brief Send sensor data via BLE client WRITE (CLIENT mode)
//  */
// void BLEComm__SendSensorDataToClient(const char *json)
// {
//     if (!s_pyClientReady)
//     {
//         return;
//     }
//     if (s_pPyScriptChar == NULL)
//     {
//         return;
//     }
//     if (s_pBLEClient == NULL)
//     {
//         return;
//     }
//     if (!s_pBLEClient->isConnected())
//     {
//         s_isConnected   = false;
//         s_pyClientReady = false;
//         s_pPyScriptChar = NULL;
//         return;
//     }

//     s_pPyScriptChar->writeValue((uint8_t *)json, strlen(json), false);
//     BLECOMM_DEBUG_PRINTF("[BLEComm] Client WRITE: %s\n", json);
// }

// /*──────────────────────────────────────────────────────────────────────────────
//  *  Scan & Connect API
//  *────────────────────────────────────────────────────────────────────────────*/

// /**
//  * @brief Start BLE scan
//  */
// void BLEComm__StartScan(void)
// {
//     s_deviceCount  = 0U;
//     s_scanComplete = false;
//     s_isScanning   = true;
//     memset(s_deviceList, 0, sizeof(s_deviceList));

//     BLECOMM_DEBUG_PRINTLN("[BLEComm] Starting scan...");

//     if (s_pBLEScan == NULL)
//     {
//         s_pBLEScan = BLEDevice::getScan();
//         s_pBLEScan->setAdvertisedDeviceCallbacks(&s_scanCallback);
//         s_pBLEScan->setActiveScan(true);
//         s_pBLEScan->setInterval((uint16_t)BLECOMM_SCAN_INTERVAL);
//         s_pBLEScan->setWindow((uint16_t)BLECOMM_SCAN_WINDOW);
//     }

//     s_pBLEScan->start((uint32_t)BLECOMM_SCAN_DURATION_SEC,
//                       [](BLEScanResults results)
//                       {
//                           s_isScanning   = false;
//                           s_scanComplete = true;
//                           BLECOMM_DEBUG_PRINTF("[BLEComm] Scan complete. Found %d devices.\n",
//                                               (int)s_deviceCount);
//                           s_bleStatusDirty = true;
//                       },
//                       false);
// }

// /**
//  * @brief Stop active BLE scan
//  */
// void BLEComm__StopScan(void)
// {
//     if ((s_pBLEScan != NULL) && s_isScanning)
//     {
//         s_pBLEScan->stop();
//         s_isScanning   = false;
//         s_scanComplete = true;
//     }
// }

// /**
//  * @brief Connect to scanned device by index (non-blocking)
//  */
// void BLEComm__ConnectToDevice(uint8_t index)
// {
//     if (index >= s_deviceCount)
//     {
//         BLECOMM_DEBUG_PRINTF("[BLEComm] ConnectToDevice: index %d out of range\n", (int)index);
//         return;
//     }

//     /* Disconnect if already connected to this device */
//     if (s_isConnected &&
//         (strncmp(s_deviceList[index].address, s_connectedAddr, 17U) == 0))
//     {
//         BLECOMM_DEBUG_PRINTF("[BLEComm] Disconnecting from: %s\n", s_deviceList[index].name);
//         if ((s_pBLEClient != NULL) && s_pBLEClient->isConnected())
//         {
//             s_pBLEClient->disconnect();
//         }
//         s_isConnected = false;
//         memset(s_connectedName, 0, sizeof(s_connectedName));
//         memset(s_connectedAddr, 0, sizeof(s_connectedAddr));
//         s_bleStatusDirty = true;
//         return;
//     }

//     BLECOMM_DEBUG_PRINTF("[BLEComm] Connecting to: %s  [%s]\n",
//                          s_deviceList[index].name,
//                          s_deviceList[index].address);

//     BLEComm__StopScan();

//     strncpy(s_connectedName, s_deviceList[index].name,    31U);
//     strncpy(s_connectedAddr, s_deviceList[index].address, 17U);
//     s_connectedName[31U] = '\0';
//     s_connectedAddr[17U] = '\0';

//     s_isConnecting    = true;
//     s_connectingIndex = (int8_t)index;

//     BLEComm__SpawnConnectTask();
// }

// /**
//  * @brief Disconnect from currently connected device
//  */
// void BLEComm__Disconnect(void)
// {
//     if ((s_pBLEClient != NULL) && s_pBLEClient->isConnected())
//     {
//         s_pBLEClient->disconnect();
//     }
//     s_isConnected   = false;
//     s_pyClientReady = false;
//     s_pPyScriptChar = NULL;
//     memset(s_connectedName, 0, sizeof(s_connectedName));
//     memset(s_connectedAddr, 0, sizeof(s_connectedAddr));
//     s_bleStatusDirty = true;
// }

// /*──────────────────────────────────────────────────────────────────────────────
//  *  State Query API
//  *────────────────────────────────────────────────────────────────────────────*/

// BLEComm_ServerState BLEComm__GetServerState(void)
// {
//     if (s_pyScriptConnected)  { return BLECOMM_SERVER_IDENTIFIED;   }
//     if (s_clientConnected)    { return BLECOMM_SERVER_CONNECTED;     }
//     return BLECOMM_SERVER_DISCONNECTED;
// }

// BLEComm_ClientState BLEComm__GetClientState(void)
// {
//     if (s_isConnected)   { return BLECOMM_CLIENT_CONNECTED;  }
//     if (s_isConnecting)  { return BLECOMM_CLIENT_CONNECTING; }
//     return BLECOMM_CLIENT_DISCONNECTED;
// }

// bool BLEComm__IsConnected(void)          { return s_isConnected;          }
// bool BLEComm__IsSendingData(void)        { return s_serverStarted && s_pyScriptConnected; }
// bool BLEComm__IsClientSendingData(void)  { return s_pyClientReady && s_isConnected;       }
// bool BLEComm__IsScanning(void)           { return s_isScanning;            }
// bool BLEComm__IsScanComplete(void)       { return s_scanComplete;          }
// bool BLEComm__IsServerClientConnected(void) { return s_clientConnected;    }
// bool BLEComm__IsPyScriptConnected(void)  { return s_pyScriptConnected;     }

// uint8_t BLEComm__GetDeviceCount(void)    { return s_deviceCount;           }

// const char *BLEComm__GetDeviceName(uint8_t index)
// {
//     if (index >= s_deviceCount) { return NULL; }
//     return s_deviceList[index].name;
// }

// const char *BLEComm__GetDeviceAddress(uint8_t index)
// {
//     if (index >= s_deviceCount) { return NULL; }
//     return s_deviceList[index].address;
// }

// int32_t BLEComm__GetDeviceRSSI(uint8_t index)
// {
//     if (index >= s_deviceCount) { return 0; }
//     return s_deviceList[index].rssi;
// }

// bool BLEComm__IsDeviceConnected(uint8_t index)
// {
//     if (index >= s_deviceCount) { return false; }
//     return (s_isConnected &&
//             (strncmp(s_deviceList[index].address, s_connectedAddr, 17U) == 0));
// }

// bool BLEComm__IsDeviceConnecting(uint8_t index)
// {
//     return (s_isConnecting && (s_connectingIndex == (int8_t)index));
// }

// const char *BLEComm__GetServerClientName(void)  { return s_serverClientName; }
// const char *BLEComm__GetConnectedDeviceName(void)
// {
//     return s_isConnected ? s_connectedName : NULL;
// }

// /*──────────────────────────────────────────────────────────────────────────────
//  *  Dirty Flag API
//  *────────────────────────────────────────────────────────────────────────────*/

// bool BLEComm__IsStatusDirty(void)   { return s_bleStatusDirty;  }
// void BLEComm__ClearStatusDirty(void){ s_bleStatusDirty = false; }
// void BLEComm__SetStatusDirty(void)  { s_bleStatusDirty = true;  }

// /*==============================================================================
//  *                      PRIVATE FUNCTION IMPLEMENTATIONS
//  *============================================================================*/

// /**
//  * @brief BLE connect task — runs in background, never blocks loop()
//  */
// static void BLEComm__ConnectTask(void *pvParameters)
// {
//     (void)pvParameters;

//     BLECOMM_DEBUG_PRINTF("[BLEComm] Connect task started for: %s\n", s_connectAddr);

//     if (s_pBLEClient == NULL)
//     {
//         s_pBLEClient = BLEDevice::createClient();
//         s_pBLEClient->setClientCallbacks(&s_clientCallbacks);
//     }

//     BLEAddress bleAddr(s_connectAddr);
//     bool connected = s_pBLEClient->connect(bleAddr);

//     s_isConnecting    = false;
//     s_connectingIndex = -1;
//     s_connectPending  = false;

//     if (connected)
//     {
//         s_isConnected   = true;
//         s_pyClientReady = false;

//         BLECOMM_DEBUG_PRINTF("[BLEComm] Connected to: %s\n", s_connectedName);

//         /* If connected to PyScript — get write characteristic for sensor push */
//         if (strncmp(s_connectedName, BLECOMM_PYSCRIPT_DEVICE_NAME,
//                     strlen(BLECOMM_PYSCRIPT_DEVICE_NAME)) == 0)
//         {
//             BLERemoteService *pSvc =
//                 s_pBLEClient->getService(BLEUUID(BLECOMM_PYSCRIPT_SERVICE_UUID));

//             if (pSvc != NULL)
//             {
//                 s_pPyScriptChar =
//                     pSvc->getCharacteristic(BLEUUID(BLECOMM_PYSCRIPT_CHAR_UUID));

//                 if (s_pPyScriptChar != NULL)
//                 {
//                     s_pyClientReady = true;
//                     BLECOMM_DEBUG_PRINTLN("[BLEComm] PyScript char found — ready to send");
//                 }
//                 else
//                 {
//                     BLECOMM_DEBUG_PRINTLN("[BLEComm] WARN: PyScript sensor char not found");
//                 }
//             }
//             else
//             {
//                 BLECOMM_DEBUG_PRINTLN("[BLEComm] WARN: PyScript service not found");
//             }
//         }
//     }
//     else
//     {
//         s_isConnected   = false;
//         s_pyClientReady = false;
//         s_pPyScriptChar = NULL;
//         memset(s_connectedName, 0, sizeof(s_connectedName));
//         memset(s_connectedAddr, 0, sizeof(s_connectedAddr));
//         BLECOMM_DEBUG_PRINTLN("[BLEComm] Connection failed");
//     }

//     /* Signal UI to redraw — safe cross-task flag, never call SPI here */
//     s_bleStatusDirty    = true;
//     s_connectTaskHandle = NULL;
//     vTaskDelete(NULL);
// }

// /**
//  * @brief Spawn the connect background task
//  */
// static void BLEComm__SpawnConnectTask(void)
// {
//     if (s_connectPending)
//     {
//         BLECOMM_DEBUG_PRINTLN("[BLEComm] Connect already in progress — ignoring");
//         return;
//     }

//     s_connectPending = true;
//     strncpy(s_connectAddr, s_connectedAddr, 17U);
//     s_connectAddr[17U] = '\0';

//     BaseType_t result = xTaskCreate(
//         BLEComm__ConnectTask,
//         "BLEConnect",
//         (uint32_t)BLECOMM_CONNECT_TASK_STACK,
//         NULL,
//         (UBaseType_t)BLECOMM_CONNECT_TASK_PRIORITY,
//         &s_connectTaskHandle
//     );

//     if (result != pdPASS)
//     {
//         BLECOMM_DEBUG_PRINTLN("[BLEComm] ERROR: Failed to create connect task");
//         s_connectPending  = false;
//         s_isConnecting    = false;
//         s_connectingIndex = -1;
//     }
// }

/**
 * @file BLEComm.cpp
 * @brief Generic BLE Communication Module Implementation
 * @details Implements BLE Server and BLE Client communication for ESP32-S3.
 *
 *          SERVER mode : ESP32 advertises as BLECOMM_DEVICE_NAME.
 *                        PyScript/phone connects and writes identify token
 *                        to enable sensor NOTIFY data stream.
 *
 *          CLIENT mode : ESP32 scans, user selects device from UI,
 *                        ESP32 connects and pushes sensor data via WRITE.
 *
 *          All BLE stack objects are owned exclusively by this module.
 *          BLEScreenProcess accesses state only via the public API below —
 *          never touches BLE stack objects directly. No conflicts possible.
 *
 *          Mirrors WiFiComm.cpp pattern for consistency and reusability.
 *
 * @version 1.0.0
 */

/*==============================================================================
 *                              INCLUDES
 * Arduino.h and BLE headers MUST come before all others —
 * BLEServer, BLECharacteristic etc. are defined here.
 *============================================================================*/
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEService.h>
#include <BLECharacteristic.h>
#include <BLEAdvertising.h>
#include <BLE2902.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLERemoteCharacteristic.h>
#include <BLERemoteService.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "mbedtls/aes.h"
#include "WiFiComm.h"
#include <Preferences.h>

#define BLECOMM_WIFI_PROV_CHAR_UUID  "6E400004-B5A3-F393-E0A9-E50E24DCCA9E"

#include "BLEComm.h"
#include "BLEComm_Cfg.h"
#include "AQ_LEDHMI.h"

/*==============================================================================
 *                              PRIVATE TYPES
 *============================================================================*/

/**
 * @brief Scanned device descriptor
 */
typedef struct
{
    char    name[32U];     /**< Advertised device name          */
    char    address[18U];  /**< BLE MAC address string          */
    int32_t rssi;          /**< Signal strength in dBm          */
    bool    valid;         /**< Entry is populated              */
} BLEComm_Device_t;

/*==============================================================================
 *                          PRIVATE VARIABLES
 *============================================================================*/

/* ── Server state ─────────────────────────────────────────────────────────── */
static BLEServer*         s_pServer           = NULL;
static BLECharacteristic* s_pSensorChar       = NULL;
static BLECharacteristic* s_pIdentifyChar     = NULL;
static BLECharacteristic* s_pWiFiProvChar     = NULL;
static bool               s_serverStarted     = false;
static bool               s_clientConnected   = false;  /**< Any client on server   */
static bool               s_pyScriptConnected = false;  /**< PyScript identified     */
static char               s_serverClientName[32U] = {0U};

/* ── Client state ─────────────────────────────────────────────────────────── */
static BLEClient*               s_pBLEClient    = NULL;
static BLERemoteCharacteristic* s_pPyScriptChar = NULL;
static bool                     s_isConnected   = false;
static bool                     s_pyClientReady = false;
static char                     s_connectedName[32U] = {0U};
static char                     s_connectedAddr[18U] = {0U};

/* ── Scan state ───────────────────────────────────────────────────────────── */
static BLEScan*        s_pBLEScan      = NULL;
static BLEComm_Device_t s_deviceList[BLECOMM_MAX_DEVICES];
static uint8_t         s_deviceCount   = 0U;
static bool            s_isScanning    = false;
static bool            s_scanComplete  = false;

/* ── Connect task state ───────────────────────────────────────────────────── */
static TaskHandle_t  s_connectTaskHandle = NULL;
static char          s_connectAddr[18U]  = {0U};
static volatile bool s_connectPending    = false;
static bool          s_isConnecting      = false;
static int8_t        s_connectingIndex   = -1;

/* ── Cross-task dirty flag ────────────────────────────────────────────────── */
static volatile bool s_bleStatusDirty = false;
/* ── Parent screen (for UI back navigation) ───────────────────────────────── */
static int32_t s_parentScreen = 1;

/*==============================================================================
 *                      PRIVATE FUNCTION PROTOTYPES
 *============================================================================*/
static void BLEComm__SpawnConnectTask(void);
static void BLEComm__ConnectTask(void *pvParameters);

/*==============================================================================
 *                      BLE CALLBACK CLASSES
 * All callbacks set s_bleStatusDirty = true instead of calling display
 * functions directly — BLE callbacks run in the BLE stack task, NOT in
 * loop(). Calling SPI/TFT from here races with loop() and blanks the display.
 *============================================================================*/

/**
 * @brief Server connection callbacks — tracks phone/PyScript → ESP32
 */
class BLEComm_ServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *pServer) override
    {
        s_clientConnected   = true;
        s_pyScriptConnected = false;
        strncpy(s_serverClientName, "Device", 31U);
        s_serverClientName[31U] = '\0';
        BLECOMM_DEBUG_PRINTLN("[BLEComm] Server: client connected — sending AUTH_REQ");
        BLECOMM_DEBUG_PRINTF("[BLEComm] Identify char handle: 0x%04X\n",
            (s_pIdentifyChar != NULL) ? s_pIdentifyChar->getHandle() : 0xFFFFU);
        s_bleStatusDirty = true;
        LEDHMI__BleConnected();

        /* Notify Python that authentication is required.
         * Python subscribes to NOTIFY then waits for this string
         * before showing the auth panel and sending encrypted token. */
        if (s_pSensorChar != NULL)
        {
            s_pSensorChar->setValue("AUTH_REQ");
            s_pSensorChar->notify();
            BLECOMM_DEBUG_PRINTLN("[BLEComm] AUTH_REQ sent via NOTIFY");
        }
    }

    void onDisconnect(BLEServer *pServer) override
    {
        Serial.println("[BLEComm] onDisconnect FIRED");
        s_clientConnected   = false;
        s_pyScriptConnected = false;
        memset(s_serverClientName, 0, sizeof(s_serverClientName));
        BLECOMM_DEBUG_PRINTLN("[BLEComm] Server: client disconnected");
        s_bleStatusDirty = true;
        /* NOTE: App.cpp state machine handles advertising restart — do NOT
         * call BLEDevice::startAdvertising() here. Calling it from BLEComm
         * bypasses App state machine and re-enables BLE after App_BLEStop(). */
        LEDHMI__BleDisconnected();
    }
};

/**
 * @brief Identify characteristic write callback — enables sensor data stream
 * @details PyScript writes BLECOMM_IDENTIFY_TOKEN to activate NOTIFY.
 *          Generic phone connections are accepted but receive no sensor data.
 */
class BLEComm_IdentifyCallback : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *pChar) override
    {
        String raw = pChar->getValue();

        /* DEBUG — confirm onWrite fires and report exact byte count */
        BLECOMM_DEBUG_PRINTF("[BLEComm] onWrite FIRED — char UUID: %s, len: %d\n",
            pChar->getUUID().toString().c_str(),
            (int)raw.length());

        if ((uint32_t)raw.length() == 16U)
        {
            /* AES-128 ECB decrypt — key and token must match AirPulse__mqtt.py */
            static const uint8_t s_aesKey[16U] = {
                'A','i','r','S','e','n','s','e','2','0','2','4','K','e','y','!'
            };
            static const uint8_t s_authToken[16U] = {
                'A','I','R','S','E','N','S','E','_','V','A','L','I','D','0','1'
            };

            uint8_t decrypted[16U] = {0U};
            mbedtls_aes_context aes;
            mbedtls_aes_init(&aes);
            mbedtls_aes_setkey_dec(&aes, s_aesKey, 128U);
            mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT,
                                  (const uint8_t *)raw.c_str(), decrypted);
            mbedtls_aes_free(&aes);

            if (memcmp(decrypted, s_authToken, 16U) == 0)
            {
                s_pyScriptConnected = true;
                strncpy(s_serverClientName, "PyScript", 31U);
                s_serverClientName[31U] = '\0';
                BLECOMM_DEBUG_PRINTLN("[BLEComm] AUTH_OK — sensor data ON");
                if (s_pSensorChar != NULL)
                {
                    s_pSensorChar->setValue("AUTH_OK");
                    s_pSensorChar->notify();
                }
            }
            else
            {
                s_pyScriptConnected = false;
                BLECOMM_DEBUG_PRINTLN("[BLEComm] AUTH_FAIL — wrong key");
                if (s_pSensorChar != NULL)
                {
                    s_pSensorChar->setValue("AUTH_FAIL");
                    s_pSensorChar->notify();
                }
            }
        }
        else
        {
            BLECOMM_DEBUG_PRINTF("[BLEComm] Auth: unexpected length %d (expected 16)\n",
                (int)raw.length());
        }

        s_bleStatusDirty = true;
    }
};

/**
 * @brief BLE client connection callbacks — tracks ESP32 → remote device
 */
class BLEComm_ClientCallbacks : public BLEClientCallbacks
{
    void onConnect(BLEClient *pClient) override
    {
        s_isConnected = true;
        BLECOMM_DEBUG_PRINTF("[BLEComm] Client connected to: %s\n", s_connectedName);
        s_bleStatusDirty = true;
        LEDHMI__BleConnected();
    }

    void onDisconnect(BLEClient *pClient) override
    {
        s_isConnected   = false;
        s_pyClientReady = false;
        s_pPyScriptChar = NULL;
        BLECOMM_DEBUG_PRINTLN("[BLEComm] Client disconnected");
        s_bleStatusDirty = true;
    }
};

/**
 * @brief BLE scan result callback — populates device list
 */
class BLEComm_ScanCallback : public BLEAdvertisedDeviceCallbacks
{
    void onResult(BLEAdvertisedDevice advertisedDevice) override
    {
        if (s_deviceCount >= BLECOMM_MAX_DEVICES)
        {
            return;
        }

        String devName = advertisedDevice.getName();
        String devAddr = advertisedDevice.getAddress().toString();

        if (devName.length() == 0U)
        {
            devName = devAddr;
        }

        /* Duplicate address check */
        for (uint8_t i = 0U; i < s_deviceCount; i++)
        {
            if (strncmp(s_deviceList[i].address, devAddr.c_str(), 17U) == 0)
            {
                return;
            }
        }

        strncpy(s_deviceList[s_deviceCount].name,    devName.c_str(), 31U);
        strncpy(s_deviceList[s_deviceCount].address, devAddr.c_str(), 17U);
        s_deviceList[s_deviceCount].name[31U]    = '\0';
        s_deviceList[s_deviceCount].address[17U] = '\0';
        s_deviceList[s_deviceCount].rssi         = (int32_t)advertisedDevice.getRSSI();
        s_deviceList[s_deviceCount].valid        = true;
        s_deviceCount++;

        BLECOMM_DEBUG_PRINTF("[BLEComm] Found: %s  RSSI: %d\n",
                             devName.c_str(), (int)advertisedDevice.getRSSI());
        s_bleStatusDirty = true;
    }
};

/* Static callback instances — one of each, owned by this module */
static BLEComm_ServerCallbacks  s_serverCallbacks;
static BLEComm_IdentifyCallback s_identifyCallback;
/**
 * @brief WiFi provisioning write callback — auth-gated
 */
class BLEComm_WiFiProvCallback : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *pChar) override
    {
        if (!s_pyScriptConnected)
        {
            BLECOMM_DEBUG_PRINTLN("[BLEComm] WiFiProv: rejected — not authenticated");
            if (s_pSensorChar != NULL) { s_pSensorChar->setValue("WIFI_AUTH_FAIL"); s_pSensorChar->notify(); }
            return;
        }

        String cmd = pChar->getValue();

        if (cmd == "WIFI:CLEAR")
        {
            BLECOMM_DEBUG_PRINTLN("[BLEComm] WiFiProv: clearing credentials");
            if (s_pSensorChar != NULL) { s_pSensorChar->setValue("WIFI_CLEARED"); s_pSensorChar->notify(); }
            delay(500U);
            WiFiComm__ClearCredentials();
            return;
        }

        if (cmd.startsWith("WIFI:"))
        {
            String payload  = cmd.substring(5);
            int    sep      = payload.indexOf(':');
            if (sep < 1)
            {
                if (s_pSensorChar != NULL) { s_pSensorChar->setValue("WIFI_FMT_ERR"); s_pSensorChar->notify(); }
                return;
            }
            String ssid     = payload.substring(0, sep);
            String password = payload.substring(sep + 1);
            if (ssid.length() == 0U)
            {
                if (s_pSensorChar != NULL) { s_pSensorChar->setValue("WIFI_SSID_ERR"); s_pSensorChar->notify(); }
                return;
            }
            BLECOMM_DEBUG_PRINTF("[BLEComm] WiFiProv: saving SSID='%s'\n", ssid.c_str());
            Preferences prefs;
            prefs.begin("wifi_creds", false);
            prefs.putString("ssid",     ssid.c_str());
            prefs.putString("password", password.c_str());
            prefs.end();
            if (s_pSensorChar != NULL) { s_pSensorChar->setValue("WIFI_SAVED"); s_pSensorChar->notify(); }
            delay(500U);
            ESP.restart();
            return;
        }

        if (s_pSensorChar != NULL) { s_pSensorChar->setValue("WIFI_CMD_ERR"); s_pSensorChar->notify(); }
    }
};
static BLEComm_WiFiProvCallback  s_wifiProvCallback;
static BLEComm_ClientCallbacks  s_clientCallbacks;
static BLEComm_ScanCallback     s_scanCallback;

/*==============================================================================
 *                      PUBLIC FUNCTION IMPLEMENTATIONS
 *============================================================================*/

/**
 * @brief Initialize BLE communication module
 */
void BLEComm__Init(void)
{
    if (s_serverStarted)
    {
        return;
    }

    BLECOMM_DEBUG_PRINTLN("\n[BLEComm] ========== INITIALIZING ==========");
    BLECOMM_DEBUG_PRINTF("[BLEComm] Device name : %s\n", BLECOMM_DEVICE_NAME);
    BLECOMM_DEBUG_PRINTF("[BLEComm] Service UUID : %s\n", BLECOMM_SERVICE_UUID);

    /* Initialize BLE stack — once per boot */
    BLEDevice::init(BLECOMM_DEVICE_NAME);

    /* Create server */
    s_pServer = BLEDevice::createServer();
    s_pServer->setCallbacks(&s_serverCallbacks);

    /* Create sensor data service */
    BLEService *pService = s_pServer->createService(BLECOMM_SERVICE_UUID);

    /* Sensor characteristic — READ + NOTIFY
     * ESP32 pushes sensor JSON to identified client */
    s_pSensorChar = pService->createCharacteristic(
        BLECOMM_SENSOR_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ   |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    s_pSensorChar->addDescriptor(new BLE2902());

    /* Identify characteristic — WRITE
     * Client writes BLECOMM_IDENTIFY_TOKEN to enable data stream */
    s_pIdentifyChar = pService->createCharacteristic(
        BLECOMM_IDENTIFY_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    s_pIdentifyChar->setCallbacks(&s_identifyCallback);

    s_pWiFiProvChar = pService->createCharacteristic(
        BLECOMM_WIFI_PROV_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    s_pWiFiProvChar->setCallbacks(&s_wifiProvCallback);

    pService->start();

    /* Start advertising */
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLECOMM_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    s_serverStarted  = true;
    s_bleStatusDirty = false;  /* Clear any flag set during init */

    BLECOMM_DEBUG_PRINTF("[BLEComm] Advertising as '%s' — ready\n", BLECOMM_DEVICE_NAME);
    BLECOMM_DEBUG_PRINTLN("[BLEComm] ========== INIT COMPLETE ==========\n");
}

/**
 * @brief BLE communication periodic handler — call every 100ms from scheduler
 */
void BLEComm__Handler(void)
{
    /* Poll connect task completion — clear connecting state when task finishes */
    if (s_connectPending && (s_connectTaskHandle == NULL))
    {
        s_connectPending  = false;
        s_isConnecting    = false;
        s_connectingIndex = -1;
    }

    /* Advertising restart intentionally removed from handler.
     * App.cpp state machine is the single authority for advertising start/stop.
     * BLEComm auto-restart was causing BLE to re-enable after App_BLEStop(). */
}

/*──────────────────────────────────────────────────────────────────────────────
 *  Data Send API
 *────────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Send sensor data via BLE server NOTIFY (SERVER mode)
 */
void BLEComm__SendSensorData(const char *json)
{
    if (!s_serverStarted)
    {
        return;
    }
    if (s_pSensorChar == NULL)
    {
        return;
    }
    if (!s_pyScriptConnected)
    {
        return;
    }

    s_pSensorChar->setValue((uint8_t *)json, strlen(json));
    s_pSensorChar->notify();
    BLECOMM_DEBUG_PRINTF("[BLEComm] Server NOTIFY: %s\n", json);
}

/**
 * @brief Send sensor data via BLE client WRITE (CLIENT mode)
 */
void BLEComm__SendSensorDataToClient(const char *json)
{
    if (!s_pyClientReady)
    {
        return;
    }
    if (s_pPyScriptChar == NULL)
    {
        return;
    }
    if (s_pBLEClient == NULL)
    {
        return;
    }
    if (!s_pBLEClient->isConnected())
    {
        s_isConnected   = false;
        s_pyClientReady = false;
        s_pPyScriptChar = NULL;
        return;
    }

    s_pPyScriptChar->writeValue((uint8_t *)json, strlen(json), false);
    BLECOMM_DEBUG_PRINTF("[BLEComm] Client WRITE: %s\n", json);
}

/*──────────────────────────────────────────────────────────────────────────────
 *  Scan & Connect API
 *────────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Start BLE scan
 */
void BLEComm__StartScan(void)
{
    s_deviceCount  = 0U;
    s_scanComplete = false;
    s_isScanning   = true;
    memset(s_deviceList, 0, sizeof(s_deviceList));

    BLECOMM_DEBUG_PRINTLN("[BLEComm] Starting scan...");

    if (s_pBLEScan == NULL)
    {
        s_pBLEScan = BLEDevice::getScan();
        s_pBLEScan->setAdvertisedDeviceCallbacks(&s_scanCallback);
        s_pBLEScan->setActiveScan(true);
        s_pBLEScan->setInterval((uint16_t)BLECOMM_SCAN_INTERVAL);
        s_pBLEScan->setWindow((uint16_t)BLECOMM_SCAN_WINDOW);
    }

    s_pBLEScan->start((uint32_t)BLECOMM_SCAN_DURATION_SEC,
                      [](BLEScanResults results)
                      {
                          s_isScanning   = false;
                          s_scanComplete = true;
                          BLECOMM_DEBUG_PRINTF("[BLEComm] Scan complete. Found %d devices.\n",
                                              (int)s_deviceCount);
                          s_bleStatusDirty = true;
                      },
                      false);
}

/**
 * @brief Stop active BLE scan
 */
void BLEComm__StopScan(void)
{
    if ((s_pBLEScan != NULL) && s_isScanning)
    {
        s_pBLEScan->stop();
        s_isScanning   = false;
        s_scanComplete = true;
    }
}

/**
 * @brief Connect to scanned device by index (non-blocking)
 */
void BLEComm__ConnectToDevice(uint8_t index)
{
    if (index >= s_deviceCount)
    {
        BLECOMM_DEBUG_PRINTF("[BLEComm] ConnectToDevice: index %d out of range\n", (int)index);
        return;
    }

    /* Disconnect if already connected to this device */
    if (s_isConnected &&
        (strncmp(s_deviceList[index].address, s_connectedAddr, 17U) == 0))
    {
        BLECOMM_DEBUG_PRINTF("[BLEComm] Disconnecting from: %s\n", s_deviceList[index].name);
        if ((s_pBLEClient != NULL) && s_pBLEClient->isConnected())
        {
            s_pBLEClient->disconnect();
        }
        s_isConnected = false;
        memset(s_connectedName, 0, sizeof(s_connectedName));
        memset(s_connectedAddr, 0, sizeof(s_connectedAddr));
        s_bleStatusDirty = true;
        return;
    }

    BLECOMM_DEBUG_PRINTF("[BLEComm] Connecting to: %s  [%s]\n",
                         s_deviceList[index].name,
                         s_deviceList[index].address);

    BLEComm__StopScan();

    strncpy(s_connectedName, s_deviceList[index].name,    31U);
    strncpy(s_connectedAddr, s_deviceList[index].address, 17U);
    s_connectedName[31U] = '\0';
    s_connectedAddr[17U] = '\0';

    s_isConnecting    = true;
    s_connectingIndex = (int8_t)index;

    BLEComm__SpawnConnectTask();
}

/**
 * @brief Disconnect from currently connected device
 */
void BLEComm__Disconnect(void)
{
    if ((s_pBLEClient != NULL) && s_pBLEClient->isConnected())
    {
        s_pBLEClient->disconnect();
    }
    s_isConnected   = false;
    s_pyClientReady = false;
    s_pPyScriptChar = NULL;
    memset(s_connectedName, 0, sizeof(s_connectedName));
    memset(s_connectedAddr, 0, sizeof(s_connectedAddr));
    s_bleStatusDirty = true;
}

/*──────────────────────────────────────────────────────────────────────────────
 *  State Query API
 *────────────────────────────────────────────────────────────────────────────*/

BLEComm_ServerState BLEComm__GetServerState(void)
{
    if (s_pyScriptConnected)  { return BLECOMM_SERVER_IDENTIFIED;   }
    if (s_clientConnected)    { return BLECOMM_SERVER_CONNECTED;     }
    return BLECOMM_SERVER_DISCONNECTED;
}

BLEComm_ClientState BLEComm__GetClientState(void)
{
    if (s_isConnected)   { return BLECOMM_CLIENT_CONNECTED;  }
    if (s_isConnecting)  { return BLECOMM_CLIENT_CONNECTING; }
    return BLECOMM_CLIENT_DISCONNECTED;
}

bool BLEComm__IsConnected(void)          { return s_isConnected;          }
bool BLEComm__IsSendingData(void)        { return s_serverStarted && s_pyScriptConnected; }
bool BLEComm__IsClientSendingData(void)  { return s_pyClientReady && s_isConnected;       }
bool BLEComm__IsScanning(void)           { return s_isScanning;            }
bool BLEComm__IsScanComplete(void)       { return s_scanComplete;          }
bool BLEComm__IsServerClientConnected(void) { return s_clientConnected;    }
bool BLEComm__IsPyScriptConnected(void)  { return s_pyScriptConnected;     }

uint8_t BLEComm__GetDeviceCount(void)    { return s_deviceCount;           }

const char *BLEComm__GetDeviceName(uint8_t index)
{
    if (index >= s_deviceCount) { return NULL; }
    return s_deviceList[index].name;
}

const char *BLEComm__GetDeviceAddress(uint8_t index)
{
    if (index >= s_deviceCount) { return NULL; }
    return s_deviceList[index].address;
}

int32_t BLEComm__GetDeviceRSSI(uint8_t index)
{
    if (index >= s_deviceCount) { return 0; }
    return s_deviceList[index].rssi;
}

bool BLEComm__IsDeviceConnected(uint8_t index)
{
    if (index >= s_deviceCount) { return false; }
    return (s_isConnected &&
            (strncmp(s_deviceList[index].address, s_connectedAddr, 17U) == 0));
}

bool BLEComm__IsDeviceConnecting(uint8_t index)
{
    return (s_isConnecting && (s_connectingIndex == (int8_t)index));
}

const char *BLEComm__GetServerClientName(void)  { return s_serverClientName; }
const char *BLEComm__GetConnectedDeviceName(void)
{
    return s_isConnected ? s_connectedName : NULL;
}

/*──────────────────────────────────────────────────────────────────────────────
 *  Dirty Flag API
 *────────────────────────────────────────────────────────────────────────────*/

bool BLEComm__IsStatusDirty(void)   { return s_bleStatusDirty;  }
void BLEComm__ClearStatusDirty(void){ s_bleStatusDirty = false; }
void BLEComm__SetStatusDirty(void)  { s_bleStatusDirty = true;  }

/*==============================================================================
 *                      PRIVATE FUNCTION IMPLEMENTATIONS
 *============================================================================*/

/**
 * @brief BLE connect task — runs in background, never blocks loop()
 */
static void BLEComm__ConnectTask(void *pvParameters)
{
    (void)pvParameters;

    BLECOMM_DEBUG_PRINTF("[BLEComm] Connect task started for: %s\n", s_connectAddr);

    if (s_pBLEClient == NULL)
    {
        s_pBLEClient = BLEDevice::createClient();
        s_pBLEClient->setClientCallbacks(&s_clientCallbacks);
    }

    BLEAddress bleAddr(s_connectAddr);
    bool connected = s_pBLEClient->connect(bleAddr);

    s_isConnecting    = false;
    s_connectingIndex = -1;
    s_connectPending  = false;

    if (connected)
    {
        s_isConnected   = true;
        s_pyClientReady = false;

        BLECOMM_DEBUG_PRINTF("[BLEComm] Connected to: %s\n", s_connectedName);

        /* If connected to PyScript — get write characteristic for sensor push */
        if (strncmp(s_connectedName, BLECOMM_PYSCRIPT_DEVICE_NAME,
                    strlen(BLECOMM_PYSCRIPT_DEVICE_NAME)) == 0)
        {
            BLERemoteService *pSvc =
                s_pBLEClient->getService(BLEUUID(BLECOMM_PYSCRIPT_SERVICE_UUID));

            if (pSvc != NULL)
            {
                s_pPyScriptChar =
                    pSvc->getCharacteristic(BLEUUID(BLECOMM_PYSCRIPT_CHAR_UUID));

                if (s_pPyScriptChar != NULL)
                {
                    s_pyClientReady = true;
                    BLECOMM_DEBUG_PRINTLN("[BLEComm] PyScript char found — ready to send");
                }
                else
                {
                    BLECOMM_DEBUG_PRINTLN("[BLEComm] WARN: PyScript sensor char not found");
                }
            }
            else
            {
                BLECOMM_DEBUG_PRINTLN("[BLEComm] WARN: PyScript service not found");
            }
        }
    }
    else
    {
        s_isConnected   = false;
        s_pyClientReady = false;
        s_pPyScriptChar = NULL;
        memset(s_connectedName, 0, sizeof(s_connectedName));
        memset(s_connectedAddr, 0, sizeof(s_connectedAddr));
        BLECOMM_DEBUG_PRINTLN("[BLEComm] Connection failed");
    }

    /* Signal UI to redraw — safe cross-task flag, never call SPI here */
    s_bleStatusDirty    = true;
    s_connectTaskHandle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief Spawn the connect background task
 */
static void BLEComm__SpawnConnectTask(void)
{
    if (s_connectPending)
    {
        BLECOMM_DEBUG_PRINTLN("[BLEComm] Connect already in progress — ignoring");
        return;
    }

    s_connectPending = true;
    strncpy(s_connectAddr, s_connectedAddr, 17U);
    s_connectAddr[17U] = '\0';

    BaseType_t result = xTaskCreate(
        BLEComm__ConnectTask,
        "BLEConnect",
        (uint32_t)BLECOMM_CONNECT_TASK_STACK,
        NULL,
        (UBaseType_t)BLECOMM_CONNECT_TASK_PRIORITY,
        &s_connectTaskHandle
    );

    if (result != pdPASS)
    {
        BLECOMM_DEBUG_PRINTLN("[BLEComm] ERROR: Failed to create connect task");
        s_connectPending  = false;
        s_isConnecting    = false;
        s_connectingIndex = -1;
    }
}