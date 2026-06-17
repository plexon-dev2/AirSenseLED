/**
 * @file AQ_LEDHMI.cpp
 * @brief Air Quality LED HMI Implementation
 * @details LED management system with state machine control
 *          for the Air Quality Monitor project (ESP32-S3).
 *
 * LED BEHAVIOUR SUMMARY (per product specification):
 * ==================================================
 *   POWER  LED (IO47, Green) : Solid ON — always ON while powered
 *   CPU    LED (IO40, Green) : 1Hz blink — 500ms ON / 500ms OFF, continuous
 *   ERROR  LED (IO42, Red)   : 1Hz blink — 500ms ON / 500ms OFF while error active
 *                              Triggers: Fan failed | Sensor stopped | No Modbus master response
 *   ALARM  LED (IO46, Red)   : 1Hz blink — 500ms ON / 500ms OFF while threshold exceeded
 *   MODBUS LED (IO45, Yellow): 1Hz blink — 500ms ON / 500ms OFF while master polling;
 *                              stops automatically after 2000ms of master silence
 *   BLE    LED (IO41, Blue)  : 1Hz blink — 500ms ON / 500ms OFF while advertising/pairing;
 *                              Solid ON once a central device connects
 *
 * DESIGN NOTES:
 * =============
 * - CPU, ERROR, ALARM, and MODBUS LEDs all use dedicated state machines in
 *   LEDHMI__Handler() so they run continuously without a finite blink count.
 * - MODBUS LED uses a "last-poll timestamp" keep-alive: it blinks while polls
 *   arrive within MODBUS_LED_TIMEOUT_MS; stops after the master goes silent.
 * - BLE LED has two modes: PAIRING (1Hz blink via state machine) and CONNECTED
 *   (solid ON via AQLED__On). LEDHMI__BleConnected/Disconnected switch modes.
 * - The low-level AQLED__* primitives and AQLED__Update() are unchanged so
 *   all existing callers (Modbus.cpp etc.) remain compatible.
 * - Per-sensor alarm edge detection: each sensor beeps once independently
 *   on its own 0→1 threshold crossing.
 *
 * @author
 * @date 2025-12-19
 * @version 1.2.0
 */

/*==============================================================================
 *                              INCLUDES
 *============================================================================*/
#include "Arduino.h"
#include "AQ_LEDHMI.h"
#include "AQ_LEDHMI_Cfg.h"
#include "App.h"
#include "CmdParser.h"
#include "Fan.h"
#include "Modbus.h"
#include "ThresholdSettings.h"
#include "AppMutex.h"
#include "Buzzer.h"

/*==============================================================================
 *                              PRIVATE TYPES
 *============================================================================*/

/**
 * @brief Generic ON/OFF blink state used by CPU, ERROR, ALARM, MODBUS, BLE
 *        state machines inside LEDHMI__Handler()
 */
typedef enum
{
    LED_SM_ON  = 0, /**< Currently in ON  phase */
    LED_SM_OFF = 1  /**< Currently in OFF phase */
} AQ_LedSm_State;

/**
 * @brief Per-sensor alarm previous state flags for edge detection
 * @details One flag per sensor — each beeps independently on 0→1 transition.
 */
typedef struct
{
    uint8_t temperature;
    uint8_t humidity;
    uint8_t co2;
    uint8_t co;
    uint8_t o3;
    uint8_t no2;
    uint8_t formaldehyde;
    uint8_t pm25;
    uint8_t tvoc;
} AQ_AlarmPrevFlags_t;

/*==============================================================================
 *                          PRIVATE CONSTANTS
 *============================================================================*/

/**
 * @brief Modbus LED keep-alive timeout (ms)
 * @details MODBUS LED stops blinking this many ms after the last received poll.
 *          Set to 2× the expected poll interval to allow one missed frame.
 */
#define MODBUS_LED_TIMEOUT_MS   (2000U)

/*==============================================================================
 *                          PRIVATE VARIABLES
 *============================================================================*/

/* LED runtime instances */
static AQ_LED_Instance AQ_LED_Power;
static AQ_LED_Instance AQ_LED_Cpu;
static AQ_LED_Instance AQ_LED_Error;
static AQ_LED_Instance AQ_LED_Alarm;
static AQ_LED_Instance AQ_LED_Modbus;
static AQ_LED_Instance AQ_LED_Ble;

