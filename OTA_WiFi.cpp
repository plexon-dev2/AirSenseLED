/**
 * @file OTA_WiFi.cpp
 * @brief HTTP + MQTT OTA for AirSense ESP32-S3
 * @version 2.1.0
 *
 * WHY PREVIOUS DIRECT OTA FAILED:
 *   AirSense (4.14MB) runs from ota_0.
 *   There is no ota_1 -- 8MB flash cannot fit two 4.14MB slots.
 *   esp_ota_begin() refuses to write to the running partition -- CONFLICT.
 *
 * SOLUTION -- Two-phase OTA:
 *   Phase 1 (Python -> AirSense port 8082):
 *     GET /ota_start -> AirSense clears mqttPending, erases otadata, reboots
 *
 *   Phase 2 (Python -> Factory OTA receiver port 8080):
 *     POST /ota -> factory writes new AirSense to ota_0, reboots
 *
 * Python GUI handles both phases transparently -- user clicks once.
 *
 * ENDPOINTS:
 *   GET  /status    -> device info JSON
 *   GET  /ota_start -> reboot to factory OTA receiver
 *   POST /restart   -> soft reboot
 *
 * v2.1.0 changes (review fixes):
 *   - CRITICAL: ~215 lines of commented-out previous version removed (MISRA 2.1)
 *   - CRITICAL: extern WiFiComm__MQTTPublish() inside function bodies removed --
 *     WiFiComm.h already provides the declaration (MISRA 8.5)
 *   - CRITICAL: s_otaTaskRunning guard added to OTA_WiFi__MQTTOTAHandler() --
 *     prevents second MQTT command from spawning a second task while first is
 *     running and overwriting s_ota_url
 *   - CRITICAL: OTA_WiFi__IsUpdating() now returns s_otaInProgress (was always
 *     returning false -- function contract was never satisfied)
 *   - Duplicate #include directives removed (HTTPClient.h, esp_ota_ops.h,
 *     esp_partition.h each appeared twice with mixed "" and <> quoting)
 *   - esp_ota_get_running_partition()->label: null check added before deref
 *   - delay() before esp_restart() replaced with Serial.flush() in HTTP handlers
 *   - String (Arduino heap) in MQTT URL parser replaced with strstr/strchr on
 *     raw const char* -- avoids heap allocation in MQTT callback context
 *   - if (!factory) replaced with if (factory == NULL) (MISRA Rule 11.3)
 *   - All Serial.printf replaced with SERIAL_PRINTF (mutex-safe)
 *   - Missing braces added to single-statement if blocks (MISRA Rule 15.6)
 *   - vTaskDelete(NULL) after ESP.restart() removed (unreachable code)
 *   - OTA_WIFI_VERSION synced to "2.1.0" to match module version
 *   - Non-ASCII encoding artefacts replaced with ASCII in comments
 *   - MQTT subscribe retry: 5-second backoff added to avoid calling
 *     WiFiComm__MQTTSubscribe() 600 times/minute while MQTT is down
 */

/*==============================================================================
 *                              INCLUDES
 *============================================================================*/
#include "OTA_WiFi.h"
#include "WiFiComm.h"
#include "WiFiComm_Cfg.h"
#include "AppMutex.h"       /* SERIAL_PRINTF -- mutex-safe debug output */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Preferences.h>
#include <string.h>

/*==============================================================================
 *                          PRIVATE CONSTANTS
 *============================================================================*/

#define OTA_WIFI_VERSION                "2.1.0"
#define OTA_MQTT_COMMAND_TOPIC          "aqi/" WIFI_MQTT_CLIENT_ID "/ota/command"
#define OTA_MQTT_STATUS_TOPIC           "aqi/" WIFI_MQTT_CLIENT_ID "/ota/status"

/** @brief Minimum interval between MQTT subscribe retries (ms) */
#define OTA_MQTT_SUB_RETRY_INTERVAL_MS  (5000U)

/*==============================================================================
 *                          PRIVATE VARIABLES
 *============================================================================*/

static WebServer  s_server(OTA_WIFI_HTTP_PORT);
static bool       s_serverStarted      = false;
static bool       s_mqttOtaSubbed      = false;
static uint32_t   s_mqttSubLastTryMs   = 0U;

