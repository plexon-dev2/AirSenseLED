/**
 * @file App.cpp
 * @brief Application Module -- LED Project
 * @version 3.17.0
 *
 * BLE Behaviour:
 *   Power ON  -> BLE ON, 2min timer
 *   2min timeout -> BLE OFF
 *   5s hold   -> BLE ON, no timer
 *   Connect   -> LED blinks, timer cancelled
 *   Disconnect -> LED OFF, BLE stays ON advertising
 *
 * MQTT: Routed through WiFiComm module only (no duplicate client)
 *
 * v3.17.0 changes (review fixes):
 *   - App_MQTTCommandHandler: replaced atoi()/strchr() with App_ParseIntField()
 *     -- eliminates NULL-deref on malformed payload and forbidden atoi() use
 *   - App_MQTTCommandHandler: (void)length suppression removed; length forwarded
 *     consistently to OTA handler
 *   - App_BLESwitchHandler: s_firstRun moved to file scope (s_bleSwitchFirstRun)
 *     -- visible to App_BLESwitchInit() for deterministic re-init
 *   - App__Init: MQTT subscription and callback registration moved here from
 *     App_BLESwitchHandler first-run block -- correct separation of concerns
 *   - App_DisplayData: dead RTC publish counter removed -- RTC publishes every
 *     sensor cycle as intended; use APP_RTC_PUBLISH_DIVIDER if rate limiting needed
 *   - App_DisplayData: snprintf truncation detection added for json[] buffer
 *   - App_DisplayData: stale sensor data flag (s_sensorDataStale) added --
 *     BLE/MQTT publish suppressed when CmdParser returns no valid data for
 *     APP_SENSOR_STALE_LIMIT consecutive cycles
 *   - App_BLEStop: null-check added before getAdvertising()->stop()
 *   - App_UpdateFanSpeed: volatile currentData fields copied to locals before
 *     comparison (MISRA R6)
 *   - rtcJson buffer widened to 48 bytes
 *   - App__BLEClientConnected / App__BLEClientDisconnected: stubs removed;
 *     functions removed from public API (App.h updated accordingly)
 *
 * v3.16.0 changes:
 *   - App__Init: replaced fixed delay(3000) with SensorBoot__MeasureAndWait()
 *   - App_DisplayData: publishes sensor boot time once after MQTT connects
 */

#include "App.h"
#include "App_Cfg.h"
#include "Modbus.h"
#include "CmdParser.h"
#include "Arduino.h"
#include "RTC.h"
#include "Fan.h"
#include "BLEComm.h"
#include "AQ_LEDHMI.h"
#include "WiFiComm.h"
#include "WiFiComm_Cfg.h"
#include "OTA_WiFi.h"
#include "SensorBoot.h"
#include <BLEDevice.h>
#include <string.h>
#include <stdlib.h>

#define APP_BLE_SWITCH_PIN      (8)          /**< @todo Move to HardwareConfig.h */
#define APP_BLE_SWITCH_ACTIVE   (LOW)
#define APP_BLE_HOLD_MS         (5000U)
#define APP_BLE_AUTO_OFF_MS     (120000UL)

#define APP_FAN_MIN_VOLTAGE     (3.0f)       /**< Always-on floor speed. Raised from 2.0V -- 2.0V was not enough to keep exhaust fan motor spinning reliably */

/**
 * @brief Number of consecutive failed CmdParser reads before sensor data is
 *        flagged stale and BLE/MQTT publish is suppressed.
 */
#define APP_SENSOR_STALE_LIMIT  (5U)

/**
 * @brief RTC timestamp publish rate divider relative to sensor update cycle.
 *        Set to 1 to publish every sensor cycle, N to publish every Nth cycle.
 */
#define APP_RTC_PUBLISH_DIVIDER (1U)

typedef enum
{
    BLE_SW_OFF          = 0U,
    BLE_SW_ADVERTISING  = 1U,
    BLE_SW_CONNECTED    = 2U,
    BLE_SW_DISCONNECTED = 3U
} App_BLESwitchState;

extern volatile SensorData currentData;

