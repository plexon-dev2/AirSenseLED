/**
 * @file Scheduler_Cfg.h
 * @brief AQI Monitor Dual Core Task Configuration — LED Build
 * @details Defines all init functions and task handlers for both cores.
 *
 *          Core 0 (50ms) — Communication & Logic:
 *              App, CmdParser, Modbus, BLEComm, Fan
 *
 *          Core 1 (50ms) — LED HMI:
 *              Buzzer, Thresholds, LEDHMI
 *
 *          BLE stack runs on Core 0 by default on ESP32-S3.
 *
 * HOW TO ADD A NEW MODULE:
 * ========================
 * 1. Add #include for the module header below
 * 2. Add Init function to Init_Core0_Functions[] or Init_Core1_Functions[]
 * 3. Add Handler function to Task_50ms_Core0_Functions[] or
 *    Task_50ms_Core1_Functions[]
 * 4. No changes needed in Scheduler.cpp
 *
 * CHANGES FROM TFT BUILD (→ v1.5.0):
 * ===========================================
 *   1. TFT includes REMOVED: ColorTFT.h, TouchController.h,
 *      DisplayProcess.h, BLEScreenProcess.h — not present in LED build.
 *
 *   2. App__Init MOVED TO FIRST in Core 0 init sequence.
 *      App__Init enables sensor power (HW_PIN_SENSOR_POWER / GPIO48).
 *      CmdParser__Init sets up sensor UART immediately after.
 *      Previously App__Init ran last — sensor was not powered when
 *      CmdParser first read the UART → floating RX line → 0xFF bytes.
 *
 *   3. Task_10ms_Modbus KEPT with priority 2 — Modbus RTU slave must
 *      respond within inter-frame gap. 50ms task too slow → timeouts.
 *
 * @version 1.5.0 — LED build
 */

#ifndef TASK_CONFIG_H
#define TASK_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/*==============================================================================
 *                          SCHEDULER CONFIGURATION
 *============================================================================*/

#define SCHEDULER_CFG_MAX_INIT_FUNCTIONS        (16U)
#define SCHEDULER_CFG_MAX_FUNCTIONS_PER_TASK    (16U)
#define SCHEDULER_CFG_SERIAL_DEBUG              (1U)

/*==============================================================================
 *                          CORE AFFINITY DEFINITIONS
 *============================================================================*/

#define SCHEDULER_CORE_0            (0)
#define SCHEDULER_CORE_1            (1)
#define SCHEDULER_CORE_ANY          tskNO_AFFINITY

/*==============================================================================
 *                          TYPE DEFINITIONS
 *============================================================================*/

typedef void (*InitFunc_t)(void);
typedef void (*TaskFunctionPtr_t)(void);
typedef void (*TaskFunction_t)(void *);

typedef struct
{
    const char       *name;        /**< Task name for debug                  */
    TaskFunction_t    function;    /**< FreeRTOS task function               */
    uint32_t          period_ms;   /**< Task period in milliseconds          */
    uint32_t          stack_size;  /**< Stack size in BYTES                  */
    uint8_t           priority;    /**< FreeRTOS task priority               */
    BaseType_t        core_id;     /**< Core affinity (0, 1, or ANY)         */
    TaskHandle_t      handle;      /**< Task handle — filled by Scheduler    */
} TaskConfig_t;

/*==============================================================================
 *                          MODULE INCLUDES — LED Build
 *  ColorTFT.h, TouchController.h, DisplayProcess.h, BLEScreenProcess.h
 *  REMOVED — TFT hardware not present in LED build.
 *============================================================================*/
#include "BLEComm.h"
#include "BLESwitch.h"
#include "WiFiComm.h"
#include "OTA_WiFi.h"
#include "CmdParser.h"
#include "Modbus.h"
#include "App.h"
#include "Fan.h"
#include "Buzzer.h"
#include "RTC.h"
#include "AQ_LEDHMI.h"
#include "ThresholdSettings.h"

/*==============================================================================
 *                          TASK FUNCTION PROTOTYPES
 *============================================================================*/
void Task_50ms_Core0(void *pvParameters);
void Task_50ms_Core1(void *pvParameters);
void Task_10ms_Modbus(void *pvParameters);  /**< Dedicated fast Modbus task   */

/*==============================================================================
 *                    CORE 0 — INIT FUNCTIONS
 *          Communication & Logic — called from setup() before tasks start.
 *
 *          ORDER IS CRITICAL:
 *          App__Init MUST run first — it enables sensor power (GPIO48).
 *          CmdParser__Init runs second — sensor UART is initialised only
 *          after the sensor is already powered. Previously App__Init ran
 *          last which caused the sensor RX pin to float (reads 0xFF).
 *============================================================================*/
