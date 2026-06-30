/**
 * @file Scheduler.cpp
 * @brief FreeRTOS Scheduler -- Generic Engine
 *
 * Pure engine: reads all configuration from Scheduler_Cfg.h / Scheduler_Cfg.cpp.
 * No module names, stack sizes, priorities, or periods are hardcoded here.
 * To add/remove a module or tune task parameters, edit Scheduler_Cfg.cpp only.
 *
 * @version 2.1.0
 *
 * v2.1.0 changes (review fixes):
 *   - g_setup_complete spin-wait: portMEMORY_BARRIER() added before each read
 *     to ensure the write from Core 1 (setup) is visible on Core 0 tasks.
 *   - Scheduler_Start(): g_setup_complete = true removed -- the .ino sets it
 *     before calling Scheduler_Start(); double write was redundant and
 *     confusing about ownership. Scheduler_Start() is now informational only.
 *   - Scheduler_RtcTickHandler(): cleared rtc_tick_flag removed -- ISR is not
 *     attached so flag is always false; the write was a no-op (MISRA 2.2).
 *     Function retained as a placeholder for future RTC tick integration.
 *   - Scheduler_Init() error log: Serial.println replaced with SERIAL_PRINTF
 *     for consistency with mutex-safe serial pattern.
 *   - All task body single-statement if/while given braces (MISRA A3).
 */

#include "Scheduler.h"
#include "Scheduler_Cfg.h"
#include "AppMutex.h"
#include "SensorConfig.h"
#include "RTC.h"
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
 *                          PRIVATE FUNCTION PROTOTYPES
 *============================================================================*/
static void Task_50ms_Core0(void *pvParameters);
static void Task_100ms_WiFi(void *pvParameters);
static void Task_50ms_Core1(void *pvParameters);
static void Scheduler_RtcTickHandler(void);

/*==============================================================================
 *                          PUBLIC FUNCTIONS
 *============================================================================*/

void Scheduler_ExecuteInitCore1(void)
{
    for (uint8_t i = 0U; Init_Core1_Functions[i] != NULL; i++)
    {
        Init_Core1_Functions[i]();
    }
    SERIAL_PRINTF("[SCHED] Core1 init done\n");
}

void Scheduler_ExecuteInitCore0(void)
{
    for (uint8_t i = 0U; Init_Core0_Functions[i] != NULL; i++)
    {
        Init_Core0_Functions[i]();
    }
    SERIAL_PRINTF("[SCHED] Core0 init done\n");
}

bool Scheduler_Init(void)
{
    BaseType_t r0 = xTaskCreatePinnedToCore(
        Task_50ms_Core0, "Task_C0",
        SCHED_STACK_CORE0, NULL, SCHED_PRIORITY_HIGH, NULL, 0);

    BaseType_t rw = xTaskCreatePinnedToCore(
        Task_100ms_WiFi, "Task_WiFi",
        SCHED_STACK_WIFI, NULL, SCHED_PRIORITY_WIFI, NULL, 0);

    BaseType_t r1 = xTaskCreatePinnedToCore(
        Task_50ms_Core1, "Task_C1",
        SCHED_STACK_CORE1, NULL, SCHED_PRIORITY_HIGH, NULL, 1);

    if ((r0 != pdPASS) || (rw != pdPASS) || (r1 != pdPASS))
    {
        SERIAL_PRINTF("[SCHED] ERROR: task creation failed (r0=%d rw=%d r1=%d)\n",
                      (int)r0, (int)rw, (int)r1);
        return false;
    }
    return true;
}

void Scheduler_Start(void)
{
    /*--------------------------------------------------------------------------
     * g_setup_complete is set by the caller (.ino) BEFORE calling here.
     * Scheduler_Start() is retained as a named lifecycle hook -- it serves
     * as a logical boundary between "init complete" and "tasks running".
     * Do NOT set g_setup_complete here again -- the .ino is the sole owner.
     *------------------------------------------------------------------------*/
    SERIAL_PRINTF("[SCHED] Started\n");
}

/*==============================================================================
 *                          PRIVATE TASK BODIES
 *============================================================================*/

static void Task_50ms_Core0(void *pvParameters)
{
    (void)pvParameters;

    /* portMEMORY_BARRIER() ensures the Core 1 write to g_setup_complete
     * is visible here before we check it. Without the barrier, the Xtensa
     * LX7 cache may return a stale false. */
    portMEMORY_BARRIER();
    while (!g_setup_complete)
    {
        vTaskDelay(pdMS_TO_TICKS(10U));
        portMEMORY_BARRIER();
    }

    TickType_t xLastWake = xTaskGetTickCount();
    while (1)
    {
        for (uint8_t i = 0U; Task_50ms_Core0_Functions[i] != NULL; i++)
        {
            Task_50ms_Core0_Functions[i]();
        }
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(SCHED_PERIOD_CORE0_MS));
    }
}

static void Task_100ms_WiFi(void *pvParameters)
{
    (void)pvParameters;

    portMEMORY_BARRIER();
    while (!g_setup_complete)
    {
        vTaskDelay(pdMS_TO_TICKS(10U));
        portMEMORY_BARRIER();
    }

    TickType_t xLastWake = xTaskGetTickCount();
    while (1)
    {
        for (uint8_t i = 0U; Task_100ms_WiFi_Functions[i] != NULL; i++)
        {
            Task_100ms_WiFi_Functions[i]();
        }
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(SCHED_PERIOD_WIFI_MS));
    }
}

static void Task_50ms_Core1(void *pvParameters)
{
    (void)pvParameters;

    portMEMORY_BARRIER();
    while (!g_setup_complete)
    {
        vTaskDelay(pdMS_TO_TICKS(10U));
        portMEMORY_BARRIER();
    }

    TickType_t xLastWake = xTaskGetTickCount();
    while (1)
    {
        for (uint8_t i = 0U; Task_50ms_Core1_Functions[i] != NULL; i++)
        {
            Task_50ms_Core1_Functions[i]();
        }
        Scheduler_RtcTickHandler();
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(SCHED_PERIOD_CORE1_MS));
    }
}

static void Scheduler_RtcTickHandler(void)
{
    /* RTC 1Hz interrupt is not currently attached (hardware not yet verified).
     * rtc_tick_flag will always be false until attachInterrupt() is called.
     * Placeholder retained for forward-compatibility -- implement here when
     * RTC hardware is confirmed and interrupt is attached in the .ino.
     *
     * When enabled:
     *   if (rtc_tick_flag)
     *   {
     *       rtc_tick_flag = false;
     *       App__RTCHandler();
     *   }
     */
    (void)rtc_tick_flag;  /* suppress unused-variable warning while disabled */
}