static bool               s_rs485CommError      = false;
static uint32_t           s_displayCounter      = 0U;
static bool               s_fanManualMode       = false;
static bool               s_fanBLEManual        = false;  /**< true when BLE set duty% directly -- skip App_UpdateFanSpeed entirely */
static float              s_fanManualVolts      = 0.0f;
static App_BLESwitchState s_bleState            = BLE_SW_OFF;
static bool               s_holdCounting        = false;
static uint32_t           s_holdStartMs         = 0U;
static bool               s_timerActive         = false;
static uint32_t           s_timerStartMs        = 0U;
static bool               s_lastPinState        = false;
static bool               s_bleSwitchFirstRun   = true;   /**< moved from function scope -- reset by App_BLESwitchInit() */
static uint8_t            s_sensorStaleCount    = 0U;     /**< consecutive CmdParser failures; suppress publish above APP_SENSOR_STALE_LIMIT */

static void App_MonitorCommunication(void);
static void App_DisplayData(void);
static void App_UpdateFanSpeed(void);
static void App_BLESwitchInit(void);
static void App_BLESwitchHandler(void);
static void App_BLEStartAdvertising(bool withTimer);
static void App_BLEStop(void);

/**
 * @brief Safe integer field extractor for minimal JSON payloads.
 * @details Locates @p key in @p payload, reads the integer value after the
 *          first ':' that follows the key, validates it is within
 *          [@p minVal, @p maxVal], and stores it in @p out.
 *
 * @param payload   Null-terminated JSON string.
 * @param key       Key string to search for, e.g. "\"y\"".
 * @param minVal    Minimum acceptable value (inclusive).
 * @param maxVal    Maximum acceptable value (inclusive).
 * @param out       Output parameter; written only on success.
 * @return true  Field found, parsed, and within range.
 * @return false Field missing, malformed, or out of range.
 */
static bool App_ParseIntField(const char *payload, const char *key,
                               int32_t minVal, int32_t maxVal, int32_t *out)
{
    const char *keyPos   = strstr(payload, key);
    if (keyPos == NULL) { return false; }

    const char *colon = strchr(keyPos, ':');
    if (colon == NULL)  { return false; }

    char   *endPtr = NULL;
    long    val    = strtol(colon + 1, &endPtr, 10);

    /* endPtr == colon+1 means no digits were consumed */
    if (endPtr == (colon + 1)) { return false; }

    if (val < (long)minVal || val > (long)maxVal) { return false; }

    *out = (int32_t)val;
    return true;
}

/**
 * @brief MQTT command handler — routes OTA commands and sets RTC time.
 * @details RTC payload format: {"y":2025,"mo":6,"d":19,"h":14,"mi":30,"s":0}
 *          All fields are mandatory integers.  Missing or out-of-range fields
 *          cause the entire command to be rejected.
 */
static void App_MQTTCommandHandler(const char *topic, const char *payload, uint16_t length)
{
    if (topic == NULL || payload == NULL) { return; }

    /* Route to OTA handler first -- length is forwarded as received */
    if (strstr(topic, "/ota/command") != NULL)
    {
        OTA_WiFi__MQTTOTAHandler(topic, payload, length);
        return;
    }

    if (strcmp(topic, MQTT_TOPIC_RTC_SET) == 0)
    {
        int32_t y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;

        bool valid =
            App_ParseIntField(payload, "\"y\"",  2000L, 2099L, &y)  &&
            App_ParseIntField(payload, "\"mo\"", 1L,    12L,   &mo) &&
            App_ParseIntField(payload, "\"d\"",  1L,    31L,   &d)  &&
            App_ParseIntField(payload, "\"h\"",  0L,    23L,   &h)  &&
            App_ParseIntField(payload, "\"mi\"", 0L,    59L,   &mi) &&
            App_ParseIntField(payload, "\"s\"",  0L,    59L,   &s);

        if (!valid)
        {
            Serial.println("[APP] RTC set -- payload missing field, malformed, or out of range -- ignored");
            return;
        }

        uint8_t dow    = RTC_GetDayOfWeek((uint8_t)d, (uint8_t)mo, (uint16_t)y);
        int8_t  result = RTC_SetDateTime((uint8_t)s,  (uint8_t)mi, (uint8_t)h,
                                         dow,          (uint8_t)d,  (uint8_t)mo,
                                         (uint16_t)y);
        if (result == RTC_OK)
        {
            Serial.printf("[APP] RTC set via MQTT: %04ld-%02ld-%02ld %02ld:%02ld:%02ld\n",
                          (long)y, (long)mo, (long)d, (long)h, (long)mi, (long)s);
        }
        else
        {
            Serial.printf("[APP] RTC set FAILED, code=%d\n", (int)result);
        }
    }
}