/*------------------------------------------------------------------------------
 * CPU LED — 1Hz continuous blink state machine (500ms ON / 500ms OFF)
 *----------------------------------------------------------------------------*/
static AQ_LedSm_State AQ_CpuLedState         = LED_SM_ON;
static uint32_t       AQ_CpuLedStateStartTime = 0U;

/*------------------------------------------------------------------------------
 * ERROR LED — 1Hz blink state machine, only runs when error is flagged
 *----------------------------------------------------------------------------*/
static AQ_LedSm_State   AQ_ErrorLedState         = LED_SM_ON;
static uint32_t         AQ_ErrorLedStateStartTime = 0U;
static volatile uint8_t AQ_ErrorLedActive         = 0U;

/*------------------------------------------------------------------------------
 * ALARM LED — 1Hz blink state machine, only runs when alarm is flagged
 *----------------------------------------------------------------------------*/
static AQ_LedSm_State   AQ_AlarmLedState         = LED_SM_ON;
static uint32_t         AQ_AlarmLedStateStartTime = 0U;
static volatile uint8_t AQ_AlarmLedActive         = 0U;

/**
 * @brief Per-sensor previous alarm state — edge detection for buzzer
 * @details Initialised to all zeros in LEDHMI__Init().
 *          Each field tracks one sensor's previous active/inactive state.
 *          Buzzer fires once per sensor on 0→1 transition only.
 */
static AQ_AlarmPrevFlags_t AQ_AlarmPrev;

/*------------------------------------------------------------------------------
 * MODBUS LED — 1Hz blink state machine, runs while master is actively polling
 *----------------------------------------------------------------------------*/
static AQ_LedSm_State AQ_ModbusLedState         = LED_SM_ON;
static uint32_t       AQ_ModbusLedStateStartTime = 0U;
static uint8_t        AQ_ModbusLedActive         = 0U;
static uint32_t       AQ_ModbusLastPollTime       = 0U;

/*------------------------------------------------------------------------------
 * BLE LED — dual-mode: PAIRING (1Hz blink SM) or CONNECTED (solid ON)
 *----------------------------------------------------------------------------*/
static AQ_LedSm_State   AQ_BleLedState         = LED_SM_ON;
static uint32_t         AQ_BleLedStateStartTime = 0U;
static volatile uint8_t AQ_BleConnected         = 0U;

/*==============================================================================
 *                          PRIVATE FUNCTION PROTOTYPES
 *============================================================================*/
static void AQ_LED__SetPin(uint8_t pin, uint8_t state);

/*==============================================================================
 *                          PUBLIC FUNCTION IMPLEMENTATIONS
 *============================================================================*/

AQ_LED_Status AQLED__Init(AQ_LED_Instance *led, const AQ_LED_Config *config)
{
    if ((led == NULL) || (config == NULL))
    {
        return AQ_LED_STATUS_ERROR;
    }

    led->config            = *config;
    led->state             = AQ_LED_OFF;
    led->last_toggle_time  = millis();
    led->timed_on_start    = 0U;
    led->timed_on_duration = 0U;
    led->blink_count       = 0U;
    led->is_on             = 0U;

    pinMode(config->pin, OUTPUT);
    AQLED__Off(led);

    return AQ_LED_STATUS_OK;
}

void AQLED__On(AQ_LED_Instance *led)
{
    if (led == NULL) { return; }

    led->state             = AQ_LED_ON;
    led->blink_count       = 0U;
    led->timed_on_duration = 0U;

    if (led->config.active_low == 1U) { led->is_on = 0U; }
    else                              { led->is_on = 1U; }

    AQ_LED__SetPin(led->config.pin, led->is_on);
}

void AQLED__Off(AQ_LED_Instance *led)
{
    if (led == NULL) { return; }

    led->state             = AQ_LED_OFF;
    led->blink_count       = 0U;
    led->timed_on_duration = 0U;

    if (led->config.active_low == 1U) { led->is_on = 1U; }
    else                              { led->is_on = 0U; }

    AQ_LED__SetPin(led->config.pin, led->is_on);
}

