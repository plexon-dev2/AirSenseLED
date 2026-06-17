/**
 * @file App.cpp
 * @brief Application Module -- LED Project
 * @version 3.15.0
 *
 * BLE Behaviour:
 *   Power ON  -> BLE ON, 2min timer
 *   2min timeout -> BLE OFF
 *   5s hold   -> BLE ON, no timer
 *   Connect   -> LED blinks, timer cancelled
 *   Disconnect -> LED OFF, BLE stays ON advertising
 *
 * MQTT: Routed through WiFiComm module only (no duplicate client)
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
#include <BLEDevice.h>

#define APP_BLE_SWITCH_PIN      (8)
#define APP_BLE_SWITCH_ACTIVE   (LOW)
#define APP_BLE_HOLD_MS         (5000U)
#define APP_BLE_AUTO_OFF_MS     (120000UL)

typedef enum
{
    BLE_SW_OFF          = 0U,
    BLE_SW_ADVERTISING  = 1U,
    BLE_SW_CONNECTED    = 2U,
    BLE_SW_DISCONNECTED = 3U
} App_BLESwitchState;

extern volatile SensorData currentData;

static bool               s_rs485CommError  = false;
static uint32_t           s_displayCounter  = 0U;
static bool               s_fanManualMode   = false;
static float              s_fanManualVolts  = 0.0f;
static App_BLESwitchState s_bleState        = BLE_SW_OFF;
static bool               s_holdCounting    = false;
static uint32_t           s_holdStartMs     = 0U;
static bool               s_timerActive     = false;
static uint32_t           s_timerStartMs    = 0U;
static bool               s_lastPinState    = false;

static void App_MonitorCommunication(void);
static void App_DisplayData(void);
static void App_UpdateFanSpeed(void);
static void App_BLESwitchInit(void);
static void App_BLESwitchHandler(void);
static void App_BLEStartAdvertising(bool withTimer);
static void App_BLEStop(void);

/*==============================================================================
 * PUBLIC FUNCTIONS
 *============================================================================*/

void App__Init(void)
{
    s_rs485CommError = false;
    s_displayCounter = 0U;
    s_fanManualMode  = false;
    s_fanManualVolts = 0.0f;
    App_BLESwitchInit();
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
void App__SetFanAuto(void)             { s_fanManualMode = false; }
bool App__IsFanManual(void)            { return s_fanManualMode; }
void App__BLEClientConnected(void)     { /* handled by BLEComm watchdog */ }
void App__BLEClientDisconnected(void)  { /* handled by BLEComm watchdog */ }

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
    BLEDevice::getAdvertising()->stop();
    s_bleState    = BLE_SW_OFF;
    s_timerActive = false;
    LEDHMI__BleDisconnected();
    Serial.println("[APP] BLE stopped -- LED OFF");
}

static void App_BLESwitchInit(void)
{
    pinMode(APP_BLE_SWITCH_PIN, INPUT_PULLUP);
    s_lastPinState = (digitalRead(APP_BLE_SWITCH_PIN) == APP_BLE_SWITCH_ACTIVE);
    s_holdCounting = false;
    s_bleState     = BLE_SW_OFF;
    Serial.printf("[APP] BLE init -- ResetReason=%d\n", (int)esp_reset_reason());
}

static void App_BLESwitchHandler(void)
{
    /* First call: BLEComm is now initialized, safe to start advertising */
    static bool s_firstRun = true;
    if (s_firstRun)
    {
        s_firstRun = false;
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
    if (s_fanManualMode) { Fan__SetVoltage(s_fanManualVolts); return; }
    float voltage = 0.0f;
    if      (currentData.co2  >= 1500.0f) { voltage = 5.0f; }
    else if (currentData.pm25 >= 75.0f)   { voltage = 5.0f; }
    else if (currentData.co2  >= 1000.0f) { voltage = 3.5f; }
    else if (currentData.pm25 >= 35.0f)   { voltage = 3.0f; }
    else if (currentData.co2  >= 800.0f)  { voltage = 2.0f; }
    else                                  { voltage = 0.0f; }
    Fan__SetVoltage(voltage);
}

static void App_DisplayData(void)
{
    s_displayCounter++;
    if (s_displayCounter > (UINT32_MAX - 100U)) { s_displayCounter = 0U; }
    if (s_displayCounter < APP_DISPLAY_UPDATE_INTERVAL) return;
    s_displayCounter = 0U;

    CmdParser_ZPHS01B_Data_t aq_data;
    CmdParser_StatusType_t   status = CmdParser__GetAirQualityData(&aq_data);

    if (status == CMDPARSER_STATUS_OK)
    {
        currentData.temperature  = aq_data.temperature;
        currentData.humidity     = (float)aq_data.humidity;
        currentData.o3           = aq_data.o3 * 1000.0f;
        currentData.no2          = aq_data.no2;
        currentData.formaldehyde = aq_data.ch2o;
        currentData.pm25         = (float)aq_data.pm2_5;
        currentData.co           = aq_data.co;
        currentData.co2          = (float)aq_data.co2;
        currentData.tvoc         = (float)((aq_data.voc <= 3U) ? aq_data.voc : 3U);

        Modbus_UpdateSensorData(&aq_data);

        char json[128];
        snprintf(json, sizeof(json),
                "{\"t\":%.1f,\"h\":%.0f,\"co2\":%.0f,"
                "\"no2\":%.3f,\"ch2o\":%.3f,\"pm25\":%.0f,"
                "\"co\":%.1f,\"o3\":%.1f,\"voc\":%d}",
                currentData.temperature, currentData.humidity,
                currentData.co2,         currentData.no2,
                currentData.formaldehyde, currentData.pm25,
                currentData.co,           currentData.o3,
                (int)currentData.tvoc);

        BLEComm__SendSensorData(json);
        BLEComm__SendSensorDataToClient(json);
        WiFiComm__MQTTPublish(WIFI_MQTT_TOPIC_SENSOR_DATA, json);
    }
}

void App__RTCHandler(void)
{
    /* Intentionally empty */
}