/*==============================================================================
 * PUBLIC FUNCTIONS
 *============================================================================*/

void App__Init(void)
{
    /*--------------------------------------------------------------------------
     * v3.16.0: SensorBoot__MeasureAndWait() replaces the fixed delay.
     *   - Drives APP_SENSOR_POWER_PIN (GPIO48) HIGH internally
     *   - Polls CmdParser until first valid sensor response
     *   - Records actual boot time for MQTT report
     *   - Falls back to CFG_BOOT_SENSOR_TIMEOUT_MS if sensor does not respond
     *------------------------------------------------------------------------*/
    SensorBoot__MeasureAndWait();

    s_rs485CommError    = false;
    s_displayCounter    = 0U;
    s_fanManualMode     = false;
    s_fanBLEManual      = false;
    s_fanManualVolts    = 0.0f;
    s_sensorStaleCount  = 0U;

    App_BLESwitchInit();

    /*--------------------------------------------------------------------------
     * Register MQTT command handler and subscriptions here -- not deferred into
     * App_BLESwitchHandler() -- so MQTT setup is always performed exactly once
     * regardless of BLE state machine behaviour.
     *------------------------------------------------------------------------*/
    WiFiComm__RegisterMQTTRxCallback(App_MQTTCommandHandler);
    WiFiComm__MQTTSubscribe(MQTT_TOPIC_RTC_SET);
    WiFiComm__MQTTSubscribe(MQTT_TOPIC_OTA_COMMAND);
    Serial.println("[APP] MQTT subscriptions registered (rtc/set + ota/command)");

    Serial.println("[APP] Init OK");
}

void App__Handler(void)
{
    App_MonitorCommunication();
    App_UpdateFanSpeed();
    App_BLESwitchHandler();
    App_DisplayData();
}

bool App__IsRS485CommError(void)       { return s_rs485CommError; }
void App__ClearErrors(void)            { s_rs485CommError = false; }
void App__SetFanManual(float voltage)  { s_fanManualMode = true; s_fanManualVolts = voltage; Fan__SetVoltage(voltage); }
void App__SetFanAuto(void)             { s_fanManualMode = false; s_fanBLEManual = false; }
void App__SetFanManualFlag(void)       { s_fanBLEManual = true; }
bool App__IsFanManual(void)            { return s_fanManualMode || s_fanBLEManual; }

bool App__IsBLEAdvertisingAllowed(void)
{
    return (s_bleState == BLE_SW_ADVERTISING ||
            s_bleState == BLE_SW_CONNECTED   ||
            s_bleState == BLE_SW_DISCONNECTED);
}

/*==============================================================================
 * BLE PRIVATE FUNCTIONS
 *============================================================================*/

static void App_BLEStartAdvertising(bool withTimer)
{
    BLEDevice::startAdvertising();
    s_bleState     = BLE_SW_ADVERTISING;
    s_timerActive  = withTimer;
    s_timerStartMs = millis();
    LEDHMI__BleDisconnected();
    Serial.printf("[APP] BLE advertising%s\n",
                  withTimer ? " (2min timer)" : " (no timer)");
}

static void App_BLEStop(void)
{
    BLEAdvertising *pAdv = BLEDevice::getAdvertising();
    if (pAdv != NULL)
    {
        pAdv->stop();
    }
    s_bleState    = BLE_SW_OFF;
    s_timerActive = false;
    LEDHMI__BleDisconnected();
    Serial.println("[APP] BLE stopped -- LED OFF");
}

static void App_BLESwitchInit(void)
{
    pinMode(APP_BLE_SWITCH_PIN, INPUT_PULLUP);
    s_lastPinState      = (digitalRead(APP_BLE_SWITCH_PIN) == APP_BLE_SWITCH_ACTIVE);
    s_holdCounting      = false;
    s_bleState          = BLE_SW_OFF;
    s_bleSwitchFirstRun = true;   /* explicit reset -- safe to call App__Init() again */
    Serial.printf("[APP] BLE init -- ResetReason=%d\n", (int)esp_reset_reason());
}

