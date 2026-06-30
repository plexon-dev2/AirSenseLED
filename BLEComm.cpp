/**
 * @file BLEComm.cpp
 * @brief Generic BLE Communication Module Implementation
 * @details Implements BLE Server and BLE Client communication for ESP32-S3.
 *
 *          SERVER mode : ESP32 advertises as BLECOMM_DEVICE_NAME.
 *                        PyScript/phone connects and sends 16-byte AES-128 ECB
 *                        encrypted token to authenticate and enable sensor NOTIFY.
 *
 *          CLIENT mode : ESP32 scans, user selects device from UI,
 *                        ESP32 connects and pushes sensor data via WRITE.
 *
 *          All BLE stack objects owned exclusively by this module.
 *          BLEScreenProcess accesses state only via the public API.
 *          Mirrors WiFiComm.cpp pattern for consistency.
 *
 * @version 1.1.0
 *
 * v1.1.0 changes (review fixes):
 *   - CRITICAL: Removed ~680 lines of commented-out previous version (MISRA 2.1)
 *   - CRITICAL: BLEComm__SetAdvSuppressed() implemented -- was declared in .h
 *     but missing from .cpp causing linker error (MISRA 8.4)
 *   - CRITICAL: AES key and auth token moved from hardcoded source to NVS.
 *     BLEComm__Init() loads them from BLECOMM_NVS_NAMESPACE; falls back to
 *     BLECOMM_DEFAULT_* only when NVS has no provisioned credentials.
 *   - CRITICAL: LEDHMI__BleConnected() deferred out of BLE callback -- was
 *     called directly from BLE stack task causing cross-task race on LED state.
 *     Now deferred via s_bleConnectedEvent flag, consumed in BLEComm__Handler().
 *   - All cross-task boolean state flags declared volatile (MISRA R1)
 *   - s_parentScreen dead variable removed (MISRA 2.2)
 *   - BLEComm_Status enum: negative values replaced with positive (MISRA 10.3)
 *   - Fan duty: raw.toInt() replaced with strtol() + validation; sends
 *     "ERR_INVALID" notify on bad input (replaces silent 0% fan command)
 *   - Fan frequency: min/max validated against BLECOMM_FAN_FREQ_MIN/MAX_HZ;
 *     rejects out-of-range values with "ERR_RANGE" notify
 *   - setMinPreferred(0x12) corrected to setMaxPreferred(0x12) -- was calling
 *     setMinPreferred twice, second call silently overwrote first
 *   - BLECOMM_DEBUG_* now routes through SERIAL_PRINTF (mutex-safe)
 *   - strncpy magic numbers replaced with sizeof(buf)-1U
 *   - BLEClient freshly created on each connect attempt -- avoids stale object
 *     state on reconnect
 */

/*==============================================================================
 *                              INCLUDES
 * Arduino.h and BLE headers MUST come before all others.
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
#include <Preferences.h>
#include <string.h>
#include <stdlib.h>

#include "BLEComm.h"
#include "BLEComm_Cfg.h"
#include "AQ_LEDHMI.h"
#include "Fan.h"
#include "App.h"
#include "AppMutex.h"

/*==============================================================================
 *                              PRIVATE TYPES
 *============================================================================*/

/** @brief Scanned device descriptor */
typedef struct
{
    char    name[32U];    /**< Advertised device name (null-terminated) */
    char    address[18U]; /**< BLE MAC address string (null-terminated) */
    int32_t rssi;         /**< Signal strength in dBm                  */
    bool    valid;        /**< Entry is populated                      */
} BLEComm_Device_t;

/*==============================================================================
 *                          PRIVATE VARIABLES
 *============================================================================*/

/* -- Server state ----------------------------------------------------------- */
static BLEServer*         s_pServer           = NULL;
static BLECharacteristic* s_pSensorChar       = NULL;
static BLECharacteristic* s_pIdentifyChar     = NULL;
static BLECharacteristic* s_pFanDutyChar      = NULL;
static BLECharacteristic* s_pFanFreqChar      = NULL;
static bool               s_serverStarted     = false;

