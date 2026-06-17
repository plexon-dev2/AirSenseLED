/**
 * @file OTA_WiFi.h
 * @brief HTTP + MQTT OTA for AirSense
 * @version 2.0.0 — added MQTT OTA (remote, any location)
 */

#ifndef OTA_WIFI_H
#define OTA_WIFI_H

#include <stdint.h>
#include <stdbool.h>

/** @brief Port for local HTTP OTA server */
#define OTA_WIFI_HTTP_PORT      (8082U)

/** @brief Local HTTP OTA init — call once from setup() after WiFi init */
void OTA_WiFi__Init(void);

/** @brief Local HTTP OTA periodic handler — call every 100ms */
void OTA_WiFi__Handler(void);

/** @brief Returns true if OTA update is currently in progress */
bool OTA_WiFi__IsUpdating(void);

/**
 * @brief MQTT OTA command handler
 * @details Register this as the MQTT RX callback for the OTA command topic.
 *          Payload: {"url":"https://...firmware.bin","version":"1.5.0"}
 */
void OTA_WiFi__MQTTOTAHandler(const char *topic,
                                const char *payload,
                                uint16_t    length);

/**
 * @brief Get the MQTT OTA command topic string
 * @return Topic string e.g. "aqi/ESP32_AQI_001/ota/command"
 */
const char* OTA_WiFi__GetMQTTCommandTopic(void);

#endif /* OTA_WIFI_H */