/** @brief Set true before any esp_restart() call -- read by OTA_WiFi__IsUpdating() */
static volatile bool s_otaInProgress   = false;

/**
 * @brief Guard against double-trigger of MQTT OTA task.
 * @details Set true when OTA_WiFi__DownloadTask is spawned.
 *          Cleared only on device reboot (task ends with ESP.restart()).
 */
static volatile bool s_otaTaskRunning  = false;

/** @brief URL buffer for the OTA download task -- static to survive handler return */
static char s_ota_url[512U];

/*==============================================================================
 *                          PRIVATE FUNCTION PROTOTYPES
 *============================================================================*/
static void _handle_status(void);
static void _handle_ota_start(void);
static void _handle_restart(void);
static void OTA_WiFi__DownloadTask(void *pvParameters);

/**
 * @brief Parse a quoted string value for a given key from a minimal JSON payload.
 * @details Locates "key": then extracts the string between the next two quotes.
 *          No heap allocation -- operates directly on the input buffer.
 * @param[in]  json    Null-terminated JSON string.
 * @param[in]  key     Key to search for e.g. "url".
 * @param[out] out     Output buffer.
 * @param[in]  outSize Size of output buffer including null terminator.
 * @return true if key found and value extracted, false otherwise.
 */
static bool OTA_ParseStringField(const char *json, const char *key,
                                  char *out, size_t outSize);

/*==============================================================================
 *                          PRIVATE IMPLEMENTATIONS
 *============================================================================*/

static bool OTA_ParseStringField(const char *json, const char *key,
                                  char *out, size_t outSize)
{
    const char *keyPos = strstr(json, key);
    if (keyPos == NULL) { return false; }

    /* Advance past the key to the colon */
    const char *colon = strchr(keyPos, ':');
    if (colon == NULL) { return false; }

    /* Find first quote after colon */
    const char *q1 = strchr(colon, '"');
    if (q1 == NULL) { return false; }
    q1++;  /* advance past opening quote */

    /* Find closing quote */
    const char *q2 = strchr(q1, '"');
    if (q2 == NULL) { return false; }

    size_t len = (size_t)(q2 - q1);
    if (len == 0U || len >= outSize) { return false; }

    (void)memcpy(out, q1, len);
    out[len] = '\0';
    return true;
}

/* -- GET /status ----------------------------------------------------------- */
static void _handle_status(void)
{
    char buf[256];
    const char *partLabel = "unknown";

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != NULL)
    {
        partLabel = running->label;
    }

    (void)snprintf(buf, sizeof(buf),
        "{\"version\":\"%s\",\"project\":\"AirSense\","
        "\"ip\":\"%s\",\"free_heap\":%lu,"
        "\"uptime_s\":%lu,\"partition\":\"%s\"}",
        OTA_WIFI_VERSION,
        WIFI_STATIC_IP,
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)(millis() / 1000UL),
        partLabel);

    s_server.sendHeader("Access-Control-Allow-Origin", "*");
    s_server.send(200, "application/json", buf);
}

/* -- GET /ota_start --------------------------------------------------------
 * Reboots device into factory partition (OTA receiver firmware).
 * Factory receiver accepts firmware push on port 8080.
 * Python GUI calls this, waits 8s, then pushes to port 8080.
 * ----------------------------------------------------------------------- */