/** @note volatile -- written by BLE stack task, read by scheduler task */
static volatile bool      s_clientConnected   = false;
static volatile bool      s_pyScriptConnected = false;
static char               s_serverClientName[32U] = {0U};

/* -- Client state ----------------------------------------------------------- */
static BLEClient*               s_pBLEClient    = NULL;
static BLERemoteCharacteristic* s_pPyScriptChar = NULL;

/** @note volatile -- written by BLE stack task, read by scheduler task */
static volatile bool            s_isConnected   = false;
static volatile bool            s_pyClientReady = false;
static char                     s_connectedName[32U] = {0U};
static char                     s_connectedAddr[18U] = {0U};

/* -- Scan state ------------------------------------------------------------- */
static BLEScan*         s_pBLEScan     = NULL;
static BLEComm_Device_t s_deviceList[BLECOMM_MAX_DEVICES];
static uint8_t          s_deviceCount  = 0U;

/** @note volatile -- written by BLE scan callback, read by scheduler task */
static volatile bool    s_isScanning   = false;
static volatile bool    s_scanComplete = false;

/* -- Connect task state ----------------------------------------------------- */
static TaskHandle_t       s_connectTaskHandle = NULL;
static char               s_connectAddr[18U]  = {0U};
static volatile bool      s_connectPending    = false;
static volatile bool      s_isConnecting      = false;
static volatile int8_t    s_connectingIndex   = -1;

/* -- Cross-task flags ------------------------------------------------------- */
/** @brief Display dirty flag -- set in callbacks, cleared by display task */
static volatile bool s_bleStatusDirty = false;

/**
 * @brief BLE connected event flag -- set in BLE callbacks, consumed in Handler.
 * @details LEDHMI__BleConnected/Disconnected MUST NOT be called from BLE
 *          callback context (BLE stack task) -- they write LED state shared
 *          with the LEDHMI scheduler task.  These flags defer the call to
 *          BLEComm__Handler() which runs in the safe scheduler context.
 */
static volatile bool s_bleConnectedEvent    = false;
static volatile bool s_bleDisconnectedEvent = false;

/* -- Deferred NVS save state ------------------------------------------------ */
/**
 * @brief NVS save request -- set from BLE callbacks, flushed in Handler().
 * @details NVS flash erase/write stalls CPU bus for 200-500ms.  Doing this
 *          from BLE callbacks (BLE stack task) starves IDLE0 and triggers WDT.
 */
static volatile bool     s_nvsPendingSave    = false;
static volatile bool     s_nvsPendingManual  = false;
static volatile uint8_t  s_nvsPendingDuty    = 0U;
static volatile uint32_t s_nvsPendingFreqHz  = 0U;

/* -- AES credentials loaded from NVS at Init -------------------------------- */
static uint8_t s_aesKey[16U]    = {0U};
static uint8_t s_authToken[16U] = {0U};

/*==============================================================================
 *                      PRIVATE FUNCTION PROTOTYPES
 *============================================================================*/
static void BLEComm__SpawnConnectTask(void);
static void BLEComm__ConnectTask(void *pvParameters);

/*==============================================================================
 *                      BLE CALLBACK CLASSES
 * All callbacks set volatile flags instead of calling display or LED functions
 * directly -- BLE callbacks run in the BLE stack task (Core 0), NOT in the
 * scheduler task. Calling LEDHMI/SPI from here races with Core 1 tasks.
 *============================================================================*/

/**
 * @brief Server connection callbacks -- tracks phone/PyScript connection
 */