void AQLED__Blink(AQ_LED_Instance *led, uint8_t count)
{
    if ((led == NULL) || (count == 0U)) { return; }

    if ((led->state == AQ_LED_BLINK) && (led->blink_count > 0U)) { return; }

    led->state            = AQ_LED_BLINK;
    led->blink_count      = (uint8_t)(count * 2U);
    led->last_toggle_time = millis();

    if (led->config.active_low == 1U) { led->is_on = 0U; }
    else                              { led->is_on = 1U; }

    AQ_LED__SetPin(led->config.pin, led->is_on);

    SERIAL_PRINTF("[AQ_LED] Blink started: pin=%d count=%d\n",
                  (int)led->config.pin, (int)count);
}

void AQLED__TimedOn(AQ_LED_Instance *led, uint16_t durationMs)
{
    if ((led == NULL) || (durationMs == 0U)) { return; }

    led->state             = AQ_LED_TIMED_ON;
    led->timed_on_start    = millis();
    led->timed_on_duration = durationMs;
    led->blink_count       = 0U;

    if (led->config.active_low == 1U) { led->is_on = 0U; }
    else                              { led->is_on = 1U; }

    AQ_LED__SetPin(led->config.pin, led->is_on);

    Serial.printf("[AQ_LED] TimedOn: pin=%d duration=%dms\n",
                  (int)led->config.pin, (int)durationMs);
}

void AQLED__Update(AQ_LED_Instance *led, uint32_t currentTimeMs)
{
    uint32_t elapsed        = 0U;
    uint16_t toggleInterval = 0U;

    if (led == NULL) { return; }

    if ((led->state == AQ_LED_OFF) || (led->state == AQ_LED_ON)) { return; }

    if (led->state == AQ_LED_TIMED_ON)
    {
        elapsed = currentTimeMs - led->timed_on_start;
        if (elapsed >= (uint32_t)led->timed_on_duration)
        {
            AQLED__Off(led);
            Serial.printf("[AQ_LED] TimedOn expired: pin=%d\n", (int)led->config.pin);
        }
        return;
    }

    if ((led->state == AQ_LED_BLINK) && (led->blink_count > 0U))
    {
        elapsed = currentTimeMs - led->last_toggle_time;

        if (led->is_on == 1U) { toggleInterval = led->config.blink_on_ms;  }
        else                  { toggleInterval = led->config.blink_off_ms; }

        if (elapsed >= (uint32_t)toggleInterval)
        {
            led->last_toggle_time = currentTimeMs;
            led->blink_count--;
            led->is_on = (led->is_on == 1U) ? 0U : 1U;
            AQ_LED__SetPin(led->config.pin, led->is_on);

            if (led->blink_count == 0U)
            {
                AQLED__Off(led);
                Serial.printf("[AQ_LED] Blink complete: pin=%d\n", (int)led->config.pin);
            }
        }
    }
}

/*==============================================================================
 *                          MODULE-LEVEL FUNCTIONS
 *============================================================================*/

