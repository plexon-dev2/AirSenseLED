/**
 * @file AirSense_LEDBased_1V4.ino
 * @brief Air Quality Sensor -- LED Project entry point
 * @version 1.4.0
 *
 * FIXES:
 *   1. Serial.begin() moved FIRST before any Serial.printf
 *   2. Direct WiFi.begin() added before scheduler init
 *      -- WiFiComm module picks up the existing connection
 *   3. RTC disabled (hardware not verified)
 *   4. USB CDC disconnect no longer resets ESP32 (fix for ResetReason=11)
 *      -- Closing Arduino IDE / Serial Monitor no longer kills BLE connection
 */

#include <WiFi.h>
#include "Scheduler.h"
#include "AppMutex.h"
#include "SensorConfig.h"
#include "CmdParser.h"
#include "RTC.h"
#include "RTC_Cfg.h"
#include "WiFiComm_Cfg.h"
#include <Preferences.h>

/*==============================================================================
 *                          GLOBAL VARIABLES
 *============================================================================*/

volatile SensorData currentData;              /* Live sensor data -- shared across cores */
volatile bool       g_setup_complete = false; /* Tasks wait on this flag                 */
volatile bool       rtc_tick_flag    = false; /* RTC 1Hz ISR flag -- Core 1 consumes     */

/*==============================================================================
 *                          ISR
 *============================================================================*/

void IRAM_ATTR RTC_SQW_ISR(void)
{
    rtc_tick_flag = true;
}

/**
 * @brief USB CDC disconnect handler — suppresses ESP_RST_USB (ResetReason=11)
 * @details Closing Arduino IDE drops DTR → ESP32-S3 resets by default → kills BLE.
 *          This empty handler overrides the reset. Serial logs unaffected.
 */
static void OnCDCDisconnect(void *arg, esp_event_base_t base,
                             int32_t id, void *data)
{
    /* Intentionally empty — suppress DTR-triggered reset */
}

/*==============================================================================
 *                              SETUP
 *============================================================================*/
void setup()
{
    /* Register CDC disconnect handler BEFORE Serial.begin()
     * Cast to arduino_hw_cdc_event_t — enum value 1 = disconnected */
    Serial.onEvent((arduino_hw_cdc_event_t)1, OnCDCDisconnect);

    /* Serial MUST be first before any printf */
    Serial.begin(115200);
    delay(3000);
    Serial.printf("[BOOT] Reset reason: %d\n", (int)esp_reset_reason());
    Serial.println("[SETUP] LED project boot");
    Serial.printf("[BOOT] Free heap: %lu bytes\n", esp_get_free_heap_size());
    Serial.printf("[BOOT] Min free heap: %lu bytes\n", esp_get_minimum_free_heap_size());

    /* Serial mutex must exist before any SERIAL_PRINTF call */
    g_serial_mutex = xSemaphoreCreateMutex();

   /* WiFi is fully managed by WiFiComm__Init() / WiFiComm__Handler().
 * Do NOT call WiFi.begin() here — credentials are read from NVS.
 * On first boot (no credentials) WiFiComm__Init() starts the
 * provisioning AP automatically.                                  */
Serial.println("[WIFI] Init deferred to WiFiComm__Init()");

    /* Core 1 init: Buzzer, LEDHMI, Thresholds */
    Scheduler_ExecuteInitCore1();

    /* RTC INT pin -- input pullup (interrupt not attached) */
    pinMode(RTC_INT_PIN, INPUT_PULLUP);

    /* Core 0 init: CmdParser, Modbus, BLEComm, WiFiComm, OTA_WiFi, Fan, App */
    Scheduler_ExecuteInitCore0();

    if (!Scheduler_Init())
    {
        Serial.println("[SETUP] ERROR: Scheduler_Init failed");
        while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    g_setup_complete = true;
    Scheduler_Start();
    Serial.println("[SETUP] COMPLETE");
}

/*==============================================================================
 *                              LOOP
 *============================================================================*/
void loop()
{
    vTaskDelete(NULL);
}