class BLEComm_ServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *pServer) override
    {
        s_clientConnected   = true;
        s_pyScriptConnected = false;
        strncpy(s_serverClientName, "Device", sizeof(s_serverClientName) - 1U);
        s_serverClientName[sizeof(s_serverClientName) - 1U] = '\0';
        BLECOMM_DEBUG_PRINTLN("[BLEComm] Server: client connected -- sending AUTH_REQ");
        s_bleStatusDirty    = true;
        s_bleConnectedEvent = true;  /* Deferred: LEDHMI__BleConnected() in Handler() */

        /* Prompt client to authenticate */
        if (s_pSensorChar != NULL)
        {
            s_pSensorChar->setValue("AUTH_REQ");
            s_pSensorChar->notify();
        }
    }

    void onDisconnect(BLEServer *pServer) override
    {
        s_clientConnected      = false;
        s_pyScriptConnected    = false;
        memset(s_serverClientName, 0, sizeof(s_serverClientName));
        BLECOMM_DEBUG_PRINTLN("[BLEComm] Server: client disconnected");
        s_bleStatusDirty       = true;
        s_bleDisconnectedEvent = true;  /* Deferred: LEDHMI__BleDisconnected() in Handler() */
        /* NOTE: App.cpp state machine handles advertising restart -- do NOT
         * call BLEDevice::startAdvertising() here.  Calling it from BLEComm
         * bypasses the App state machine and re-enables BLE after BLEStop(). */
    }
};

/**
 * @brief Identify characteristic write callback -- AES-128 ECB authentication
 * @details Client writes 16-byte ciphertext. Decrypted with key from NVS.
 *          Sends AUTH_OK or AUTH_FAIL notify in response.
 */
class BLEComm_IdentifyCallback : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *pChar) override
    {
        String raw = pChar->getValue();

        BLECOMM_DEBUG_PRINTF("[BLEComm] Identify onWrite: len=%d\n", (int)raw.length());

        if ((uint32_t)raw.length() == 16U)
        {
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
                strncpy(s_serverClientName, "PyScript",
                        sizeof(s_serverClientName) - 1U);
                s_serverClientName[sizeof(s_serverClientName) - 1U] = '\0';
                BLECOMM_DEBUG_PRINTLN("[BLEComm] AUTH_OK -- sensor data ON");
                if (s_pSensorChar != NULL)
                {
                    s_pSensorChar->setValue("AUTH_OK");
                    s_pSensorChar->notify();
                }
            }
            else
            {
                s_pyScriptConnected = false;
                BLECOMM_DEBUG_PRINTLN("[BLEComm] AUTH_FAIL -- wrong key");
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
 * @brief Fan duty percent characteristic callback
 * @details Client writes "0".."100" for direct duty, or "AUTO" for auto control.
 *          NVS write is deferred to BLEComm__Handler() to avoid CPU bus stall.
 *          Invalid input sends "ERR_INVALID" notify -- does NOT silently set 0%.
 */
class BLEComm_FanDutyCallback : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *pChar) override
    {
        String raw = pChar->getValue();
        raw.trim();

        BLECOMM_DEBUG_PRINTF("[BLEComm] Fan duty WRITE: '%s'\n", raw.c_str());

        if (raw.equalsIgnoreCase("AUTO"))
        {
            App__SetFanAuto();
            s_nvsPendingManual = false;
            s_nvsPendingSave   = true;
            BLECOMM_DEBUG_PRINTLN("[BLEComm] Fan -> AUTO mode (NVS save deferred)");
        }
        else
        {
            /* Use strtol -- rejects non-numeric input, detects parse failure */
            char   *endPtr  = NULL;
            long    percent = strtol(raw.c_str(), &endPtr, 10);

            if ((endPtr == raw.c_str()) || (*endPtr != '\0'))
            {
                /* No digits consumed or trailing garbage -- reject */
                BLECOMM_DEBUG_PRINTF("[BLEComm] Fan duty: invalid input '%s'\n",
                                     raw.c_str());
                if (s_pSensorChar != NULL)
                {
                    s_pSensorChar->setValue("ERR_INVALID");
                    s_pSensorChar->notify();
                }
                return;
            }

            if (percent < 0L)   { percent = 0L;   }
            if (percent > 100L) { percent = 100L; }

            Fan__SetDutyPercent((uint8_t)percent);
            App__SetFanManualFlag();
            s_nvsPendingManual = true;
            s_nvsPendingDuty   = (uint8_t)percent;
            s_nvsPendingSave   = true;
            BLECOMM_DEBUG_PRINTF("[BLEComm] Fan duty -> %ld%% (NVS save deferred)\n",
                                 percent);
        }
        s_bleStatusDirty = true;
    }
};