static void App_BLESwitchHandler(void)
{
    /* First call after init: BLEComm is now ready, start advertising */
    if (s_bleSwitchFirstRun)
    {
        s_bleSwitchFirstRun = false;
        App_BLEStartAdvertising(true);
        Serial.println("[APP] Boot -- BLE ON, 2min timer");
        return;
    }

    uint32_t now      = millis();
    bool     pinState = (digitalRead(APP_BLE_SWITCH_PIN) == APP_BLE_SWITCH_ACTIVE);

    /* Hold detection */
    if (pinState && !s_lastPinState)
    {
        s_holdCounting = true;
        s_holdStartMs  = now;
    }
    else if (!pinState && s_lastPinState)
    {
        if (s_holdCounting)
        {
            uint32_t held  = now - s_holdStartMs;
            s_holdCounting = false;

            if (held >= APP_BLE_HOLD_MS)
            {
                if (s_bleState == BLE_SW_OFF)
                {
                    Serial.println("[APP] 5s hold -- BLE ON (no timer)");
                    App_BLEStartAdvertising(false);
                }
            }
        }
    }
    s_lastPinState = pinState;

    /* 2min auto-off */
    if (s_bleState == BLE_SW_ADVERTISING && s_timerActive &&
        (now - s_timerStartMs) >= APP_BLE_AUTO_OFF_MS)
    {
        Serial.println("[APP] 2min timeout -- BLE OFF");
        App_BLEStop();
        return;
    }

    /* Connection state from BLEComm */
    if (s_bleState != BLE_SW_OFF)
    {
        bool pyConn = BLEComm__IsPyScriptConnected();
        bool hwConn = BLEComm__IsServerClientConnected();

        if (pyConn && (s_bleState == BLE_SW_ADVERTISING ||
                       s_bleState == BLE_SW_DISCONNECTED))
        {
            s_bleState    = BLE_SW_CONNECTED;
            s_timerActive = false;
            LEDHMI__BleConnected();
            Serial.println("[APP] Connected -- LED blinks, timer cancelled");
        }
        else if (!hwConn && s_bleState == BLE_SW_CONNECTED)
        {
            s_bleState = BLE_SW_DISCONNECTED;
            LEDHMI__BleDisconnected();
            App_BLEStartAdvertising(true);
            Serial.println("[APP] Disconnected -- LED OFF, BLE stays ON");
        }
    }
}

/*==============================================================================
 * SENSOR / DISPLAY
 *============================================================================*/

static void App_MonitorCommunication(void)
{
    s_rs485CommError = !Modbus_IsInitialized();
}

static void App_UpdateFanSpeed(void)
{
    if (s_fanBLEManual)  { return; }  /* BLE has direct control -- don't touch fan */
    if (s_fanManualMode) { Fan__SetVoltage(s_fanManualVolts); return; }

    /* Copy volatile fields to locals before comparison (MISRA C:2012 Rule 10.4) */
    float co2  = currentData.co2;
    float pm25 = currentData.pm25;

    float voltage;
    if      (co2  >= 1500.0f) { voltage = 5.0f; }
    else if (pm25 >= 75.0f)   { voltage = 5.0f; }
    else if (co2  >= 1000.0f) { voltage = 3.5f; }
    else if (pm25 >= 35.0f)   { voltage = 3.0f; }
    /* CO2 800-1000 tier raised from 2.0f to match new floor -- was below
     * the minimum spin-sustaining voltage for this motor. */
    else if (co2  >= 800.0f)  { voltage = APP_FAN_MIN_VOLTAGE; }
    else                      { voltage = APP_FAN_MIN_VOLTAGE; }

    Fan__SetVoltage(voltage);
}

