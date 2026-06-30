/**
 * @file AQ_LEDHMI.cpp
 * @brief Air Quality LED HMI Implementation
 * @details LED management system with state machine control
 *          for the Air Quality Monitor project (ESP32-S3).
 *
 * LED BEHAVIOUR SUMMARY (per product specification):
 * ==================================================
 *   POWER  LED (Green) : Solid ON -- always ON while powered
 *   CPU    LED (Green) : 1Hz blink -- 500ms ON / 500ms OFF, continuous
 *   ERROR  LED (Red)   : 1Hz blink -- 500ms ON / 500ms OFF while error active
 *                        Triggers: Fan failed | Sensor stopped | No Modbus master >5s
 *   ALARM  LED (Red)   : 1Hz blink -- 500ms ON / 500ms OFF while threshold exceeded
 *   MODBUS LED (Yellow): 1Hz blink -- 500ms ON / 500ms OFF while master polling;
 *                        stops automatically after MODBUS_LED_TIMEOUT_MS of silence
 *   BLE    LED (Blue)  : 1Hz blink -- 500ms ON / 500ms OFF while advertising or connected;
 *                        OFF when BLE switch is OFF (LEDHMI__BleOff called)
 *
 * DESIGN NOTES:
 * =============
 * - CPU, ERROR, ALARM, MODBUS, BLE LEDs all use dedicated state machines in
 *   LEDHMI__Handler() so they run continuously without a finite blink count.
 * - MODBUS LED uses a last-poll timestamp keep-alive: blinks while polls arrive
 *   within MODBUS_LED_TIMEOUT_MS; stops after master goes silent.
 * - Per-sensor alarm edge detection: each sensor triggers buzzer once independently
 *   on its own 0->1 threshold crossing via AQ_AlarmCheck() inline helper.
 * - AQ_LED_Config structs defined here (not in _Cfg.h) to avoid ODR violations
 *   when multiple TUs include the config header.
 *
 * @date 2025-12-19
 * @version 1.3.0
 *
 * v1.3.0 changes (review fixes):
 *   - CRITICAL: Removed duplicate temperature alarm block (was firing Buzzer__Beep()
 *     twice and overwriting AQ_AlarmPrev.temperature after macro already set it)
 *   - CRITICAL: LEDHMI__BleOff() implemented (was declared in .h but missing --
 *     would cause linker error)
 *   - extern declarations for currentData and thresholds moved from inside
 *     LEDHMI__Handler() body to file scope (MISRA Rule 8.5)
 *   - SENSOR_EDGE_BEEP macro replaced with AQ_AlarmCheck() static inline function
 *     (MISRA Rules 20.5, 8.5 -- macro defined/undef inside function body)
 *   - MODBUS_ERROR_TIMEOUT_MS #define moved from inside function body to
 *     file-scope constants section
 *   - s_lastModbusFrameCount, s_modbusLastSeenTime, s_modbusMasterEverSeen moved
 *     from static locals inside LEDHMI__Handler() to file scope -- reset by Init()
 *   - All raw Serial.printf() calls replaced with SERIAL_PRINTF (mutex-safe)
 *     except LEDHMI__Init() boot log which uses Serial.printf before scheduler
 *   - AQLED__Blink(): count clamped to 127 max to prevent uint8_t overflow on *2
 *   - AQ_LED_Config structs defined here; extern declarations in _Cfg.h
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
#include <string.h>

/*==============================================================================
 *                              PRIVATE TYPES
 *============================================================================*/

/**
 * @brief Generic ON/OFF blink state used by CPU, ERROR, ALARM, MODBUS, BLE SMs
 */
typedef enum
{
    LED_SM_ON  = 0U, /**< Currently in ON  phase */
    LED_SM_OFF = 1U  /**< Currently in OFF phase */
} AQ_LedSm_State;

/**
 * @brief Per-sensor alarm previous state flags for edge detection
 * @details One flag per sensor -- each triggers buzzer on 0->1 transition only.
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
 */