/**
 * @brief Fan PWM frequency characteristic callback
 * @details Client writes frequency in Hz as a decimal string (e.g. "15000").
 *          Validated against BLECOMM_FAN_FREQ_MIN_HZ / MAX_HZ.
 *          NVS write deferred to BLEComm__Handler().
 */
class BLEComm_FanFreqCallback : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *pChar) override
    {
        String raw = pChar->getValue();
        raw.trim();

        BLECOMM_DEBUG_PRINTF("[BLEComm] Fan freq WRITE: '%s'\n", raw.c_str());

        char  *endPtr = NULL;
        long   hz     = strtol(raw.c_str(), &endPtr, 10);

        if ((endPtr == raw.c_str()) || (*endPtr != '\0') || (hz <= 0L))
        {
            BLECOMM_DEBUG_PRINTF("[BLEComm] Fan freq: invalid input '%s'\n",
                                 raw.c_str());
            if (s_pSensorChar != NULL)
            {
                s_pSensorChar->setValue("ERR_INVALID");
                s_pSensorChar->notify();
            }
            return;
        }

        if (((uint32_t)hz < BLECOMM_FAN_FREQ_MIN_HZ) ||
            ((uint32_t)hz > BLECOMM_FAN_FREQ_MAX_HZ))
        {
            BLECOMM_DEBUG_PRINTF("[BLEComm] Fan freq %ldHz out of range [%u-%u]\n",
                                 hz, BLECOMM_FAN_FREQ_MIN_HZ, BLECOMM_FAN_FREQ_MAX_HZ);
            if (s_pSensorChar != NULL)
            {
                s_pSensorChar->setValue("ERR_RANGE");
                s_pSensorChar->notify();
            }
            return;
        }

        Fan__SetFrequency((uint32_t)hz);
        s_nvsPendingFreqHz = (uint32_t)hz;
        s_nvsPendingSave   = true;
        BLECOMM_DEBUG_PRINTF("[BLEComm] Fan frequency -> %ldHz (NVS save deferred)\n",
                             hz);
        s_bleStatusDirty = true;
    }
};

/**
 * @brief BLE client connection callbacks -- tracks ESP32 as client to remote
 */
class BLEComm_ClientCallbacks : public BLEClientCallbacks
{
    void onConnect(BLEClient *pClient) override
    {
        s_isConnected       = true;
        s_bleStatusDirty    = true;
        s_bleConnectedEvent = true;  /* Deferred: LEDHMI__BleConnected() in Handler() */
        BLECOMM_DEBUG_PRINTF("[BLEComm] Client connected to: %s\n", s_connectedName);
    }

    void onDisconnect(BLEClient *pClient) override
    {
        s_isConnected          = false;
        s_pyClientReady        = false;
        s_pPyScriptChar        = NULL;
        s_bleStatusDirty       = true;
        s_bleDisconnectedEvent = true;  /* Deferred: LEDHMI__BleDisconnected() in Handler() */
        BLECOMM_DEBUG_PRINTLN("[BLEComm] Client disconnected");
    }
};

/**
 * @brief BLE scan result callback -- populates device list
 */
