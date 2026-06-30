/**
 * @file OTA_WiFi.h
 * @brief HTTP + MQTT OTA for AirSense ESP32-S3
 * @version 2.1.0
 *
 * Two-phase OTA design (required by partition layout):
 *   AirSense (4.14MB) runs from ota_0. No ota_1 slot exists on 8MB flash.
 *   esp_ota_begin() refuses to write to the running partition.
 *
 *   Phase 1 (Python -> AirSense port 8082):
 *     GET /ota_start -> AirSense saves mqttPending=false, erases otadata,
 *                       sends HTTP 200, reboots to factory OTA receiver.
 *
 *   Phase 2 (Python -> Factory OTA receiver port 8080):
 *     POST /ota -> factory writes new AirSense to ota_0, reboots.
 *
 *   MQTT OTA path:
 *     Payload {"url":"https://..."} -> save URL + mqttPending=true to NVS,
 *     set factory boot, reboot. Factory sees mqttPending=true, downloads URL.
 *
 * v2.1.0 changes (review fixes):
 *   - OTA_WiFi__IsUpdating(): now returns true during active OTA reboot phase
 *     (was always returning false -- function contract was never satisfied)
 *   - Header: encoding artefacts removed from comments
 */

#ifndef OTA_WIFI_H
#define OTA_WIFI_H

#include <stdint.h>
#include <stdbool.h>

/** @brief Port for local HTTP OTA server */
#define OTA_WIFI_HTTP_PORT      (8082U)

/** @brief Local HTTP OTA init -- call once from setup() after WiFi init */
void OTA_WiFi__Init(void);

/** @brief Local HTTP OTA periodic handler -- call every 100ms */
void OTA_WiFi__Handler(void);

/**
 * @brief Returns true if an OTA reboot sequence is in progress.
 * @details Set to true just before esp_restart() is called in any OTA path.
 *          Callers can check this to suppress non-essential activity
 *          (LED updates, MQTT publish, sensor reads) during the reboot window.
 */
bool OTA_WiFi__IsUpdating(void);

/**
 * @brief MQTT OTA command handler.
 * @details Register as the MQTT RX callback for the OTA command topic.
 *          Payload: {"url":"https://...firmware.bin","version":"1.5.0"}
 *          Spawns a FreeRTOS task to save URL to NVS and reboot to factory.
 *          Safe to call multiple times -- guarded against double-trigger.
 */
void OTA_WiFi__MQTTOTAHandler(const char *topic,
                                const char *payload,
                                uint16_t    length);

/**
 * @brief Get the MQTT OTA command topic string.
 * @return Topic string e.g. "aqi/ESP32_AQI_001/ota/command"
 */
const char* OTA_WiFi__GetMQTTCommandTopic(void);

#endif /* OTA_WIFI_H */