#define MODBUS_LED_TIMEOUT_MS       (2000U)

/**
 * @brief Modbus error timeout (ms)
 * @details ERROR LED activates if no Modbus frame received for this duration
 *          after the master was first detected.
 */
#define MODBUS_ERROR_TIMEOUT_MS     (5000U)

/*==============================================================================
 *                          LED CONFIGURATION DEFINITIONS
 *  Definitions live here -- AQ_LEDHMI_Cfg.h provides extern declarations only.
 *  This prevents each including TU from getting its own copy (ODR / MISRA 2.4).
 *============================================================================*/

const AQ_LED_Config AQ_LED_Power_Config =
{
    .pin          = AQ_LED_POWER_PIN,
    .blink_on_ms  = 0U,
    .blink_off_ms = 0U,
    .active_low   = AQ_LED_ACTIVE_LOW
};

const AQ_LED_Config AQ_LED_Cpu_Config =
{
    .pin          = AQ_LED_CPU_PIN,
    .blink_on_ms  = AQ_LED_CPU_ON_MS,
    .blink_off_ms = AQ_LED_CPU_OFF_MS,
    .active_low   = AQ_LED_ACTIVE_LOW
};

const AQ_LED_Config AQ_LED_Error_Config =
{
    .pin          = AQ_LED_ERROR_PIN,
    .blink_on_ms  = AQ_LED_ERROR_BLINK_ON_MS,
    .blink_off_ms = AQ_LED_ERROR_BLINK_OFF_MS,
    .active_low   = AQ_LED_ACTIVE_LOW
};

const AQ_LED_Config AQ_LED_Alarm_Config =
{
    .pin          = AQ_LED_ALARM_PIN,
    .blink_on_ms  = AQ_LED_ALARM_BLINK_ON_MS,
    .blink_off_ms = AQ_LED_ALARM_BLINK_OFF_MS,
    .active_low   = AQ_LED_ACTIVE_LOW
};

const AQ_LED_Config AQ_LED_Modbus_Config =
{
    .pin          = AQ_LED_MODBUS_PIN,
    .blink_on_ms  = AQ_LED_MODBUS_BLINK_ON_MS,
    .blink_off_ms = AQ_LED_MODBUS_BLINK_OFF_MS,
    .active_low   = AQ_LED_ACTIVE_LOW
};

const AQ_LED_Config AQ_LED_Ble_Config =
{
    .pin          = AQ_LED_BLE_PIN,
    .blink_on_ms  = AQ_LED_BLE_BLINK_ON_MS,
    .blink_off_ms = AQ_LED_BLE_BLINK_OFF_MS,
    .active_low   = AQ_LED_ACTIVE_LOW
};

/*==============================================================================
 *                          EXTERN DECLARATIONS  (file scope -- MISRA Rule 8.5)
 *============================================================================*/

/**
 * @brief Live sensor readings produced by App_DisplayData() on Core 0.
 * @note  Read here on Core 1 in LEDHMI__Handler() -- access is inherently
 *        racy until a SensorData mutex API is introduced (see App.cpp review).
 *        Reads are tolerant of one-cycle stale values for LED/alarm purposes.
 */
extern volatile SensorData currentData;

/**
 * @brief Air quality alarm thresholds configured by ThresholdSettings module.
 */
extern ThresholdData thresholds;

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
 * CPU LED -- 1Hz continuous blink SM
 *----------------------------------------------------------------------------*/
static AQ_LedSm_State AQ_CpuLedState          = LED_SM_ON;
static uint32_t       AQ_CpuLedStateStartTime  = 0U;

/*------------------------------------------------------------------------------
 * ERROR LED -- 1Hz blink SM, only while error flagged
 *----------------------------------------------------------------------------*/
static AQ_LedSm_State   AQ_ErrorLedState          = LED_SM_ON;
static uint32_t         AQ_ErrorLedStateStartTime  = 0U;
static volatile uint8_t AQ_ErrorLedActive          = 0U;