class BLEComm_ScanCallback : public BLEAdvertisedDeviceCallbacks
{
    void onResult(BLEAdvertisedDevice advertisedDevice) override
    {
        if (s_deviceCount >= BLECOMM_MAX_DEVICES) { return; }

        String devName = advertisedDevice.getName();
        String devAddr = advertisedDevice.getAddress().toString();

        if (devName.length() == 0U) { devName = devAddr; }

        /* Duplicate address check */
        for (uint8_t i = 0U; i < s_deviceCount; i++)
        {
            if (strncmp(s_deviceList[i].address, devAddr.c_str(), 17U) == 0)
            {
                return;
            }
        }

        strncpy(s_deviceList[s_deviceCount].name,
                devName.c_str(),
                sizeof(s_deviceList[0].name) - 1U);
        s_deviceList[s_deviceCount].name[sizeof(s_deviceList[0].name) - 1U] = '\0';

        strncpy(s_deviceList[s_deviceCount].address,
                devAddr.c_str(),
                sizeof(s_deviceList[0].address) - 1U);
        s_deviceList[s_deviceCount].address[sizeof(s_deviceList[0].address) - 1U] = '\0';

        s_deviceList[s_deviceCount].rssi  = (int32_t)advertisedDevice.getRSSI();
        s_deviceList[s_deviceCount].valid = true;
        s_deviceCount++;

        BLECOMM_DEBUG_PRINTF("[BLEComm] Found: %s  RSSI: %d\n",
                             devName.c_str(), (int)advertisedDevice.getRSSI());
        s_bleStatusDirty = true;
    }
};

/* Static callback instances -- one of each, owned by this module */
static BLEComm_ServerCallbacks  s_serverCallbacks;
static BLEComm_IdentifyCallback s_identifyCallback;
static BLEComm_ClientCallbacks  s_clientCallbacks;
static BLEComm_ScanCallback     s_scanCallback;
static BLEComm_FanDutyCallback  s_fanDutyCallback;
static BLEComm_FanFreqCallback  s_fanFreqCallback;

/*==============================================================================
 *                      PUBLIC FUNCTION IMPLEMENTATIONS
 *============================================================================*/

/**
 * @brief Initialize BLE communication module
 */
