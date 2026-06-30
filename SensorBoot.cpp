/**
 * @file SensorBoot.cpp
 * @brief Sensor Bootup Time Measurement -- AirSense ESP32-S3
 *
 * How it works:
 *   1. Drive HW_PIN_SENSOR_POWER HIGH (sensor power ON)
 *   2. Call CmdParser__Init() to open sensor UART
 *   3. Poll CmdParser every CFG_BOOT_POLL_INTERVAL_MS
 *   4. First valid CMDPARSER_STATUS_OK response -> record elapsed time
 *   5. If CFG_BOOT_SENSOR_TIMEOUT_MS elapses with no response -> record timeout
 *   6. Log result to Serial
 *   7. SensorBoot__PublishResult() sends via MQTT + BLE (call after comms init)
 *
 * @version 1.1.0
 *
 * v1.1.0 changes (review fixes):
 *   - s_published reset to false at start of SensorBoot__MeasureAndWait() --
 *     was not reset on warm reset (OTA reboot), causing SensorBoot__PublishResult()
 *     to silently skip publishing the new boot time after a warm reset.
 *   - Serial.println / Serial.printf replaced with SERIAL_PRINTF (mutex-safe).
 *     Calls in MeasureAndWait() run before the scheduler so are safe either way,
 *     but SERIAL_PRINTF is consistent with the rest of the project.
 *   - delay() documented as intentional: called before FreeRTOS scheduler starts
 *     so vTaskDelay() is not available here.
 *   - MQTT retain flag (true) documented as intentional -- new subscribers
 *     receive the last boot time immediately on connect.
 */

#include "SensorBoot.h"
#include "ProjectConfig.h"
#include "CmdParser.h"
#include "WiFiComm.h"
#include "BLEComm.h"
#include "HardwareConfig.h"
#include "AppMutex.h"       /* SERIAL_PRINTF */

#include <Arduino.h>
#include <stdio.h>

/*==============================================================================
 *                          PRIVATE VARIABLES
 *============================================================================*/

static uint32_t s_boot_time_ms = 0UL;
static bool     s_sensor_ready = false;
static bool     s_published    = false;
static char     s_result_json[64U];

/*==============================================================================
 *                          PUBLIC FUNCTIONS
 *============================================================================*/

void SensorBoot__MeasureAndWait(void)
{
    /* Reset one-shot publish flag so a warm reset (OTA reboot) correctly
     * re-publishes the new boot time on the next MQTT connect.           */
    s_published    = false;
    s_sensor_ready = false;
    s_boot_time_ms = 0UL;

    /* Power sensor ON */
    pinMode(HW_PIN_SENSOR_POWER, OUTPUT);
    digitalWrite(HW_PIN_SENSOR_POWER, HIGH);

    SERIAL_PRINTF("[SBOOT] Sensor power ON -- waiting for first valid response...\n");

    uint32_t t_start = millis();
    uint32_t elapsed = 0UL;

    /* Initialise CmdParser so we can poll it.
     * Do NOT call CmdParser__Init() separately in the scheduler init sequence --
     * it is already initialised when this function returns.               */
    CmdParser__Init();

    while (elapsed < CFG_BOOT_SENSOR_TIMEOUT_MS)
    {
        /* Drive the parser to accumulate incoming UART bytes */
        CmdParser__Handler();

        CmdParser_ZPHS01B_Data_t data;
        CmdParser_StatusType_t status = CmdParser__GetAirQualityData(&data);

        if (status == CMDPARSER_STATUS_OK)
        {
            elapsed        = millis() - t_start;
            s_boot_time_ms = elapsed;
            s_sensor_ready = true;

            SERIAL_PRINTF("[SBOOT] Sensor ready in %lums\n", (unsigned long)elapsed);
            break;
        }

        /* delay() is correct here -- FreeRTOS scheduler has not started yet.
         * vTaskDelay() must not be called before vTaskStartScheduler().   */
        delay(CFG_BOOT_POLL_INTERVAL_MS);
        elapsed = millis() - t_start;
    }

    if (!s_sensor_ready)
    {
        s_boot_time_ms = CFG_BOOT_SENSOR_TIMEOUT_MS;
        SERIAL_PRINTF("[SBOOT] *** Sensor did not respond within %lums -- timeout ***\n",
                      (unsigned long)CFG_BOOT_SENSOR_TIMEOUT_MS);
    }

    /* Build result JSON now -- comms modules not ready yet, publish later */
    (void)snprintf(s_result_json, sizeof(s_result_json),
                   "{\"boot_ms\":%lu,\"ready\":%d}",
                   (unsigned long)s_boot_time_ms,
                   (int)s_sensor_ready);
}

void SensorBoot__PublishResult(void)
{
    if (s_published) { return; }  /* one-shot per boot cycle */

#if (CFG_BOOT_MQTT_REPORT_ENABLE == 1U)
    /* retain=true is intentional: new MQTT subscribers receive the last boot
     * time immediately on connect, without waiting for the next measurement. */
    WiFiComm__MQTTPublishEx(CFG_MQTT_TOPIC_BOOTTIME, s_result_json, 1, true);
    SERIAL_PRINTF("[SBOOT] Published to MQTT: %s\n", s_result_json);
#endif

#if (CFG_BOOT_BLE_REPORT_ENABLE == 1U)
    BLEComm__SendSensorData(s_result_json);
#endif

    s_published = true;
}

uint32_t SensorBoot__GetBootTimeMS(void) { return s_boot_time_ms; }
bool     SensorBoot__SensorReady(void)   { return s_sensor_ready; }