static void _handle_ota_start(void)
{
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_FACTORY,
        NULL);

    if (factory == NULL)
    {
        SERIAL_PRINTF("[OTA_WiFi] ERROR: factory partition not found\n");
        s_server.send(500, "application/json",
                      "{\"error\":\"factory partition not found\"}");
        return;
    }

    SERIAL_PRINTF("[OTA_WiFi] Factory partition: %s at 0x%08lx\n",
                  factory->label, (unsigned long)factory->address);

    /* CRITICAL: Clear any pending MQTT OTA flag before rebooting.
     * Without this, the factory receiver will find mqttPending=true from a
     * previous MQTT OTA and download the old URL, overwriting the firmware
     * we are about to push via WiFi OTA on port 8080. */
    Preferences prefs;
    prefs.begin("otaCfg", false);
    prefs.putBool("mqttPending", false);
    prefs.end();
    SERIAL_PRINTF("[OTA_WiFi] Cleared mqttPending flag (WiFi OTA path)\n");

    /* Erase otadata to force factory boot on next reset */
    const esp_partition_t *otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_OTA,
        NULL);

    if (otadata == NULL)
    {
        s_server.send(500, "application/json", "{\"error\":\"otadata not found\"}");
        return;
    }

    esp_partition_erase_range(otadata, 0, otadata->size);
    SERIAL_PRINTF("[OTA_WiFi] otadata erased -- factory will boot next\n");

    s_server.sendHeader("Access-Control-Allow-Origin", "*");
    s_server.send(200, "application/json",
                  "{\"status\":\"rebooting\","
                  "\"message\":\"Switching to OTA receiver on port 8080\","
                  "\"ota_port\":8080}");

    /* Flush Serial before restart to ensure log message is fully transmitted.
     * Serial.flush() is preferred over delay() -- deterministic. */
    Serial.flush();

    s_otaInProgress = true;
    esp_restart();
}

/* -- POST /restart --------------------------------------------------------- */
static void _handle_restart(void)
{
    s_server.send(200, "application/json", "{\"status\":\"restarting\"}");
    Serial.flush();
    s_otaInProgress = true;
    esp_restart();
}

/* -- MQTT OTA download task ------------------------------------------------
 * Runs on Core 0, 24KB stack. Saves URL to NVS, sets factory boot, reboots.
 * ----------------------------------------------------------------------- */
static void OTA_WiFi__DownloadTask(void *pvParameters)
{
    (void)pvParameters;

    SERIAL_PRINTF("[OTA_WiFi] MQTT OTA: saving URL to NVS and rebooting to factory\n");
    SERIAL_PRINTF("[OTA_WiFi]   URL: %s\n", s_ota_url);

    /* Save URL to NVS so factory receiver can read and download it */
    Preferences prefs;
    prefs.begin("otaCfg", false);
    prefs.putString("mqttUrl",     s_ota_url);
    prefs.putBool("mqttPending",   true);
    prefs.end();

    SERIAL_PRINTF("[OTA_WiFi] URL saved to NVS\n");

    WiFiComm__MQTTPublish(OTA_MQTT_STATUS_TOPIC,
        "{\"status\":\"rebooting_to_factory\",\"source\":\"mqtt\"}");

    /* Set factory partition as next boot target */
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_FACTORY,
        NULL);

    if (factory != NULL)
    {
        esp_ota_set_boot_partition(factory);
        SERIAL_PRINTF("[OTA_WiFi] Boot target -> factory OK\n");
    }
    else
    {
        SERIAL_PRINTF("[OTA_WiFi] ERROR: factory partition not found\n");
        WiFiComm__MQTTPublish(OTA_MQTT_STATUS_TOPIC,
            "{\"status\":\"failed\",\"source\":\"mqtt\"}");
        s_otaTaskRunning = false;
        vTaskDelete(NULL);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(1000U));   /* allow MQTT status to transmit */

    s_otaInProgress = true;
    ESP.restart();
    /* Note: ESP.restart() never returns. vTaskDelete() is omitted -- unreachable. */
}

/*==============================================================================
 *                          PUBLIC API
 *============================================================================*/

void OTA_WiFi__Init(void)
{
    s_server.on("/status",    HTTP_GET,  _handle_status);
    s_server.on("/ota_start", HTTP_GET,  _handle_ota_start);
    s_server.on("/restart",   HTTP_POST, _handle_restart);

    memset(s_ota_url, 0, sizeof(s_ota_url));

    SERIAL_PRINTF("[OTA_WiFi] Ready -- http://%s:%d\n",
                  WIFI_STATIC_IP, (int)OTA_WIFI_HTTP_PORT);
    SERIAL_PRINTF("[OTA_WiFi] To update: GET /ota_start -> push to port 8080\n");
}