void LEDHMI__Init(void)
{
    uint32_t now = millis();

    SERIAL_PRINTF("\n[AQ_LEDHMI] ========== INITIALIZING ==========");

    if (AQLED__Init(&AQ_LED_Power,  &AQ_LED_Power_Config)  == AQ_LED_STATUS_OK)
    {
        Serial.printf("[AQ_LEDHMI] POWER  LED - Pin %d (Solid ON)\n",          (int)AQ_LED_POWER_PIN);
    }
    if (AQLED__Init(&AQ_LED_Cpu,    &AQ_LED_Cpu_Config)    == AQ_LED_STATUS_OK)
    {
        Serial.printf("[AQ_LEDHMI] CPU    LED - Pin %d (1Hz continuous)\n",     (int)AQ_LED_CPU_PIN);
    }
    if (AQLED__Init(&AQ_LED_Error,  &AQ_LED_Error_Config)  == AQ_LED_STATUS_OK)
    {
        Serial.printf("[AQ_LEDHMI] ERROR  LED - Pin %d (1Hz on error)\n",       (int)AQ_LED_ERROR_PIN);
    }
    if (AQLED__Init(&AQ_LED_Alarm,  &AQ_LED_Alarm_Config)  == AQ_LED_STATUS_OK)
    {
        Serial.printf("[AQ_LEDHMI] ALARM  LED - Pin %d (1Hz on alarm)\n",       (int)AQ_LED_ALARM_PIN);
    }
    if (AQLED__Init(&AQ_LED_Modbus, &AQ_LED_Modbus_Config) == AQ_LED_STATUS_OK)
    {
        Serial.printf("[AQ_LEDHMI] MODBUS LED - Pin %d (1Hz while polling)\n",  (int)AQ_LED_MODBUS_PIN);
    }
    if (AQLED__Init(&AQ_LED_Ble,    &AQ_LED_Ble_Config)    == AQ_LED_STATUS_OK)
    {
        Serial.printf("[AQ_LEDHMI] BLE    LED - Pin %d (blink/solid)\n",        (int)AQ_LED_BLE_PIN);
    }

    /* POWER LED — solid ON immediately */
    AQLED__On(&AQ_LED_Power);

    /* CPU LED — start 1Hz heartbeat */
    AQ_CpuLedState        = LED_SM_ON;
    AQ_CpuLedStateStartTime = now;
    AQ_LED__SetPin(AQ_LED_CPU_PIN, 1U);

    /* ERROR LED — not active at boot */
    AQ_ErrorLedActive = 0U;

    /* ALARM LED — not active at boot, clear all per-sensor flags */
    AQ_AlarmLedActive = 0U;
    memset(&AQ_AlarmPrev, 0, sizeof(AQ_AlarmPrev));

    /* MODBUS LED — not active at boot */
    AQ_ModbusLedActive   = 0U;
    AQ_ModbusLastPollTime = 0U;

    /* BLE LED — start OFF (advertising, no connection yet) */
    AQ_BleConnected        = 0U;
    AQ_BleLedState         = LED_SM_ON;
    AQ_BleLedStateStartTime = now;
    AQ_LED__SetPin(AQ_LED_BLE_PIN, 0U);  /* LED OFF at boot */

    SERIAL_PRINTF("[AQ_LEDHMI] ========== INIT COMPLETE ==========\n");
}

/**
 * @brief LED HMI periodic handler — must be called every 50ms
 */