void BLEComm__Init(void)
{
    if (s_serverStarted) { return; }

    BLECOMM_DEBUG_PRINTLN("\n[BLEComm] ========== INITIALIZING ==========");
    BLECOMM_DEBUG_PRINTF("[BLEComm] Device name : %s\n", BLECOMM_DEVICE_NAME);

    /*--------------------------------------------------------------------------
     * Load AES key and auth token from NVS.
     * Falls back to compiled defaults ONLY when NVS has no provisioned values.
     * WARNING: The compiled defaults are publicly visible in source -- provision
     * per-device credentials via BLECOMM_NVS_NAMESPACE before deployment.
     *------------------------------------------------------------------------*/
    {
        Preferences prefs;
        prefs.begin(BLECOMM_NVS_NAMESPACE, true);  /* read-only */
        size_t keyLen   = prefs.getBytes(BLECOMM_NVS_KEY_AES,   s_aesKey,   16U);
        size_t tokenLen = prefs.getBytes(BLECOMM_NVS_KEY_TOKEN, s_authToken, 16U);
        prefs.end();

        if (keyLen != 16U)
        {
            /* NVS not provisioned -- use compiled default (insecure, dev only) */
            memcpy(s_aesKey, BLECOMM_DEFAULT_AES_KEY, 16U);
            BLECOMM_DEBUG_PRINTLN("[BLEComm] WARN: AES key not in NVS -- using default");
        }
        if (tokenLen != 16U)
        {
            memcpy(s_authToken, BLECOMM_DEFAULT_AUTH_TOKEN, 16U);
            BLECOMM_DEBUG_PRINTLN("[BLEComm] WARN: Auth token not in NVS -- using default");
        }
    }

    /* Initialize BLE stack -- once per boot */
    BLEDevice::init(BLECOMM_DEVICE_NAME);

    /* Create server */
    s_pServer = BLEDevice::createServer();
    s_pServer->setCallbacks(&s_serverCallbacks);

    /* Create sensor data service */
    BLEService *pService = s_pServer->createService(BLECOMM_SERVICE_UUID);

    /* Sensor characteristic -- READ + NOTIFY */
    s_pSensorChar = pService->createCharacteristic(
        BLECOMM_SENSOR_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ  |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    s_pSensorChar->addDescriptor(new BLE2902());

    /* Identify characteristic -- WRITE (AES-128 ECB auth) */
    s_pIdentifyChar = pService->createCharacteristic(
        BLECOMM_IDENTIFY_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    s_pIdentifyChar->setCallbacks(&s_identifyCallback);

    /* Fan duty characteristic -- WRITE ("0".."100" or "AUTO") */
    s_pFanDutyChar = pService->createCharacteristic(
        BLECOMM_FAN_DUTY_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    s_pFanDutyChar->setCallbacks(&s_fanDutyCallback);

    /* Fan frequency characteristic -- WRITE (Hz as decimal string) */
    s_pFanFreqChar = pService->createCharacteristic(
        BLECOMM_FAN_FREQ_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    s_pFanFreqChar->setCallbacks(&s_fanFreqCallback);

    pService->start();

    /*--------------------------------------------------------------------------
     * Restore last fan settings from NVS.
     * Frequency is always restored. Manual duty restored only if manual was
     * active when last saved.
     *------------------------------------------------------------------------*/
    {
        Preferences prefs;
        prefs.begin("fanCfg", true);  /* read-only */
        bool     savedManual = prefs.getBool("manual",  false);
        uint8_t  savedDuty   = prefs.getUChar("duty",  0U);
        uint32_t savedFreq   = prefs.getUInt("freqHz", 0U);
        prefs.end();

        if (savedFreq > 0U)
        {
            Fan__SetFrequency(savedFreq);
            BLECOMM_DEBUG_PRINTF("[BLEComm] Restored fan frequency: %luHz\n",
                                  (unsigned long)savedFreq);
        }
        if (savedManual)
        {
            Fan__SetDutyPercent(savedDuty);
            App__SetFanManualFlag();
            BLECOMM_DEBUG_PRINTF("[BLEComm] Restored fan manual duty: %d%%\n",
                                  (int)savedDuty);
        }
    }

    /* Start advertising */
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLECOMM_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);   /* iOS connection interval hint */
    pAdvertising->setMaxPreferred(0x12);   /* was setMinPreferred(0x12) -- bug fix */
    BLEDevice::startAdvertising();

    s_serverStarted  = true;
    s_bleStatusDirty = false;

    BLECOMM_DEBUG_PRINTF("[BLEComm] Advertising as '%s' -- ready\n", BLECOMM_DEVICE_NAME);
    BLECOMM_DEBUG_PRINTLN("[BLEComm] ========== INIT COMPLETE ==========\n");
}

/**
 * @brief BLE communication periodic handler -- call every 100ms from scheduler
 */
void BLEComm__Handler(void)
{
    /*--------------------------------------------------------------------------
     * Consume deferred BLE connect/disconnect events.
     * LEDHMI functions MUST be called from scheduler task context, not from
     * BLE stack callbacks -- they write LED state shared with LEDHMI task.
     *------------------------------------------------------------------------*/
    if (s_bleConnectedEvent)
    {
        s_bleConnectedEvent = false;
        LEDHMI__BleConnected();
    }
    if (s_bleDisconnectedEvent)
    {
        s_bleDisconnectedEvent = false;
        LEDHMI__BleDisconnected();
    }

    /* Poll connect task completion */
    if (s_connectPending && (s_connectTaskHandle == NULL))
    {
        s_connectPending  = false;
        s_isConnecting    = false;
        s_connectingIndex = -1;
    }

    /*--------------------------------------------------------------------------
     * Deferred NVS save -- flush fan config to flash.
     * NVS erase/write stalls CPU bus 200-500ms; doing from BLE callback
     * (BLE stack task) was starving IDLE0 and triggering WDT.
     *------------------------------------------------------------------------*/
    if (s_nvsPendingSave)
    {
        s_nvsPendingSave = false;
        Preferences prefs;
        prefs.begin("fanCfg", false);
        prefs.putBool("manual", (bool)s_nvsPendingManual);
        if (s_nvsPendingManual)
        {
            prefs.putUChar("duty", s_nvsPendingDuty);
        }
        if (s_nvsPendingFreqHz > 0U)
        {
            prefs.putUInt("freqHz", s_nvsPendingFreqHz);
        }
        prefs.end();
        BLECOMM_DEBUG_PRINTF("[BLEComm] NVS saved -- manual=%d duty=%d freq=%lu\n",
                             (int)s_nvsPendingManual,
                             (int)s_nvsPendingDuty,
                             (unsigned long)s_nvsPendingFreqHz);
    }

    /* Advertising auto-restart removed -- App.cpp is single authority for
     * advertising start/stop. See BLEComm__SetAdvSuppressed() below. */
}

/*------------------------------------------------------------------------------
 *  Data Send API
 *----------------------------------------------------------------------------*/

void BLEComm__SendSensorData(const char *json)
{
    if (!s_serverStarted)      { return; }
    if (s_pSensorChar == NULL) { return; }
    if (!s_pyScriptConnected)  { return; }
    if (json == NULL)          { return; }

    /* Arduino BLE setValue() takes uint8_t* -- const_cast required by API.
     * The data is not modified; this is an Arduino library limitation. */
    s_pSensorChar->setValue(
        const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(json)),
        strlen(json));
    s_pSensorChar->notify();
}

void BLEComm__SendSensorDataToClient(const char *json)
{
    if (!s_pyClientReady)       { return; }
    if (s_pPyScriptChar == NULL){ return; }
    if (s_pBLEClient == NULL)   { return; }
    if (json == NULL)           { return; }

    if (!s_pBLEClient->isConnected())
    {
        s_isConnected   = false;
        s_pyClientReady = false;
        s_pPyScriptChar = NULL;
        return;
    }

    s_pPyScriptChar->writeValue(
        const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(json)),
        strlen(json), false);
}

/*------------------------------------------------------------------------------
 *  Scan & Connect API
 *----------------------------------------------------------------------------*/

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

void BLEComm__StopScan(void)
{
    if ((s_pBLEScan != NULL) && s_isScanning)
    {
        s_pBLEScan->stop();
        s_isScanning   = false;
        s_scanComplete = true;
    }
}

void BLEComm__ConnectToDevice(uint8_t index)
{
    if (index >= s_deviceCount)
    {
        BLECOMM_DEBUG_PRINTF("[BLEComm] ConnectToDevice: index %d out of range\n",
                             (int)index);
        return;
    }

    /* Toggle disconnect if already connected to this device */
    if (s_isConnected &&
        (strncmp(s_deviceList[index].address, s_connectedAddr, 17U) == 0))
    {
        BLECOMM_DEBUG_PRINTF("[BLEComm] Disconnecting from: %s\n",
                             s_deviceList[index].name);
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

    strncpy(s_connectedName, s_deviceList[index].name,
            sizeof(s_connectedName) - 1U);
    s_connectedName[sizeof(s_connectedName) - 1U] = '\0';

    strncpy(s_connectedAddr, s_deviceList[index].address,
            sizeof(s_connectedAddr) - 1U);
    s_connectedAddr[sizeof(s_connectedAddr) - 1U] = '\0';

    s_isConnecting    = true;
    s_connectingIndex = (int8_t)index;

    BLEComm__SpawnConnectTask();
}

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

/*------------------------------------------------------------------------------
 *  State Query API
 *----------------------------------------------------------------------------*/

BLEComm_ServerState BLEComm__GetServerState(void)
{
    if (s_pyScriptConnected) { return BLECOMM_SERVER_IDENTIFIED;   }
    if (s_clientConnected)   { return BLECOMM_SERVER_CONNECTED;    }
    return BLECOMM_SERVER_DISCONNECTED;
}

BLEComm_ClientState BLEComm__GetClientState(void)
{
    if (s_isConnected)  { return BLECOMM_CLIENT_CONNECTED;  }
    if (s_isConnecting) { return BLECOMM_CLIENT_CONNECTING; }
    return BLECOMM_CLIENT_DISCONNECTED;
}

bool BLEComm__IsConnected(void)             { return (bool)s_isConnected;     }
bool BLEComm__IsSendingData(void)           { return s_serverStarted && (bool)s_pyScriptConnected; }
bool BLEComm__IsClientSendingData(void)     { return (bool)s_pyClientReady && (bool)s_isConnected; }
bool BLEComm__IsScanning(void)              { return (bool)s_isScanning;      }
bool BLEComm__IsScanComplete(void)          { return (bool)s_scanComplete;    }
bool BLEComm__IsServerClientConnected(void) { return (bool)s_clientConnected; }
bool BLEComm__IsPyScriptConnected(void)     { return (bool)s_pyScriptConnected; }

uint8_t BLEComm__GetDeviceCount(void) { return s_deviceCount; }

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
    return ((bool)s_isConnected &&
            (strncmp(s_deviceList[index].address, s_connectedAddr, 17U) == 0));
}

bool BLEComm__IsDeviceConnecting(uint8_t index)
{
    return ((bool)s_isConnecting && (s_connectingIndex == (int8_t)index));
}

const char *BLEComm__GetServerClientName(void)  { return s_serverClientName; }
const char *BLEComm__GetConnectedDeviceName(void)
{
    return (bool)s_isConnected ? s_connectedName : NULL;
}

/*------------------------------------------------------------------------------
 *  Dirty Flag API
 *----------------------------------------------------------------------------*/

bool BLEComm__IsStatusDirty(void)    { return (bool)s_bleStatusDirty; }
void BLEComm__ClearStatusDirty(void) { s_bleStatusDirty = false;      }
void BLEComm__SetStatusDirty(void)   { s_bleStatusDirty = true;       }

/**
 * @brief Suppress or allow advertising auto-restart (no-op in v1.1.0).
 * @details Advertising auto-restart was removed from BLEComm__Handler() in
 *          v1.0.0; App.cpp state machine is now the single authority.
 *          This function is retained as a no-op to avoid breaking callers.
 */
void BLEComm__SetAdvSuppressed(bool suppress)
{
    (void)suppress;
    /* No-op: auto-restart removed from handler; App.cpp owns advertising */
}

/*==============================================================================
 *                      PRIVATE FUNCTION IMPLEMENTATIONS
 *============================================================================*/

/**
 * @brief BLE connect task -- runs in background, never blocks scheduler
 */
static void BLEComm__ConnectTask(void *pvParameters)
{
    (void)pvParameters;

    BLECOMM_DEBUG_PRINTF("[BLEComm] Connect task started for: %s\n", s_connectAddr);

    /* Always create a fresh BLEClient -- reusing a stale object after
     * a previous disconnect can leave the BLE stack in an undefined state. */
    if (s_pBLEClient != NULL)
    {
        if (s_pBLEClient->isConnected())
        {
            s_pBLEClient->disconnect();
        }
        /* Note: BLEDevice manages client lifetime; do not delete manually */
        s_pBLEClient = NULL;
    }

    s_pBLEClient = BLEDevice::createClient();
    s_pBLEClient->setClientCallbacks(&s_clientCallbacks);

    BLEAddress bleAddr(s_connectAddr);
    bool       connected = s_pBLEClient->connect(bleAddr);

    s_isConnecting    = false;
    s_connectingIndex = -1;
    s_connectPending  = false;

    if (connected)
    {
        s_isConnected   = true;
        s_pyClientReady = false;

        /* If connected to PyScript -- discover write characteristic */
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
                    BLECOMM_DEBUG_PRINTLN("[BLEComm] PyScript char found -- ready to send");
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

    s_bleStatusDirty    = true;
    s_connectTaskHandle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief Spawn the BLE connect background task
 */
static void BLEComm__SpawnConnectTask(void)
{
    if (s_connectPending)
    {
        BLECOMM_DEBUG_PRINTLN("[BLEComm] Connect already in progress -- ignoring");
        return;
    }

    s_connectPending = true;
    strncpy(s_connectAddr, s_connectedAddr, sizeof(s_connectAddr) - 1U);
    s_connectAddr[sizeof(s_connectAddr) - 1U] = '\0';

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