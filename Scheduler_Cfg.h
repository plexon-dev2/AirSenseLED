/**
 * @file Scheduler_Cfg.h
 * @brief FreeRTOS Scheduler Configuration -- AirSense LED Build
 *
 * Defines all init functions, task handlers, and task parameters.
 *
 *   Core 0 Task A (50ms,  pri=2): CmdParser, Modbus, BLEComm, App, Fan, CurrentSense
 *   Core 0 Task B (100ms, pri=1): WiFiComm, OTA_WiFi
 *   Core 1 Task   (50ms,  pri=2): LEDHMI, Thresholds
 *
 * HOW TO ADD A NEW MODULE:
 * ========================
 * 1. Add #include for the module header below
 * 2. Add Init function to Init_Core0_Functions[] or Init_Core1_Functions[]
 * 3. Add Handler function to the appropriate Task_*_Functions[] array
 * 4. No changes needed in Scheduler.cpp
 *
 * INIT ORDER NOTE:
 * ================
 * App__Init MUST be first in Core 0 init -- it calls SensorBoot__MeasureAndWait()
 * which drives GPIO48 HIGH (sensor power) and waits for the first valid sensor
 * frame before returning. CmdParser__Init opens the sensor UART immediately after.
 *
 * BLE NOTE:
 * =========
 * BLEComm__Init runs on Core 1 (BLE stack preference).
 * BLEComm__Handler runs on Core 0 (Task_C0).
 * The Arduino BLE library is internally thread-safe -- this cross-core
 * init/handler split is intentional and correct.
 *
 * ODR WARNING:
 * ============
 * The static arrays and RTC_InitWrapper below are defined here (not in a .cpp).
 * This file must be included ONLY by Scheduler.cpp. If any other translation
 * unit includes this header, it will silently get duplicate copies of the arrays
 * in flash/RAM (ODR violation). All other files that need SCHED_* constants
 * should include only the constant section via a separate minimal header if needed.
 *
 * @version 2.1.0
 *
 * v2.1.0 changes (review fixes):
 *   - Fan__Handler added to Task_50ms_Core0_Functions -- was missing, causing
 *     kickstart burst to run indefinitely (fan stuck at full speed on startup).
 *   - const added to all function pointer arrays (never modified at runtime).
 *   - RTC_InitWrapper: Serial.printf replaced with SERIAL_PRINTF (mutex-safe).
 *   - App init comment updated: SensorBoot__MeasureAndWait() replaces old
 *     "GPIO48 + 3s warmup" description (matches App.cpp v3.16.0+).
 *   - BLE cross-core init/handler split documented explicitly.
 *   - ODR warning added explaining why this header must only be included once.
 */

#ifndef SCHEDULER_CFG_H
#define SCHEDULER_CFG_H

#include <stdint.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/*==============================================================================
 *                          TYPE DEFINITIONS
 *============================================================================*/
typedef void (*InitFunc_t)(void);
typedef void (*TaskFunctionPtr_t)(void);

/*==============================================================================
 *                          TASK PARAMETERS
 *
 *  | Task            | Period | Stack  | Pri | Core |
 *  |-----------------|--------|--------|-----|------|
 *  | Task_C0         |  50ms  | 16384B |  2  |  0   |
 *  | Task_WiFi       | 100ms  | 16384B |  1  |  0   |
 *  | Task_C1         |  50ms  | 16384B |  2  |  1   |
 *============================================================================*/
#define SCHED_STACK_CORE0           (16384U)
#define SCHED_STACK_WIFI            (16384U)
#define SCHED_STACK_CORE1           (16384U)

#define SCHED_PRIORITY_HIGH         (2U)
#define SCHED_PRIORITY_WIFI         (1U)

#define SCHED_PERIOD_CORE0_MS       (50U)
#define SCHED_PERIOD_WIFI_MS        (100U)
#define SCHED_PERIOD_CORE1_MS       (50U)

/*==============================================================================
 *                          MODULE INCLUDES
 *============================================================================*/
#include "AppMutex.h"       /* SERIAL_PRINTF */
#include "App.h"
#include "RTC.h"
#include "CmdParser.h"
#include "Modbus.h"
#include "BLEComm.h"
#include "Fan.h"
#include "Buzzer.h"
#include "AQ_LEDHMI.h"
#include "ThresholdSettings.h"
#include "WiFiComm.h"
#include "OTA_WiFi.h"
#include "CurrentSense.h"