void OTA_WiFi__Handler(void)
{
    if (!s_serverStarted && (WiFi.status() == WL_CONNECTED))
    {
        s_server.begin();
        s_serverStarted = true;
        SERIAL_PRINTF("[OTA_WiFi] HTTP server started port %d\n",
                      (int)OTA_WIFI_HTTP_PORT);
        if (MDNS.begin("airsense"))
        {
            MDNS.addService("http", "tcp", OTA_WIFI_HTTP_PORT);
            SERIAL_PRINTF("[OTA_WiFi] mDNS started -- airsense.local\n");
        }
    }

    if (s_serverStarted)
    {
        s_server.handleClient();
    }

    /* Subscribe to MQTT OTA command topic once WiFi is up.
     * Retries every OTA_MQTT_SUB_RETRY_INTERVAL_MS until MQTT connect succeeds.
     * Previous version retried every 100ms (600x/min) -- throttled here. */
    if (!s_mqttOtaSubbed && WiFiComm__IsWiFiConnected())
    {
        uint32_t now = millis();
        if ((now - s_mqttSubLastTryMs) >= OTA_MQTT_SUB_RETRY_INTERVAL_MS)
        {
            s_mqttSubLastTryMs = now;
            WiFiComm_Status subResult =
                WiFiComm__MQTTSubscribe(OTA_WiFi__GetMQTTCommandTopic());
            if (subResult == WIFI_STATUS_OK)
            {
                s_mqttOtaSubbed = true;
                SERIAL_PRINTF("[OTA_WiFi] Subscribed to MQTT OTA topic: %s\n",
                              OTA_WiFi__GetMQTTCommandTopic());
            }
        }
    }
}

bool OTA_WiFi__IsUpdating(void)
{
    return (bool)s_otaInProgress;
}

void OTA_WiFi__MQTTOTAHandler(const char *topic,
                                const char *payload,
                                uint16_t    length)
{
    (void)topic;
    (void)length;

    if (payload == NULL) { return; }

    SERIAL_PRINTF("[OTA_WiFi] MQTT OTA command received: %s\n", payload);

    /* Guard against double-trigger -- second command while task is running */
    if (s_otaTaskRunning)
    {
        SERIAL_PRINTF("[OTA_WiFi] MQTT OTA: already in progress -- ignoring\n");
        return;
    }

    /* Parse URL from JSON payload -- no heap allocation (replaces Arduino String) */
    char url[512U];
    if (!OTA_ParseStringField(payload, "\"url\"", url, sizeof(url)))
    {
        SERIAL_PRINTF("[OTA_WiFi] MQTT OTA: no url field or malformed payload\n");
        return;
    }

    if (url[0] == '\0')
    {
        SERIAL_PRINTF("[OTA_WiFi] MQTT OTA: empty URL -- ignored\n");
        return;
    }

    /* Copy URL to module-level static buffer before spawning task */
    strncpy(s_ota_url, url, sizeof(s_ota_url) - 1U);
    s_ota_url[sizeof(s_ota_url) - 1U] = '\0';

    SERIAL_PRINTF("[OTA_WiFi] MQTT OTA: spawning download task for:\n  %s\n",
                  s_ota_url);

    WiFiComm__MQTTPublish(OTA_MQTT_STATUS_TOPIC,
        "{\"status\":\"downloading\",\"source\":\"mqtt\"}");

    /* Spawn dedicated task with 24KB stack -- sufficient for NVS + esp_ota ops */
    s_otaTaskRunning = true;
    BaseType_t ret = xTaskCreatePinnedToCore(
        OTA_WiFi__DownloadTask,
        "mqtt_ota",
        24576U,
        NULL,
        1U,
        NULL,
        0
    );

    if (ret != pdPASS)
    {
        SERIAL_PRINTF("[OTA_WiFi] MQTT OTA: failed to create download task\n");
        WiFiComm__MQTTPublish(OTA_MQTT_STATUS_TOPIC,
            "{\"status\":\"failed\",\"source\":\"mqtt\"}");
        s_otaTaskRunning = false;
    }
}

const char* OTA_WiFi__GetMQTTCommandTopic(void)
{
    return OTA_MQTT_COMMAND_TOPIC;
}