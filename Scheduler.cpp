// /**
//  * @file Scheduler.cpp
//  * @brief FreeRTOS Scheduler Implementation — LED Project
//  *
//  *  Core 0 Task A (50ms,  pri=2): CmdParser, Modbus, BLEComm, App
//  *  Core 1 Task B (100ms, pri=1): WiFiComm, OTA_WiFi  ← isolated on Core1
//  *  Core 1 Task   (50ms,  pri=2): LEDHMI, RTC tick handler
//  *
//  * @version 2.6.0 — WiFi/OTA in dedicated low-priority task on Core0
//  *                  Fixes watchdog triggered by WiFi blocking BLE 50ms task
//  */

// #include "Scheduler.h"
// #include "AppMutex.h"
// #include "SensorConfig.h"
// #include "RTC.h"

// #include "CmdParser.h"
// #include "Modbus.h"
// #include "App.h"
// #include "Fan.h"
// #include "Buzzer.h"
// #include "BLEComm.h"
// #include "AQ_LEDHMI.h"
// #include "ThresholdSettings.h"
// #include "WiFiComm.h"
// #include "OTA_WiFi.h"

// #include "Arduino.h"
// #include <freertos/FreeRTOS.h>
// #include <freertos/task.h>

// /*==============================================================================
//  *                          EXTERNAL REFERENCES
//  *============================================================================*/
// extern volatile bool       g_setup_complete;
// extern volatile bool       rtc_tick_flag;
// extern volatile SensorData currentData;

// /*==============================================================================
//  *                          PRIVATE CONSTANTS
//  *============================================================================*/
// #define TASK_STACK_CORE0        (16384U)
// #define TASK_STACK_WIFI         (16384U)
// #define TASK_STACK_CORE1        (16384U)
// #define TASK_PRIORITY_HIGH      (2U)
// #define TASK_PRIORITY_WIFI      (1U)
// #define TASK_PERIOD_MS          (50U)
// #define TASK_WIFI_PERIOD_MS     (100U)

// /*==============================================================================
//  *                          PRIVATE FUNCTION PROTOTYPES
//  *============================================================================*/
// static void Task_50ms_Core0(void *pvParameters);
// static void Task_WiFi_Core0(void *pvParameters);
// static void Task_50ms_Core1(void *pvParameters);
// static void Scheduler_RtcTickHandler(void);

// /*==============================================================================
//  *                          PUBLIC FUNCTIONS
//  *============================================================================*/

// void Scheduler_ExecuteInitCore1(void)
// {
//     LEDHMI__Init();
//     BLEComm__Init();
//     Thresholds__Init();
//     SERIAL_PRINTF("[SCHED] Core1 init done\n");
// }

// void Scheduler_ExecuteInitCore0(void)
// {
//     Fan__Init();
//     Buzzer__Init();
//     CmdParser__Init();
//     Modbus_Init();
//     App__Init();
//     WiFiComm__Init();
//     OTA_WiFi__Init();
//     /* Register MQTT OTA callback so device can be updated from anywhere */
//     // WiFiComm__RegisterMQTTRxCallback(OTA_WiFi__MQTTOTAHandler);
//     SERIAL_PRINTF("[SCHED] Core0 init done\n");
// }

// bool Scheduler_Init(void)
// {
//     BaseType_t r0 = xTaskCreatePinnedToCore(
//         Task_50ms_Core0, "Task_C0", TASK_STACK_CORE0,
//         NULL, TASK_PRIORITY_HIGH, NULL, 0);

//     BaseType_t rw = xTaskCreatePinnedToCore(
//         Task_WiFi_Core0, "Task_WiFi", TASK_STACK_WIFI,
//         NULL, TASK_PRIORITY_WIFI, NULL, 0);

//     BaseType_t r1 = xTaskCreatePinnedToCore(
//         Task_50ms_Core1, "Task_C1", TASK_STACK_CORE1,
//         NULL, TASK_PRIORITY_HIGH, NULL, 1);

//     if ((r0 != pdPASS) || (rw != pdPASS) || (r1 != pdPASS))
//     {
//         Serial.println("[SCHED] ERROR: task creation failed");
//         return false;
//     }
//     return true;
// }

// void Scheduler_Start(void)
// {
//     g_setup_complete = true;
//     SERIAL_PRINTF("[SCHED] Started\n");
// }

// /*==============================================================================
//  *                          PRIVATE TASK BODIES
//  *============================================================================*/

// static void Task_50ms_Core0(void *pvParameters)
// {
//     (void)pvParameters;
//     while (!g_setup_complete) { vTaskDelay(pdMS_TO_TICKS(10)); }

//     TickType_t xLastWake = xTaskGetTickCount();
//     while (1)
//     {
//         CmdParser__Handler();
//         Modbus_Handler();
//         BLEComm__Handler();
//         App__Handler();
//         vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(TASK_PERIOD_MS));
//     }
// }

// static void Task_WiFi_Core0(void *pvParameters)
// {
//     (void)pvParameters;
//     while (!g_setup_complete) { vTaskDelay(pdMS_TO_TICKS(10)); }

//     TickType_t xLastWake = xTaskGetTickCount();
//     while (1)
//     {
//         WiFiComm__Handler();
//         OTA_WiFi__Handler();
//         vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(TASK_WIFI_PERIOD_MS));
//     }
// }

// static void Task_50ms_Core1(void *pvParameters)
// {
//     (void)pvParameters;
//     while (!g_setup_complete) { vTaskDelay(pdMS_TO_TICKS(10)); }

//     TickType_t xLastWake = xTaskGetTickCount();
//     while (1)
//     {
//         LEDHMI__Handler();
//         Scheduler_RtcTickHandler();
//         vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(TASK_PERIOD_MS));
//     }
// }