/*------------------------------------------------------------------------------
 * ALARM LED -- 1Hz blink SM, only while alarm flagged
 *----------------------------------------------------------------------------*/
static AQ_LedSm_State   AQ_AlarmLedState          = LED_SM_ON;
static uint32_t         AQ_AlarmLedStateStartTime  = 0U;
static volatile uint8_t AQ_AlarmLedActive          = 0U;

/**
 * @brief Per-sensor previous alarm state -- edge detection for buzzer.
 *        Initialised to all zeros in LEDHMI__Init().
 */
static AQ_AlarmPrevFlags_t AQ_AlarmPrev;

/*------------------------------------------------------------------------------
 * MODBUS LED -- 1Hz blink SM while master actively polling
 *----------------------------------------------------------------------------*/
static AQ_LedSm_State AQ_ModbusLedState          = LED_SM_ON;
static uint32_t       AQ_ModbusLedStateStartTime  = 0U;
static uint8_t        AQ_ModbusLedActive          = 0U;
static uint32_t       AQ_ModbusLastPollTime        = 0U;

/*------------------------------------------------------------------------------
 * MODBUS error tracking -- moved from static locals to file scope so
 * LEDHMI__Init() can reset them on re-initialisation.
 *----------------------------------------------------------------------------*/
static uint32_t AQ_LastModbusFrameCount  = 0U;
static uint32_t AQ_ModbusLastSeenTime    = 0U;
static bool     AQ_ModbusMasterEverSeen  = false;
static bool     AQ_PrevErrorState        = false;

/*------------------------------------------------------------------------------
 * BLE LED -- 1Hz blink SM (advertising and connected both blink)
 *----------------------------------------------------------------------------*/
static AQ_LedSm_State   AQ_BleLedState          = LED_SM_ON;
static uint32_t         AQ_BleLedStateStartTime  = 0U;
static volatile uint8_t AQ_BleConnected          = 0U;

/*==============================================================================
 *                          PRIVATE FUNCTION PROTOTYPES
 *============================================================================*/
static void AQ_LED__SetPin(uint8_t pin, uint8_t state);

/**
 * @brief Per-sensor alarm edge detector -- replaces SENSOR_EDGE_BEEP macro.
 * @details Compares current sensor value against its threshold.
 *          On a 0->1 rising edge fires one Buzzer__Beep() and updates prevFlag.
 * @param[in]  currentVal  Current sensor reading (float).
 * @param[in]  threshold   Maximum allowed value (float).
 * @param[in]  sensorName  Human-readable name for debug log.
 * @param[out] prevFlag    Pointer to the sensor's previous-active flag byte.
 * @return 1U if sensor is currently above threshold, 0U otherwise.
 */
static uint8_t AQ_AlarmCheck(float currentVal, float threshold,
                              const char *sensorName, uint8_t *prevFlag);

/*==============================================================================
 *                          PRIVATE FUNCTION IMPLEMENTATIONS
 *============================================================================*/

static void AQ_LED__SetPin(uint8_t pin, uint8_t state)
{
    digitalWrite(pin, (uint8_t)state);
}