void LEDHMI__Handler(void)
{
    /*--------------------------------------------------------------------------
     * ALARM LED + per-sensor buzzer edge detection
     * Each sensor beeps once independently on its own 0→1 threshold crossing.
     * SENSOR_EDGE_BEEP macro: reads sensor, detects rising edge, fires beep,
     * updates previous state flag.
     *------------------------------------------------------------------------*/
    {
        extern volatile SensorData currentData;
        extern ThresholdData thresholds;

        /**
         * @brief Per-sensor edge detection macro
         * @param field          SensorData / AQ_AlarmPrev field name
         * @param threshold_field ThresholdData max field name
         */
        #define SENSOR_EDGE_BEEP(field, threshold_field)                             \
        do {                                                                          \
            uint8_t _active = (currentData.field > thresholds.threshold_field)       \
                              ? 1U : 0U;                                              \
            if ((_active == 1U) && (AQ_AlarmPrev.field == 0U))                       \
            {                                                                         \
                Buzzer__Beep();                                                       \
                SERIAL_PRINTF("[ALARM] %s exceeded threshold\n", #field);             \
            }                                                                         \
            AQ_AlarmPrev.field = _active;                                             \
        } while (0)

        SENSOR_EDGE_BEEP(temperature,  temperature_max);
        SENSOR_EDGE_BEEP(humidity,     humidity_max);
        SENSOR_EDGE_BEEP(co2,          co2_max);
        SENSOR_EDGE_BEEP(co,           co_max);
        SENSOR_EDGE_BEEP(o3,           o3_max);
        SENSOR_EDGE_BEEP(no2,          no2_max);
        SENSOR_EDGE_BEEP(formaldehyde, formaldehyde_max);
        SENSOR_EDGE_BEEP(pm25,         pm25_max);
        SENSOR_EDGE_BEEP(tvoc,         tvoc_max);

        #undef SENSOR_EDGE_BEEP

        /* Overall alarm state for LED — derived from per-sensor flags */
        bool anyAlarm =
            (AQ_AlarmPrev.temperature  == 1U) ||
            (AQ_AlarmPrev.humidity     == 1U) ||
            (AQ_AlarmPrev.co2          == 1U) ||
            (AQ_AlarmPrev.co           == 1U) ||
            (AQ_AlarmPrev.o3           == 1U) ||
            (AQ_AlarmPrev.no2          == 1U) ||
            (AQ_AlarmPrev.formaldehyde == 1U) ||
            (AQ_AlarmPrev.pm25         == 1U) ||
            (AQ_AlarmPrev.tvoc         == 1U);

        if (anyAlarm) { LEDHMI__AlarmActive();  }
        else          { LEDHMI__AlarmCleared(); }
    }

    /*--------------------------------------------------------------------------
     * ERROR LED — sensor + fan + Modbus silence
     *------------------------------------------------------------------------*/
    {
        bool sensorError = CmdParser__IsSensorDetected() && !CmdParser__IsDataFresh();
        bool fanError    = Fan__IsFailed();

        static uint32_t s_lastModbusFrameCount = 0U;
        static uint32_t s_modbusLastSeenTime   = 0U;
        static bool     s_modbusMasterEverSeen = false;
        #define MODBUS_ERROR_TIMEOUT_MS (5000U)

        Modbus_Statistics_t mbStats;
        Modbus_GetStatistics(&mbStats);
        if (mbStats.frames_received > s_lastModbusFrameCount)
        {
            s_lastModbusFrameCount = mbStats.frames_received;
            s_modbusLastSeenTime   = millis();
            s_modbusMasterEverSeen = true;
        }
        bool modbusError = s_modbusMasterEverSeen &&
                           ((millis() - s_modbusLastSeenTime) >= MODBUS_ERROR_TIMEOUT_MS);

        static bool s_prevErrorState = false;
        if (sensorError || fanError || modbusError)
        {
            LEDHMI__ErrorActive();
            if (!s_prevErrorState)
            {
                SERIAL_PRINTF("[ERROR] Active — sensor:%d fan:%d modbus:%d\n",
                              (int)sensorError, (int)fanError, (int)modbusError);
                s_prevErrorState = true;
            }
        }
        else
        {
            LEDHMI__ErrorCleared();
            s_prevErrorState = false;
        }
    }

    static uint32_t AQ_LastDebugTime = 0U;
    uint32_t        currentTime      = 0U;
    uint32_t        elapsed          = 0U;

    currentTime = millis();

    /* Debug log every 3 seconds */
    if ((currentTime - AQ_LastDebugTime) > 3000U)
    {
        SERIAL_PRINTF("[AQ_LEDHMI] CPU=%d ERR=%d ALARM=%d MODBUS=%d BLE=%s\n",
                      (int)AQ_CpuLedState,
                      (int)AQ_ErrorLedActive,
                      (int)AQ_AlarmLedActive,
                      (int)AQ_ModbusLedActive,
                      AQ_BleConnected ? "CONNECTED" : "PAIRING");
        AQ_LastDebugTime = currentTime;
    }

    /*--------------------------------------------------------------------------
     * CPU LED — 1Hz continuous blink (500ms ON / 500ms OFF)
     *------------------------------------------------------------------------*/
    elapsed = currentTime - AQ_CpuLedStateStartTime;
    switch (AQ_CpuLedState)
    {
        case LED_SM_ON:
            if (elapsed >= AQ_LED_CPU_ON_MS)
            {
                AQ_CpuLedState        = LED_SM_OFF;
                AQ_CpuLedStateStartTime = currentTime;
                AQ_LED__SetPin(AQ_LED_CPU_PIN, 0U);
            }
            break;
        case LED_SM_OFF:
            if (elapsed >= AQ_LED_CPU_OFF_MS)
            {
                AQ_CpuLedState        = LED_SM_ON;
                AQ_CpuLedStateStartTime = currentTime;
                AQ_LED__SetPin(AQ_LED_CPU_PIN, 1U);
            }
            break;
        default:
            AQ_CpuLedState        = LED_SM_ON;
            AQ_CpuLedStateStartTime = currentTime;
            AQ_LED__SetPin(AQ_LED_CPU_PIN, 1U);
            break;
    }

    /*--------------------------------------------------------------------------
     * ERROR LED — 1Hz blink, only while error active
     *------------------------------------------------------------------------*/
    if (AQ_ErrorLedActive == 1U)
    {
        elapsed = currentTime - AQ_ErrorLedStateStartTime;
        switch (AQ_ErrorLedState)
        {
            case LED_SM_ON:
                if (elapsed >= AQ_LED_ERROR_BLINK_ON_MS)
                {
                    AQ_ErrorLedState        = LED_SM_OFF;
                    AQ_ErrorLedStateStartTime = currentTime;
                    AQ_LED__SetPin(AQ_LED_ERROR_PIN, 0U);
                }
                break;
            case LED_SM_OFF:
                if (elapsed >= AQ_LED_ERROR_BLINK_OFF_MS)
                {
                    AQ_ErrorLedState        = LED_SM_ON;
                    AQ_ErrorLedStateStartTime = currentTime;
                    AQ_LED__SetPin(AQ_LED_ERROR_PIN, 1U);
                }
                break;
            default:
                AQ_ErrorLedState        = LED_SM_ON;
                AQ_ErrorLedStateStartTime = currentTime;
                AQ_LED__SetPin(AQ_LED_ERROR_PIN, 1U);
                break;
        }
    }

    /*--------------------------------------------------------------------------
     * ALARM LED — 1Hz blink, only while alarm active
     *------------------------------------------------------------------------*/
    if (AQ_AlarmLedActive == 1U)
    {
        elapsed = currentTime - AQ_AlarmLedStateStartTime;
        switch (AQ_AlarmLedState)
        {
            case LED_SM_ON:
                if (elapsed >= AQ_LED_ALARM_BLINK_ON_MS)
                {
                    AQ_AlarmLedState        = LED_SM_OFF;
                    AQ_AlarmLedStateStartTime = currentTime;
                    AQ_LED__SetPin(AQ_LED_ALARM_PIN, 0U);
                }
                break;
            case LED_SM_OFF:
                if (elapsed >= AQ_LED_ALARM_BLINK_OFF_MS)
                {
                    AQ_AlarmLedState        = LED_SM_ON;
                    AQ_AlarmLedStateStartTime = currentTime;
                    AQ_LED__SetPin(AQ_LED_ALARM_PIN, 1U);
                }
                break;
            default:
                AQ_AlarmLedState        = LED_SM_ON;
                AQ_AlarmLedStateStartTime = currentTime;
                AQ_LED__SetPin(AQ_LED_ALARM_PIN, 1U);
                break;
        }
    }

    /*--------------------------------------------------------------------------
     * MODBUS LED — 1Hz blink while master actively polling
     *------------------------------------------------------------------------*/
    if ((AQ_ModbusLedActive == 1U) &&
        (AQ_ModbusLastPollTime != 0U) &&
        ((currentTime - AQ_ModbusLastPollTime) >= MODBUS_LED_TIMEOUT_MS))
    {
        AQ_ModbusLedActive = 0U;
        AQ_LED__SetPin(AQ_LED_MODBUS_PIN, 0U);
        SERIAL_PRINTF("[AQ_LEDHMI] MODBUS LED: Master silent — LED OFF");
    }

    if (AQ_ModbusLedActive == 1U)
    {
        elapsed = currentTime - AQ_ModbusLedStateStartTime;
        switch (AQ_ModbusLedState)
        {
            case LED_SM_ON:
                if (elapsed >= AQ_LED_MODBUS_BLINK_ON_MS)
                {
                    AQ_ModbusLedState        = LED_SM_OFF;
                    AQ_ModbusLedStateStartTime = currentTime;
                    AQ_LED__SetPin(AQ_LED_MODBUS_PIN, 0U);
                }
                break;
            case LED_SM_OFF:
                if (elapsed >= AQ_LED_MODBUS_BLINK_OFF_MS)
                {
                    AQ_ModbusLedState        = LED_SM_ON;
                    AQ_ModbusLedStateStartTime = currentTime;
                    AQ_LED__SetPin(AQ_LED_MODBUS_PIN, 1U);
                }
                break;
            default:
                AQ_ModbusLedState        = LED_SM_ON;
                AQ_ModbusLedStateStartTime = currentTime;
                AQ_LED__SetPin(AQ_LED_MODBUS_PIN, 1U);
                break;
        }
    }

    /*--------------------------------------------------------------------------
     * BLE LED — CONNECTED: 1Hz blink / DISCONNECTED or ADVERTISING: OFF
     * Blink SM runs only when connected (AQ_BleConnected == 1).
     * When not connected the LED is driven OFF by BleDisconnected() — nothing
     * to do here.
     *------------------------------------------------------------------------*/
    if (AQ_BleConnected == 1U)
    {
        elapsed = currentTime - AQ_BleLedStateStartTime;
        switch (AQ_BleLedState)
        {
            case LED_SM_ON:
                if (elapsed >= AQ_LED_BLE_BLINK_ON_MS)
                {
                    AQ_BleLedState          = LED_SM_OFF;
                    AQ_BleLedStateStartTime = currentTime;
                    AQ_LED__SetPin(AQ_LED_BLE_PIN, 0U);
                }
                break;
            case LED_SM_OFF:
                if (elapsed >= AQ_LED_BLE_BLINK_OFF_MS)
                {
                    AQ_BleLedState          = LED_SM_ON;
                    AQ_BleLedStateStartTime = currentTime;
                    AQ_LED__SetPin(AQ_LED_BLE_PIN, 1U);
                }
                break;
            default:
                AQ_BleLedState          = LED_SM_ON;
                AQ_BleLedStateStartTime = currentTime;
                AQ_LED__SetPin(AQ_LED_BLE_PIN, 1U);
                break;
        }
    }
    /* else: AQ_BleConnected == 0 → LED OFF, driven by BleDisconnected() */
}

/*==============================================================================
 *                          APPLICATION EVENT FUNCTIONS
 *============================================================================*/

void LEDHMI__AlarmActive(void)
{
    if (AQ_AlarmLedActive == 0U)
    {
        AQ_AlarmLedActive        = 1U;
        AQ_AlarmLedState         = LED_SM_ON;
        AQ_AlarmLedStateStartTime = millis();
        AQ_LED__SetPin(AQ_LED_ALARM_PIN, 1U);
        SERIAL_PRINTF("[AQ_LEDHMI] ALARM LED: Active - 1Hz blink started");
    }
}

void LEDHMI__AlarmCleared(void)
{
    if (AQ_AlarmLedActive == 0U) { return; }
    AQ_AlarmLedActive = 0U;
    AQ_LED__SetPin(AQ_LED_ALARM_PIN, 0U);
    SERIAL_PRINTF("[AQ_LEDHMI] ALARM LED: Cleared - OFF");
}

void LEDHMI__ErrorActive(void)
{
    if (AQ_ErrorLedActive == 0U)
    {
        AQ_ErrorLedActive        = 1U;
        AQ_ErrorLedState         = LED_SM_ON;
        AQ_ErrorLedStateStartTime = millis();
        AQ_LED__SetPin(AQ_LED_ERROR_PIN, 1U);
        SERIAL_PRINTF("[AQ_LEDHMI] ERROR LED: Active - 1Hz blink started");
    }
}

void LEDHMI__ErrorCleared(void)
{
    if (AQ_ErrorLedActive == 0U) { return; }
    AQ_ErrorLedActive = 0U;
    AQ_LED__SetPin(AQ_LED_ERROR_PIN, 0U);
    SERIAL_PRINTF("[AQ_LEDHMI] ERROR LED: Cleared - OFF");
}

void LEDHMI__ModbusPoll(void)
{
    uint32_t now = millis();
    AQ_ModbusLastPollTime = now;

    if (AQ_ModbusLedActive == 0U)
    {
        AQ_ModbusLedActive        = 1U;
        AQ_ModbusLedState         = LED_SM_ON;
        AQ_ModbusLedStateStartTime = now;
        AQ_LED__SetPin(AQ_LED_MODBUS_PIN, 1U);
        SERIAL_PRINTF("[AQ_LEDHMI] MODBUS LED: Master detected - 1Hz blink started");
    }
}

void LEDHMI__ModbusDataReceived(void)
{
    AQ_ModbusLastPollTime = millis();
}

void LEDHMI__BleConnected(void)
{
    uint32_t now = millis();
    AQ_BleConnected        = 1U;
    AQ_BleLedState         = LED_SM_ON;
    AQ_BleLedStateStartTime = now;
    AQ_LED__SetPin(AQ_LED_BLE_PIN, 1U);  /* Start blink ON phase immediately */
    SERIAL_PRINTF("[AQ_LEDHMI] BLE LED: Connected - 1Hz blink started");
}

void LEDHMI__BleDisconnected(void)
{
    AQ_BleConnected = 0U;
    AQ_LED__SetPin(AQ_LED_BLE_PIN, 0U);  /* LED OFF — advertising silently */
    SERIAL_PRINTF("[AQ_LEDHMI] BLE LED: Disconnected - LED OFF");
}

/*==============================================================================
 *                          PRIVATE FUNCTION IMPLEMENTATIONS
 *============================================================================*/

static void AQ_LED__SetPin(uint8_t pin, uint8_t state)
{
    digitalWrite(pin, (uint8_t)state);
}