// static void Scheduler_RtcTickHandler(void)
// {
//     /* RTC hardware not verified — disabled to prevent NULL dereference crash */
//     rtc_tick_flag = false;
// }

/**
 * @file Scheduler.cpp
 * @brief FreeRTOS Scheduler Implementation — LED Project
 *
 *  Core 0 Task A (50ms,  pri=2): CmdParser, Modbus, BLEComm, App
 *  Core 1 Task B (100ms, pri=1): WiFiComm, OTA_WiFi  ← isolated on Core1
 *  Core 1 Task   (50ms,  pri=2): LEDHMI, RTC tick handler
 *
 * @version 2.6.0 — WiFi/OTA in dedicated low-priority task on Core0
 *                  Fixes watchdog triggered by WiFi blocking BLE 50ms task
 */

#include "Scheduler.h"
#include "AppMutex.h"
#include "SensorConfig.h"
#include "RTC.h"

#include "CmdParser.h"
#include "Modbus.h"
#include "App.h"
#include "Fan.h"
#include "Buzzer.h"
#include "BLEComm.h"
#include "AQ_LEDHMI.h"
#include "ThresholdSettings.h"
#include "WiFiComm.h"
#include "OTA_WiFi.h"

#include "Arduino.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/*==============================================================================
 *                          EXTERNAL REFERENCES
 *============================================================================*/
extern volatile bool       g_setup_complete;
extern volatile bool       rtc_tick_flag;
extern volatile SensorData currentData;

/*==============================================================================
 *                          PRIVATE CONSTANTS
 *============================================================================*/
#define TASK_STACK_CORE0        (16384U)
#define TASK_STACK_WIFI         (16384U)
#define TASK_STACK_CORE1        (16384U)
#define TASK_PRIORITY_HIGH      (2U)
#define TASK_PRIORITY_WIFI      (1U)
#define TASK_PERIOD_MS          (50U)
#define TASK_WIFI_PERIOD_MS     (100U)

/*==============================================================================
 *                          PRIVATE FUNCTION PROTOTYPES
 *============================================================================*/
static void Task_50ms_Core0(void *pvParameters);
static void Task_WiFi_Core0(void *pvParameters);
static void Task_50ms_Core1(void *pvParameters);
static void Scheduler_RtcTickHandler(void);

/*==============================================================================
 *                          PUBLIC FUNCTIONS
 *============================================================================*/

void Scheduler_ExecuteInitCore1(void)
{
    LEDHMI__Init();
    BLEComm__Init();
    Thresholds__Init();
    SERIAL_PRINTF("[SCHED] Core1 init done\n");
}

void Scheduler_ExecuteInitCore0(void)
{
    Fan__Init();
    Buzzer__Init();
    CmdParser__Init();
    Modbus_Init();
    App__Init();
    WiFiComm__Init();
    OTA_WiFi__Init();
    /* Register MQTT OTA callback so device can be updated from anywhere */
    WiFiComm__RegisterMQTTRxCallback(OTA_WiFi__MQTTOTAHandler);
    SERIAL_PRINTF("[SCHED] Core0 init done\n");
}

bool Scheduler_Init(void)
{
    BaseType_t r0 = xTaskCreatePinnedToCore(
        Task_50ms_Core0, "Task_C0", TASK_STACK_CORE0,
        NULL, TASK_PRIORITY_HIGH, NULL, 0);

    BaseType_t rw = xTaskCreatePinnedToCore(
        Task_WiFi_Core0, "Task_WiFi", TASK_STACK_WIFI,
        NULL, TASK_PRIORITY_WIFI, NULL, 0);

    BaseType_t r1 = xTaskCreatePinnedToCore(
        Task_50ms_Core1, "Task_C1", TASK_STACK_CORE1,
        NULL, TASK_PRIORITY_HIGH, NULL, 1);

    if ((r0 != pdPASS) || (rw != pdPASS) || (r1 != pdPASS))
    {
        Serial.println("[SCHED] ERROR: task creation failed");
        return false;
    }
    return true;
}

void Scheduler_Start(void)
{
    g_setup_complete = true;
    SERIAL_PRINTF("[SCHED] Started\n");
}

/*==============================================================================
 *                          PRIVATE TASK BODIES
 *============================================================================*/

static void Task_50ms_Core0(void *pvParameters)
{
    (void)pvParameters;
    while (!g_setup_complete) { vTaskDelay(pdMS_TO_TICKS(10)); }

    TickType_t xLastWake = xTaskGetTickCount();
    while (1)
    {
        CmdParser__Handler();
        Modbus_Handler();
        BLEComm__Handler();
        App__Handler();
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(TASK_PERIOD_MS));
    }
}

static void Task_WiFi_Core0(void *pvParameters)
{
    (void)pvParameters;
    while (!g_setup_complete) { vTaskDelay(pdMS_TO_TICKS(10)); }

    TickType_t xLastWake = xTaskGetTickCount();
    while (1)
    {
        WiFiComm__Handler();
        OTA_WiFi__Handler();
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(TASK_WIFI_PERIOD_MS));
    }
}

static void Task_50ms_Core1(void *pvParameters)
{
    (void)pvParameters;
    while (!g_setup_complete) { vTaskDelay(pdMS_TO_TICKS(10)); }

    TickType_t xLastWake = xTaskGetTickCount();
    while (1)
    {
        LEDHMI__Handler();
        Scheduler_RtcTickHandler();
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(TASK_PERIOD_MS));
    }
}

static void Scheduler_RtcTickHandler(void)
{
    /* RTC hardware not verified — disabled to prevent NULL dereference crash */
    rtc_tick_flag = false;
}