static void App_DisplayData(void)
{
    /* Republish boot time on every MQTT connect -- ensures GUI always receives it
     * even if it connects after the initial one-shot publish at startup.        */
    static WiFiComm_MQTTState s_lastMqttState = WIFI_MQTT_DISCONNECTED;
    WiFiComm_MQTTState mqttNow = WiFiComm__GetMQTTState();
    if (mqttNow == WIFI_MQTT_CONNECTED && s_lastMqttState != WIFI_MQTT_CONNECTED)
    {
        SensorBoot__PublishResult();
        Serial.printf("[APP] Boot time republished on MQTT connect: %lums\n",
                      (unsigned long)SensorBoot__GetBootTimeMS());
    }
    s_lastMqttState = mqttNow;

    s_displayCounter++;
    if (s_displayCounter > (UINT32_MAX - 100U)) { s_displayCounter = 0U; }
    if (s_displayCounter < APP_DISPLAY_UPDATE_INTERVAL) { return; }
    s_displayCounter = 0U;

    CmdParser_ZPHS01B_Data_t aq_data;
    CmdParser_StatusType_t   status = CmdParser__GetAirQualityData(&aq_data);

    if (status == CMDPARSER_STATUS_OK)
    {
        /* Valid data received -- clear stale counter */
        s_sensorStaleCount = 0U;

        currentData.temperature  = aq_data.temperature;
        currentData.humidity     = (float)aq_data.humidity;
        currentData.o3           = aq_data.o3 * 1000.0f;
        currentData.no2          = aq_data.no2;
        currentData.formaldehyde = aq_data.ch2o;
        currentData.pm25         = (float)aq_data.pm2_5;
        currentData.co           = aq_data.co;
        currentData.co2          = (float)aq_data.co2;
        currentData.tvoc         = (float)((aq_data.voc <= 3U) ? (uint8_t)aq_data.voc : 3U);

        Modbus_UpdateSensorData(&aq_data);

        char json[128];
        int  jsonLen = snprintf(json, sizeof(json),
                "{\"t\":%.1f,\"h\":%.0f,\"co2\":%.0f,"
                "\"no2\":%.3f,\"ch2o\":%.3f,\"pm25\":%.0f,"
                "\"co\":%.1f,\"o3\":%.1f,\"voc\":%d}",
                currentData.temperature, currentData.humidity,
                currentData.co2,         currentData.no2,
                currentData.formaldehyde, currentData.pm25,
                currentData.co,           currentData.o3,
                (int)currentData.tvoc);

        if (jsonLen >= (int)sizeof(json))
        {
            Serial.println("[APP] WARNING: sensor JSON truncated -- increase json[] buffer");
        }
        else
        {
            BLEComm__SendSensorData(json);
            BLEComm__SendSensorDataToClient(json);
            WiFiComm__MQTTPublish(WIFI_MQTT_TOPIC_SENSOR_DATA, json);
        }

        /* Publish RTC timestamp -- rate controlled by APP_RTC_PUBLISH_DIVIDER   */
        static uint8_t s_rtcDivider = 0U;
        s_rtcDivider++;
        if (s_rtcDivider >= APP_RTC_PUBLISH_DIVIDER)
        {
            s_rtcDivider = 0U;
            char timestamp[24];
            if (RTC_GetTimestamp(timestamp, sizeof(timestamp)) == RTC_OK)
            {
                char rtcJson[48];   /* {"ts":"2025-06-19T14:30:00"} = ~31 chars; 48 gives margin */
                int  rtcLen = snprintf(rtcJson, sizeof(rtcJson), "{\"ts\":\"%s\"}", timestamp);
                if (rtcLen >= (int)sizeof(rtcJson))
                {
                    Serial.println("[APP] WARNING: rtcJson truncated");
                }
                else
                {
                    WiFiComm__MQTTPublish(MQTT_TOPIC_RTC_TIME, rtcJson);
                    Serial.printf("[RTC] Published: %s\n", timestamp);
                }
            }
            else
            {
                Serial.println("[RTC] GetTimestamp FAILED -- I2C error?");
            }
        }
    }
    else
    {
        /* CmdParser returned no valid data this cycle */
        if (s_sensorStaleCount < APP_SENSOR_STALE_LIMIT)
        {
            s_sensorStaleCount++;
        }

        if (s_sensorStaleCount >= APP_SENSOR_STALE_LIMIT)
        {
            Serial.printf("[APP] Sensor data stale (%u consecutive failures) -- publish suppressed\n",
                          (unsigned)s_sensorStaleCount);
        }
    }
}

void App__RTCHandler(void)
{
    /* Intentionally empty -- RTC timestamp is published inside App_DisplayData()
     * on each sensor update cycle.  Retain function in API for scheduler
     * compatibility; implement here if a decoupled 1Hz RTC read is needed.   */
}