static InitFunc_t Init_Core0_Functions[] =
{
    App__Init,              /**< FIRST — enables sensor power (GPIO48)         */
    CmdParser__Init,        /**< Sensor UART — sensor now powered before read  */
    Modbus_Init,            /**< RS485 Modbus slave                            */
    BLEComm__Init,          /**< BLE server + advertising                      */
    BLESwitch__Init,        /**< BLE switch control — must run after BLEComm   */
    WiFiComm__Init,         /**< WiFi connect + TCP server/client              */
    OTA_WiFi__Init,         /**< OTA update over WiFi                          */
    Fan__Init,              /**< Fan PWM output                                */
    NULL                    /**< MUST be NULL terminated                       */
};

/*==============================================================================
 *                    CORE 1 — INIT FUNCTIONS
 *          LED HMI — called from setup() before tasks start.
 *============================================================================*/
static InitFunc_t Init_Core1_Functions[] =
{
    Buzzer__Init,           /**< Buzzer PWM output                            */
    Thresholds__Init,       /**< Alarm threshold settings (NVS read)          */
    LEDHMI__Init,           /**< Status LED init                              */
    NULL                    /**< MUST be NULL terminated                      */
};

/*==============================================================================
 *                    CORE 0 — TASK HANDLER FUNCTIONS (50ms)
 *          Communication & Logic handlers.
 *          Modbus_Handler moved to dedicated Task_10ms_Modbus (priority 2)
 *          for fast slave response — 50ms cycle caused master timeouts.
 *============================================================================*/
static TaskFunctionPtr_t Task_50ms_Core0_Functions[] =
{
    CmdParser__Handler,     /**< Sensor UART frame parser                      */
    App__Handler,           /**< Application logic                             */
    BLEComm__Handler,       /**< BLE notify + connection management            */
    BLESwitch__Handler,     /**< GPIO8 switch poll — start/stop advertising    */
    WiFiComm__Handler,      /**< WiFi TCP server/client                        */
    OTA_WiFi__Handler,      /**< OTA update handler                            */
    Fan__Handler,           /**< Fan PWM speed control                         */
    NULL
    /* Modbus_Handler REMOVED — runs in dedicated Task_10ms_Modbus (Core 0)   */
};

/*==============================================================================
 *                    MODBUS DEDICATED TASK (10ms) — Core 0, Priority 2
 *          Modbus RTU slave must respond within the inter-frame gap (~1-2ms
 *          at 9600 baud). A 50ms task cycle causes master timeout errors.
 *          Priority 2 (higher than 50ms tasks at priority 1) ensures
 *          Modbus_Handler always preempts and responds in time.
 *          Runs on Core 0 alongside BLE — both are time-critical.
 *============================================================================*/
static TaskFunctionPtr_t Task_10ms_Modbus_Functions[] =
{
    Modbus_Handler,         /**< RS485 Modbus slave — must run fast            */
    NULL
};

/*==============================================================================
 *                    CORE 1 — TASK HANDLER FUNCTIONS (50ms)
 *          LED HMI handlers.
 *          Thresholds__Handler : Handles NVS save, BLE/Modbus threshold
 *                                updates — separate from LEDHMI which only
 *                                reads threshold values for alarm LEDs.
 *============================================================================*/
static TaskFunctionPtr_t Task_50ms_Core1_Functions[] =
{
    Thresholds__Handler,    /**< Threshold NVS save + BLE/Modbus update logic  */
    LEDHMI__Handler,        /**< All LED state machines + threshold alarm check */
    NULL
};

/*==============================================================================
 *                          TASK CONFIGURATION ARRAY
 *
 *  stack_size is in BYTES. Scheduler divides by 4 when passing to FreeRTOS
 *  (which takes words). 32768 bytes = 8192 words.
 *
 *  | Name             | Period | Stack  | Pri | Core | Notes                  |
 *  |------------------|--------|--------|-----|------|------------------------|
 *  | Task_50ms_Core0  |  50ms  | 32768B |  1  |  0   | BLE, CmdParser, App    |
 *  | Task_50ms_Core1  |  50ms  | 32768B |  1  |  1   | LED HMI                |
 *  | Task_10ms_Modbus |  10ms  |  8192B |  2  |  0   | Must be priority 2     |
 *                                                       (preempts 50ms tasks)  |
 *============================================================================*/
static TaskConfig_t Task_Configurations[] =
{
    /* name                function           period  stack    pri  core              handle */
    { "Task_50ms_Core0",   Task_50ms_Core0,   50U,   32768U,  1U,  SCHEDULER_CORE_0, NULL },
    { "Task_50ms_Core1",   Task_50ms_Core1,   50U,   32768U,  1U,  SCHEDULER_CORE_1, NULL },
    { "Task_10ms_Modbus",  Task_10ms_Modbus,  10U,    8192U,  2U,  SCHEDULER_CORE_0, NULL },

    /* MUST end with NULL name entry */
    { NULL,                NULL,              0U,    0U,      0U,  0,                NULL }
};

#endif /* TASK_CONFIG_H */