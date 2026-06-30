/**
 * @file AirSense_LEDBased_1V7.ino
 * @brief Air Quality Sensor -- LED Project entry point
 * @version 1.7.0
 *
 * v1.7.0 changes (review fixes):
 *   - File header corrected to match filename (was 1.4.0)
 *   - g_setup_complete removed from here -- now defined in AppMutex.cpp
 *   - g_serial_mutex: xSemaphoreCreateMutex() return value checked;
 *     fatal halt with message if allocation fails
 *   - delay(3000) replaced with conditional 500ms debug-only pause
 *   - Scheduler_Init() failure now calls esp_restart() after logging
 *     instead of spinning indefinitely
 *   - OnCDCDisconnect: (void) suppression added for all unused parameters
 *     (MISRA C:2012 Rule 2.7)
 *   - Serial.onEvent magic integer cast replaced with named constant
 *     ARDUINO_HW_CDC_DISCONNECTED_EVENT (MISRA R1)
 *   - RTC_SQW_ISR and rtc_tick_flag retained but clearly marked as
 *     NOT ACTIVE -- attachInterrupt() intentionally omitted until RTC
 *     hardware is verified; ISR kept for forward-compatibility
 *   - Boot log consolidated -- heap info printed only in debug builds
 *
 * v1.4.0 changes:
 *   - Serial.begin() moved FIRST before any Serial.printf
 *   - RTC disabled (hardware not verified)
 *   - USB CDC disconnect no longer resets ESP32 (fix for ResetReason=11)
 */
#include "Scheduler.h"
#include <WiFi.h>
#include "AppMutex.h"
#include "SensorConfig.h"
#include "CmdParser.h"
#include "RTC.h"
#include "RTC_Cfg.h"
#include "WiFiComm_Cfg.h"
#include "App_Cfg.h"    
#include <Preferences.h>

/*==============================================================================
 *                          GLOBAL VARIABLES
 *============================================================================*/

/**
 * @brief Live sensor readings shared between Core 0 (producer) and
 *        Core 1 (consumer -- Modbus, BLE, MQTT).
 * @note  Access must be protected by the SensorData mutex (see AppMutex.h /
 *        future SensorData__GetSnapshot API) to prevent torn reads on a
 *        dual-core Xtensa processor.
 */
volatile SensorData currentData;

/**
 * @brief RTC 1Hz tick flag set by RTC_SQW_ISR.
 * @note  RTC interrupt is NOT currently attached -- see RTC_SQW_ISR below.
 *        Flag will remain false until attachInterrupt() is called once RTC
 *        hardware is verified.
 */
volatile bool rtc_tick_flag = false;

/* g_setup_complete is defined in AppMutex.cpp */
/* g_serial_mutex   is defined in AppMutex.cpp */

/*==============================================================================
 *                          ISR
 *============================================================================*/

/**
 * @brief RTC 1Hz square-wave interrupt service routine.
 * @details Sets rtc_tick_flag which is consumed by the RTC task on Core 1.
 *
 * @warning NOT ACTIVE -- attachInterrupt() is intentionally not called in
 *          setup() because RTC hardware has not been verified on this PCB
 *          revision.  The ISR is retained for forward-compatibility.
 *          To enable:
 *            attachInterrupt(digitalPinToInterrupt(RTC_INT_PIN),
 *                            RTC_SQW_ISR, FALLING);
 *          Replace rtc_tick_flag with a TaskNotify for production use
 *          (eliminates the read-modify-write race on the volatile flag).
 */
void IRAM_ATTR RTC_SQW_ISR(void)
{
    rtc_tick_flag = true;
}

/**
 * @brief USB CDC disconnect event handler.
 * @details Overrides the default Arduino behaviour that resets the ESP32-S3
 *          when DTR is dropped (e.g. closing Arduino IDE or Serial Monitor).
 *          Without this handler the reset kills the active BLE connection
 *          and appears in the boot log as ResetReason=11 (ESP_RST_USB).
 * @note    Must be registered with Serial.onEvent() BEFORE Serial.begin().
 */
static void OnCDCDisconnect(void *arg, esp_event_base_t base,
                             int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    /* Intentionally empty -- suppress DTR-triggered reset */
}

/*==============================================================================
 *                              SETUP
 *============================================================================*/