static uint8_t AQ_AlarmCheck(float currentVal, float threshold,
                              const char *sensorName, uint8_t *prevFlag)
{
    uint8_t active = (currentVal > threshold) ? 1U : 0U;

    if ((active == 1U) && (*prevFlag == 0U))
    {
        Buzzer__Beep();
        SERIAL_PRINTF("[ALARM] %s exceeded threshold\n", sensorName);
    }

    *prevFlag = active;
    return active;
}

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

    if ((led->state == AQ_LED_BLINK) && (led->blink_count > 0U))
    {
        SERIAL_PRINTF("[AQ_LED] Blink ignored -- already in progress: pin=%d\n",
                      (int)led->config.pin);
        return;
    }

    /* Clamp count to prevent uint8_t overflow on count*2 (MISRA Rule 10.3) */
    if (count > 127U) { count = 127U; }

    led->state            = AQ_LED_BLINK;
    led->blink_count      = (uint8_t)(count * 2U);
    led->last_toggle_time = millis();

    if (led->config.active_low == 1U) { led->is_on = 0U; }
    else                              { led->is_on = 1U; }

    AQ_LED__SetPin(led->config.pin, led->is_on);

    SERIAL_PRINTF("[AQ_LED] Blink started: pin=%d count=%u\n",
                  (int)led->config.pin, (unsigned)count);
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

    SERIAL_PRINTF("[AQ_LED] TimedOn: pin=%d duration=%ums\n",
                  (int)led->config.pin, (unsigned)durationMs);
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
            SERIAL_PRINTF("[AQ_LED] TimedOn expired: pin=%d\n", (int)led->config.pin);
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
                SERIAL_PRINTF("[AQ_LED] Blink complete: pin=%d\n", (int)led->config.pin);
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

    /* Boot log -- called before scheduler, Serial.printf safe here */
    Serial.println("\n[AQ_LEDHMI] ========== INITIALIZING ==========");

    if (AQLED__Init(&AQ_LED_Power,  &AQ_LED_Power_Config)  == AQ_LED_STATUS_OK)
    {
        Serial.printf("[AQ_LEDHMI] POWER  LED - Pin %d (Solid ON)\n",         (int)AQ_LED_POWER_PIN);
    }
    if (AQLED__Init(&AQ_LED_Cpu,    &AQ_LED_Cpu_Config)    == AQ_LED_STATUS_OK)
    {
        Serial.printf("[AQ_LEDHMI] CPU    LED - Pin %d (1Hz continuous)\n",    (int)AQ_LED_CPU_PIN);
    }
    if (AQLED__Init(&AQ_LED_Error,  &AQ_LED_Error_Config)  == AQ_LED_STATUS_OK)
    {
        Serial.printf("[AQ_LEDHMI] ERROR  LED - Pin %d (1Hz on error)\n",      (int)AQ_LED_ERROR_PIN);
    }
    if (AQLED__Init(&AQ_LED_Alarm,  &AQ_LED_Alarm_Config)  == AQ_LED_STATUS_OK)
    {
        Serial.printf("[AQ_LEDHMI] ALARM  LED - Pin %d (1Hz on alarm)\n",      (int)AQ_LED_ALARM_PIN);
    }
    if (AQLED__Init(&AQ_LED_Modbus, &AQ_LED_Modbus_Config) == AQ_LED_STATUS_OK)
    {
        Serial.printf("[AQ_LEDHMI] MODBUS LED - Pin %d (1Hz while polling)\n", (int)AQ_LED_MODBUS_PIN);
    }
    if (AQLED__Init(&AQ_LED_Ble,    &AQ_LED_Ble_Config)    == AQ_LED_STATUS_OK)
    {
        Serial.printf("[AQ_LEDHMI] BLE    LED - Pin %d (blink/solid)\n",       (int)AQ_LED_BLE_PIN);
    }

    /* POWER LED -- solid ON immediately */
    AQLED__On(&AQ_LED_Power);

    /* CPU LED -- start 1Hz heartbeat */
    AQ_CpuLedState         = LED_SM_ON;
    AQ_CpuLedStateStartTime = now;
    AQ_LED__SetPin(AQ_LED_CPU_PIN, 1U);

    /* ERROR LED -- not active at boot */
    AQ_ErrorLedActive  = 0U;
    AQ_PrevErrorState  = false;

    /* ALARM LED -- not active at boot, clear all per-sensor flags */
    AQ_AlarmLedActive = 0U;
    memset(&AQ_AlarmPrev, 0, sizeof(AQ_AlarmPrev));

    /* MODBUS LED -- not active at boot; reset error tracking state */
    AQ_ModbusLedActive       = 0U;
    AQ_ModbusLastPollTime     = 0U;
    AQ_LastModbusFrameCount   = 0U;
    AQ_ModbusLastSeenTime     = 0U;
    AQ_ModbusMasterEverSeen   = false;

    /* BLE LED -- start OFF (no connection at boot) */
    AQ_BleConnected         = 0U;
    AQ_BleLedState          = LED_SM_ON;
    AQ_BleLedStateStartTime = now;
    AQ_LED__SetPin(AQ_LED_BLE_PIN, 0U);

    Serial.println("[AQ_LEDHMI] ========== INIT COMPLETE ==========");
}