/*==============================================================================
 *                          WRAPPER FUNCTIONS
 *  RTC__Init() returns int8_t but InitFunc_t needs void(*)(void).
 *  Wrapper discards the return value and logs an error if init fails.
 *============================================================================*/
static void RTC_InitWrapper(void)
{
    int8_t result = RTC__Init();
    if (result != RTC_OK)
    {
        SERIAL_PRINTF("[SCHED] RTC__Init failed, code=%d\n", (int)result);
    }
}

/*==============================================================================
 *                    CORE 0 -- INIT FUNCTIONS
 *
 * App__Init MUST be first -- calls SensorBoot__MeasureAndWait() which:
 *   - Drives GPIO48 HIGH (sensor power supply enable)
 *   - Polls CmdParser until first valid sensor frame arrives
 *   - Records actual boot time for MQTT report
 *   - Falls back to CFG_BOOT_SENSOR_TIMEOUT_MS if sensor does not respond
 * CmdParser__Init opens the sensor UART immediately after.
 *============================================================================*/
static const InitFunc_t Init_Core0_Functions[] =
{
    App__Init,           /**< FIRST -- SensorBoot__MeasureAndWait() + MQTT setup */
    RTC_InitWrapper,     /**< PCF8563T RTC over I2C (GPIO15/16)                  */
    Fan__Init,           /**< Fan PWM output (GPIO6)                              */
    Buzzer__Init,        /**< Buzzer PWM output (GPIO42)                          */
    CmdParser__Init,     /**< Sensor UART -- sensor already powered by App__Init  */
    Modbus_Init,         /**< RS485 Modbus slave (GPIO17/18/14)                   */
    WiFiComm__Init,      /**< WiFi + MQTT                                         */
    OTA_WiFi__Init,      /**< OTA update over WiFi/MQTT                           */
    CurrentSense__Init,  /**< ADC current monitoring (GPIO1/GPIO2)                */
    NULL                 /**< MUST be NULL-terminated                             */
};

/*==============================================================================
 *                    CORE 1 -- INIT FUNCTIONS
 *
 * BLEComm__Init runs on Core 1 (BLE stack preference).
 * BLEComm__Handler runs on Core 0 -- see BLE NOTE in file header.
 *============================================================================*/
static const InitFunc_t Init_Core1_Functions[] =
{
    LEDHMI__Init,        /**< Status LEDs (GPIO21/38/39/40/45/47)   */
    BLEComm__Init,       /**< BLE server + advertising               */
    Thresholds__Init,    /**< Alarm threshold settings (NVS read)    */
    NULL                 /**< MUST be NULL-terminated                */
};

/*==============================================================================
 *                    CORE 0 TASK A -- HANDLERS (50ms, pri=2)
 *============================================================================*/
static const TaskFunctionPtr_t Task_50ms_Core0_Functions[] =
{
    CmdParser__Handler,    /**< Sensor UART frame parser                    */
    Modbus_Handler,        /**< RS485 Modbus slave                          */
    BLEComm__Handler,      /**< BLE notify + connection management          */
    App__Handler,          /**< Application logic + BLE switch              */
    Fan__Handler,          /**< Kickstart timer + tach RPM (was missing!)   */
    CurrentSense__Handler, /**< ADC current sampling + OC detection         */
    NULL
};

/*==============================================================================
 *                    CORE 0 TASK B -- HANDLERS (100ms, pri=1)
 *============================================================================*/
static const TaskFunctionPtr_t Task_100ms_WiFi_Functions[] =
{
    WiFiComm__Handler,   /**< WiFi + MQTT       */
    OTA_WiFi__Handler,   /**< OTA update handler */
    NULL
};

/*==============================================================================
 *                    CORE 1 TASK -- HANDLERS (50ms, pri=2)
 *============================================================================*/
static const TaskFunctionPtr_t Task_50ms_Core1_Functions[] =
{
    LEDHMI__Handler,         /**< All LED state machines + alarm check   */
    Thresholds__Handler,     /**< Threshold NVS save + BLE/Modbus update */
    NULL
};

#endif /* SCHEDULER_CFG_H */