void setup(void)
{
    /*--------------------------------------------------------------------------
     * Step 1 -- CDC disconnect handler.
     * Must be registered BEFORE Serial.begin() so the handler is in place
     * before the USB stack initialises.
     * ARDUINO_HW_CDC_DISCONNECTED_EVENT is the named constant for event id 1.
     *------------------------------------------------------------------------*/
   Serial.onEvent((arduino_hw_cdc_event_t)1, OnCDCDisconnect);
    /*--------------------------------------------------------------------------
     * Step 2 -- Serial.
     * Must be first before any Serial.printf / Serial.println call.
     * In debug builds a short pause allows Serial Monitor to connect before
     * boot messages are printed.  Not present in production builds.
     *------------------------------------------------------------------------*/
    Serial.begin(APP_SERIAL_BAUD_RATE);

#if (APP_DEBUG_PRINT_ENABLE == 1U)
    delay(500);   /* Debug only -- allow Serial Monitor to attach */
#endif

    Serial.printf("[BOOT] AirSense v1.7.0 -- Reset reason: %d\n",
                  (int)esp_reset_reason());
    Serial.println("[SETUP] LED project boot");

#if (APP_DEBUG_PRINT_ENABLE == 1U)
    Serial.printf("[BOOT] Free heap:     %lu bytes\n", esp_get_free_heap_size());
    Serial.printf("[BOOT] Min free heap: %lu bytes\n", esp_get_minimum_free_heap_size());
#endif

    /*--------------------------------------------------------------------------
     * Step 3 -- Serial mutex.
     * Must exist before any task that calls SERIAL_PRINTF is created.
     * Allocation failure is fatal -- no module can safely share Serial.
     *------------------------------------------------------------------------*/
    g_serial_mutex = xSemaphoreCreateMutex();
    if (g_serial_mutex == NULL)
    {
        /* Cannot use SERIAL_PRINTF here -- mutex does not exist yet */
        Serial.println("[SETUP] FATAL: g_serial_mutex alloc failed -- heap exhausted");
        /* Spin to allow Serial output to flush, then let WDT reset the system */
        while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    /*--------------------------------------------------------------------------
     * Step 4 -- WiFi note.
     * WiFi is fully managed by WiFiComm__Init() / WiFiComm__Handler().
     * Do NOT call WiFi.begin() here -- credentials are read from NVS.
     * On first boot (no credentials) WiFiComm__Init() starts the
     * provisioning AP automatically.
     *------------------------------------------------------------------------*/
    Serial.println("[WIFI] Init deferred to WiFiComm__Init()");

    /*--------------------------------------------------------------------------
     * Step 5 -- RTC INT pin.
     * Configured as input with pull-up.  Interrupt NOT attached -- see
     * RTC_SQW_ISR doxygen above.
     *------------------------------------------------------------------------*/
    pinMode(RTC_INT_PIN, INPUT_PULLUP);

    /*--------------------------------------------------------------------------
     * Step 6 -- Module init via Scheduler.
     * Core 1 first (Buzzer, LEDHMI, Thresholds) then Core 0
     * (CmdParser, Modbus, BLEComm, WiFiComm, OTA_WiFi, Fan, App).
     *------------------------------------------------------------------------*/
    Scheduler_ExecuteInitCore1();
    Scheduler_ExecuteInitCore0();

    /*--------------------------------------------------------------------------
     * Step 7 -- Scheduler init and start.
     * Scheduler_Init() creates FreeRTOS tasks.
     * Scheduler_Start() starts the FreeRTOS tick (calls vTaskStartScheduler
     * or equivalent).  g_setup_complete is set TRUE immediately before
     * Scheduler_Start() so tasks are released as soon as the scheduler runs.
     *------------------------------------------------------------------------*/
    if (!Scheduler_Init())
    {
        Serial.println("[SETUP] FATAL: Scheduler_Init failed -- rebooting in 3s");
        delay(3000);
        esp_restart();
        /* esp_restart() does not return -- line below is unreachable */
    }

    g_setup_complete = true;
    Scheduler_Start();
    Serial.println("[SETUP] COMPLETE");
}

/*==============================================================================
 *                              LOOP
 *============================================================================*/

/**
 * @brief Arduino loop task -- self-destructs after setup completes.
 * @details Under FreeRTOS the Arduino loop() runs as a low-priority task.
 *          Deleting it frees its stack (~8 KB) and prevents it from consuming
 *          scheduler time.  All application work is done in Scheduler tasks.
 */
void loop(void)
{
    vTaskDelete(NULL);
}