/*==============================================================================
 *                          PERIODIC HANDLER
 *============================================================================*/

/**
 * @brief LED HMI periodic handler -- must be called every 50ms
 */
void LEDHMI__Handler(void)
{
    uint32_t currentTime = millis();
    uint32_t elapsed     = 0U;

    /*--------------------------------------------------------------------------
     * ALARM LED + per-sensor buzzer edge detection
     * AQ_AlarmCheck() fires Buzzer__Beep() once per sensor on 0->1 transition.
     * Returns 1U if sensor is currently above threshold, 0U otherwise.
     * The return values are OR'd to derive the overall alarm state for the LED.
     *------------------------------------------------------------------------*/
    {
        /* Copy volatile sensor fields to locals before comparison (MISRA Rule 10.4) */
        float sv_temperature  = currentData.temperature;
        float sv_humidity     = currentData.humidity;
        float sv_co2          = currentData.co2;
        float sv_co           = currentData.co;
        float sv_o3           = currentData.o3;
        float sv_no2          = currentData.no2;
        float sv_formaldehyde = currentData.formaldehyde;
        float sv_pm25         = currentData.pm25;
        float sv_tvoc         = currentData.tvoc;

        uint8_t anyAlarm = 0U;
        anyAlarm |= AQ_AlarmCheck(sv_temperature,  thresholds.temperature_max,  "temperature",  &AQ_AlarmPrev.temperature);
        anyAlarm |= AQ_AlarmCheck(sv_humidity,      thresholds.humidity_max,     "humidity",     &AQ_AlarmPrev.humidity);
        anyAlarm |= AQ_AlarmCheck(sv_co2,           thresholds.co2_max,          "co2",          &AQ_AlarmPrev.co2);
        anyAlarm |= AQ_AlarmCheck(sv_co,            thresholds.co_max,           "co",           &AQ_AlarmPrev.co);
        anyAlarm |= AQ_AlarmCheck(sv_o3,            thresholds.o3_max,           "o3",           &AQ_AlarmPrev.o3);
        anyAlarm |= AQ_AlarmCheck(sv_no2,           thresholds.no2_max,          "no2",          &AQ_AlarmPrev.no2);
        anyAlarm |= AQ_AlarmCheck(sv_formaldehyde,  thresholds.formaldehyde_max, "formaldehyde", &AQ_AlarmPrev.formaldehyde);
        anyAlarm |= AQ_AlarmCheck(sv_pm25,          thresholds.pm25_max,         "pm25",         &AQ_AlarmPrev.pm25);
        anyAlarm |= AQ_AlarmCheck(sv_tvoc,          thresholds.tvoc_max,         "tvoc",         &AQ_AlarmPrev.tvoc);

        if (anyAlarm != 0U) { LEDHMI__AlarmActive();  }
        else                { LEDHMI__AlarmCleared(); }
    }

    /*--------------------------------------------------------------------------
     * ERROR LED -- sensor + fan + Modbus silence
     *------------------------------------------------------------------------*/
    {
        bool sensorError = CmdParser__IsSensorDetected() && !CmdParser__IsDataFresh();
        bool fanError    = Fan__IsFailed();

        Modbus_Statistics_t mbStats;
        Modbus_GetStatistics(&mbStats);
        if (mbStats.frames_received > AQ_LastModbusFrameCount)
        {
            AQ_LastModbusFrameCount = mbStats.frames_received;
            AQ_ModbusLastSeenTime   = currentTime;
            AQ_ModbusMasterEverSeen = true;
        }
        bool modbusError = AQ_ModbusMasterEverSeen &&
                           ((currentTime - AQ_ModbusLastSeenTime) >= MODBUS_ERROR_TIMEOUT_MS);

        if (sensorError || fanError || modbusError)
        {
            LEDHMI__ErrorActive();
            if (!AQ_PrevErrorState)
            {
                SERIAL_PRINTF("[ERROR] Active -- sensor:%d fan:%d modbus:%d\n",
                              (int)sensorError, (int)fanError, (int)modbusError);
                AQ_PrevErrorState = true;
            }
        }
        else
        {
            LEDHMI__ErrorCleared();
            AQ_PrevErrorState = false;
        }
    }

    /* Debug log every 3 seconds */
    {
        static uint32_t s_lastDebugTime = 0U;
        if ((currentTime - s_lastDebugTime) > 3000U)
        {
            SERIAL_PRINTF("[AQ_LEDHMI] CPU=%d ERR=%d ALARM=%d MODBUS=%d BLE=%s\n",
                          (int)AQ_CpuLedState,
                          (int)AQ_ErrorLedActive,
                          (int)AQ_AlarmLedActive,
                          (int)AQ_ModbusLedActive,
                          (AQ_BleConnected != 0U) ? "CONNECTED" : "ADVERTISING");
            s_lastDebugTime = currentTime;
        }
    }

    /*--------------------------------------------------------------------------
     * CPU LED -- 1Hz continuous blink (500ms ON / 500ms OFF)
     *------------------------------------------------------------------------*/
    elapsed = currentTime - AQ_CpuLedStateStartTime;
    switch (AQ_CpuLedState)
    {
        case LED_SM_ON:
            if (elapsed >= AQ_LED_CPU_ON_MS)
            {
                AQ_CpuLedState         = LED_SM_OFF;
                AQ_CpuLedStateStartTime = currentTime;
                AQ_LED__SetPin(AQ_LED_CPU_PIN, 0U);
            }
            break;
        case LED_SM_OFF:
            if (elapsed >= AQ_LED_CPU_OFF_MS)
            {
                AQ_CpuLedState         = LED_SM_ON;
                AQ_CpuLedStateStartTime = currentTime;
                AQ_LED__SetPin(AQ_LED_CPU_PIN, 1U);
            }
            break;
        default:
            AQ_CpuLedState         = LED_SM_ON;
            AQ_CpuLedStateStartTime = currentTime;
            AQ_LED__SetPin(AQ_LED_CPU_PIN, 1U);
            break;
    }

    /*--------------------------------------------------------------------------
     * ERROR LED -- 1Hz blink, only while error active
     *------------------------------------------------------------------------*/
    if (AQ_ErrorLedActive == 1U)
    {
        elapsed = currentTime - AQ_ErrorLedStateStartTime;
        switch (AQ_ErrorLedState)
        {
            case LED_SM_ON:
                if (elapsed >= AQ_LED_ERROR_BLINK_ON_MS)
                {
                    AQ_ErrorLedState          = LED_SM_OFF;
                    AQ_ErrorLedStateStartTime  = currentTime;
                    AQ_LED__SetPin(AQ_LED_ERROR_PIN, 0U);
                }
                break;
            case LED_SM_OFF:
                if (elapsed >= AQ_LED_ERROR_BLINK_OFF_MS)
                {
                    AQ_ErrorLedState          = LED_SM_ON;
                    AQ_ErrorLedStateStartTime  = currentTime;
                    AQ_LED__SetPin(AQ_LED_ERROR_PIN, 1U);
                }
                break;
            default:
                AQ_ErrorLedState          = LED_SM_ON;
                AQ_ErrorLedStateStartTime  = currentTime;
                AQ_LED__SetPin(AQ_LED_ERROR_PIN, 1U);
                break;
        }
    }

    /*--------------------------------------------------------------------------
     * ALARM LED -- 1Hz blink, only while alarm active
     *------------------------------------------------------------------------*/
    if (AQ_AlarmLedActive == 1U)
    {
        elapsed = currentTime - AQ_AlarmLedStateStartTime;
        switch (AQ_AlarmLedState)
        {
            case LED_SM_ON:
                if (elapsed >= AQ_LED_ALARM_BLINK_ON_MS)
                {
                    AQ_AlarmLedState          = LED_SM_OFF;
                    AQ_AlarmLedStateStartTime  = currentTime;
                    AQ_LED__SetPin(AQ_LED_ALARM_PIN, 0U);
                }
                break;
            case LED_SM_OFF:
                if (elapsed >= AQ_LED_ALARM_BLINK_OFF_MS)
                {
                    AQ_AlarmLedState          = LED_SM_ON;
                    AQ_AlarmLedStateStartTime  = currentTime;
                    AQ_LED__SetPin(AQ_LED_ALARM_PIN, 1U);
                }
                break;
            default:
                AQ_AlarmLedState          = LED_SM_ON;
                AQ_AlarmLedStateStartTime  = currentTime;
                AQ_LED__SetPin(AQ_LED_ALARM_PIN, 1U);
                break;
        }
    }

    /*--------------------------------------------------------------------------
     * MODBUS LED -- 1Hz blink while master actively polling
     *------------------------------------------------------------------------*/
    if ((AQ_ModbusLedActive == 1U) &&
        (AQ_ModbusLastPollTime != 0U) &&
        ((currentTime - AQ_ModbusLastPollTime) >= MODBUS_LED_TIMEOUT_MS))
    {
        AQ_ModbusLedActive = 0U;
        AQ_LED__SetPin(AQ_LED_MODBUS_PIN, 0U);
        SERIAL_PRINTF("[AQ_LEDHMI] MODBUS LED: Master silent -- LED OFF\n");
    }

    if (AQ_ModbusLedActive == 1U)
    {
        elapsed = currentTime - AQ_ModbusLedStateStartTime;
        switch (AQ_ModbusLedState)
        {
            case LED_SM_ON:
                if (elapsed >= AQ_LED_MODBUS_BLINK_ON_MS)
                {
                    AQ_ModbusLedState          = LED_SM_OFF;
                    AQ_ModbusLedStateStartTime  = currentTime;
                    AQ_LED__SetPin(AQ_LED_MODBUS_PIN, 0U);
                }
                break;
            case LED_SM_OFF:
                if (elapsed >= AQ_LED_MODBUS_BLINK_OFF_MS)
                {
                    AQ_ModbusLedState          = LED_SM_ON;
                    AQ_ModbusLedStateStartTime  = currentTime;
                    AQ_LED__SetPin(AQ_LED_MODBUS_PIN, 1U);
                }
                break;
            default:
                AQ_ModbusLedState          = LED_SM_ON;
                AQ_ModbusLedStateStartTime  = currentTime;
                AQ_LED__SetPin(AQ_LED_MODBUS_PIN, 1U);
                break;
        }
    }

    /*--------------------------------------------------------------------------
     * BLE LED -- 1Hz blink while advertising or connected (AQ_BleConnected==1)
     * LED driven OFF by LEDHMI__BleDisconnected() / LEDHMI__BleOff() -- nothing
     * to do here when AQ_BleConnected == 0.
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
                    AQ_BleLedStateStartTime  = currentTime;
                    AQ_LED__SetPin(AQ_LED_BLE_PIN, 0U);
                }
                break;
            case LED_SM_OFF:
                if (elapsed >= AQ_LED_BLE_BLINK_OFF_MS)
                {
                    AQ_BleLedState          = LED_SM_ON;
                    AQ_BleLedStateStartTime  = currentTime;
                    AQ_LED__SetPin(AQ_LED_BLE_PIN, 1U);
                }
                break;
            default:
                AQ_BleLedState          = LED_SM_ON;
                AQ_BleLedStateStartTime  = currentTime;
                AQ_LED__SetPin(AQ_LED_BLE_PIN, 1U);
                break;
        }
    }
}

/*==============================================================================
 *                          APPLICATION EVENT FUNCTIONS
 *============================================================================*/

void LEDHMI__AlarmActive(void)
{
    if (AQ_AlarmLedActive == 0U)
    {
        AQ_AlarmLedActive         = 1U;
        AQ_AlarmLedState          = LED_SM_ON;
        AQ_AlarmLedStateStartTime  = millis();
        AQ_LED__SetPin(AQ_LED_ALARM_PIN, 1U);
        SERIAL_PRINTF("[AQ_LEDHMI] ALARM LED: Active - 1Hz blink started\n");
    }
}

void LEDHMI__AlarmCleared(void)
{
    if (AQ_AlarmLedActive == 0U) { return; }
    AQ_AlarmLedActive = 0U;
    AQ_LED__SetPin(AQ_LED_ALARM_PIN, 0U);
    SERIAL_PRINTF("[AQ_LEDHMI] ALARM LED: Cleared - OFF\n");
}

void LEDHMI__ErrorActive(void)
{
    if (AQ_ErrorLedActive == 0U)
    {
        AQ_ErrorLedActive         = 1U;
        AQ_ErrorLedState          = LED_SM_ON;
        AQ_ErrorLedStateStartTime  = millis();
        AQ_LED__SetPin(AQ_LED_ERROR_PIN, 1U);
        SERIAL_PRINTF("[AQ_LEDHMI] ERROR LED: Active - 1Hz blink started\n");
    }
}

void LEDHMI__ErrorCleared(void)
{
    if (AQ_ErrorLedActive == 0U) { return; }
    AQ_ErrorLedActive = 0U;
    AQ_LED__SetPin(AQ_LED_ERROR_PIN, 0U);
    SERIAL_PRINTF("[AQ_LEDHMI] ERROR LED: Cleared - OFF\n");
}

void LEDHMI__ModbusPoll(void)
{
    uint32_t now = millis();
    AQ_ModbusLastPollTime = now;

    if (AQ_ModbusLedActive == 0U)
    {
        AQ_ModbusLedActive         = 1U;
        AQ_ModbusLedState          = LED_SM_ON;
        AQ_ModbusLedStateStartTime  = now;
        AQ_LED__SetPin(AQ_LED_MODBUS_PIN, 1U);
        SERIAL_PRINTF("[AQ_LEDHMI] MODBUS LED: Master detected - 1Hz blink started\n");
    }
}

void LEDHMI__ModbusDataReceived(void)
{
    AQ_ModbusLastPollTime = millis();
}

void LEDHMI__BleConnected(void)
{
    uint32_t now = millis();
    AQ_BleConnected         = 1U;
    AQ_BleLedState          = LED_SM_ON;
    AQ_BleLedStateStartTime  = now;
    AQ_LED__SetPin(AQ_LED_BLE_PIN, 1U);
    SERIAL_PRINTF("[AQ_LEDHMI] BLE LED: Connected - 1Hz blink started\n");
}

void LEDHMI__BleDisconnected(void)
{
    AQ_BleConnected = 0U;
    AQ_LED__SetPin(AQ_LED_BLE_PIN, 0U);
    SERIAL_PRINTF("[AQ_LEDHMI] BLE LED: Disconnected - LED OFF\n");
}

void LEDHMI__BleOff(void)
{
    /* Called when the physical BLE switch is turned OFF.
     * Stops the blink SM and drives LED OFF.
     * Unlike BleDisconnected(), this leaves AQ_BleConnected=0 so the SM
     * does not restart until BleConnected() is explicitly called again. */
    AQ_BleConnected = 0U;
    AQ_LED__SetPin(AQ_LED_BLE_PIN, 0U);
    SERIAL_PRINTF("[AQ_LEDHMI] BLE LED: Switch OFF - LED OFF\n");
}