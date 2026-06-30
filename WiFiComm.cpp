/**
 * @file WiFiComm.cpp
 * @brief Generic WiFi Communication Module Implementation
 * @details Implements TCP Client, TCP Server, and MQTT communication
 *          over WiFi for ESP32-S3. Fully independent and reusable module.
 *
 *          TCP Client : ESP32 connects to PC (port 8081)
 *          TCP Server : PC connects to ESP32 (port 8080)
 *          MQTT       : Disabled by default -- enable in cfg
 *
 * @date 2025-12-19
 * @version 1.1.0
 *
 * v1.1.0 changes (review fixes):
 *   - WIFI_DBG_PRINTF / WIFI_DBG_PRINTLN: now route through SERIAL_PRINTF
 *     (mutex-safe). Raw Serial.printf from Task_WiFi causes Modbus RX
 *     corruption on Core 0 when debug is enabled.
 *   - WiFiComm__HandleMQTT(): hardcoded topic literals removed
 *     ("aqi/rtc/set", "aqi/ESP32_AQI_001/ota/command"). These break the
 *     generic-module contract. App__Init() registers subscriptions via
 *     WiFiComm__MQTTSubscribe() and the App_DisplayData() MQTT-connect edge
 *     detection triggers re-subscription on reconnect.
 *   - WiFiComm__ClearCredentials(): implemented (was declared in header but
 *     missing from .cpp -- linker error if called).
 *   - WiFiComm__OnWiFiDisconnected(): called from WIFI_SM_CONNECTED on loss
 *     (was defined but never called -- dead function).
 *   - strncpy null-terminator explicitly set after all credential copies.
 *   - Encoding artefacts (--) replaced with -- in comments.
 *   - WiFiComm__MQTTPublishEx() qos parameter documented as not forwarded
 *     to PubSubClient (library only supports QoS 0 publish).
 */

/*==============================================================================
 *                              INCLUDES
 *============================================================================*/
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <esp_wifi.h>
#include "WiFiComm.h"
#include "WiFiComm_Cfg.h"
#include "AQ_LEDHMI.h"
#include "AppMutex.h"       /* SERIAL_PRINTF -- mutex-safe debug output */
#include "esp_task_wdt.h"

/*==============================================================================
 *                          DEBUG PRINT CONTROL
 *  Routes through SERIAL_PRINTF (mutex-safe on dual-core).
 *  Raw Serial.printf from Task_WiFi (Core 0) during Modbus RX causes
 *  byte corruption. Keep WIFICOMM_DEBUG_ENABLE 0 in production.
 *============================================================================*/
#define WIFICOMM_DEBUG_ENABLE  (0U)

#if (WIFICOMM_DEBUG_ENABLE == 1U)
    /**
     * @note MISRA C:2012 Rule 20.10 advisory deviation: variadic macro.
     *       Rationale: no compliant alternative; isolated to this file.
     */
    #define WIFI_DBG_PRINTF(...)   SERIAL_PRINTF(__VA_ARGS__)
    #define WIFI_DBG_PRINTLN(x)    SERIAL_PRINTF("%s\n", (x))
#else
    #define WIFI_DBG_PRINTF(...)
    #define WIFI_DBG_PRINTLN(x)
#endif

#if (WIFI_MQTT_ENABLED == 1U)
#include <PubSubClient.h>
#include <Preferences.h>
#endif

/*==============================================================================
 *                              PRIVATE TYPES
 *============================================================================*/

/**
 * @brief WiFi state machine states
 */
typedef enum
{
    WIFI_SM_IDLE        = 0U, /**< Not started                    */
    WIFI_SM_CONNECTING  = 1U, /**< WiFi connect in progress       */
    WIFI_SM_CONNECTED   = 2U, /**< WiFi connected                 */
    WIFI_SM_RECONNECTING= 3U  /**< WiFi lost -- reconnecting       */
} WiFiSM_State;

/**
 * @brief TCP Client state machine states
 */
typedef enum
{
    TCP_CLIENT_SM_IDLE        = 0U, /**< Not started               */
    TCP_CLIENT_SM_CONNECTING  = 1U, /**< Connecting to remote host */
    TCP_CLIENT_SM_CONNECTED   = 2U, /**< Connected and ready       */
    TCP_CLIENT_SM_RECONNECTING= 3U  /**< Lost -- reconnecting       */
} TCPClientSM_State;

/*==============================================================================
 *                          PRIVATE VARIABLES
 *============================================================================*/

/* WiFi state machine */
static char             s_activeSSID[64U]     = {0U};
static char             s_activePass[64U]     = {0U};
static WiFiSM_State     s_wifiSMState         = WIFI_SM_IDLE;
static uint32_t         s_wifiConnectStartTime = 0U;
static uint32_t         s_wifiReconnectTimer   = 0U;
static bool             s_wifiErrorReported    = false;

/* TCP Client */
#if (WIFI_TCP_CLIENT_ENABLED == 1U)
static WiFiClient       s_tcpClient;
static TCPClientSM_State s_tcpClientSMState   = TCP_CLIENT_SM_IDLE;
static uint32_t         s_tcpClientReconnTimer = 0U;
static char             s_tcpClientRxBuf[WIFI_RX_BUFFER_SIZE];
#endif

/* TCP Server */
#if (WIFI_TCP_SERVER_ENABLED == 1U)
static WiFiServer       s_tcpServer(WIFI_TCP_SERVER_PORT);
static WiFiClient       s_tcpServerClient;
static bool             s_tcpServerStarted    = false;
static char             s_tcpServerRxBuf[WIFI_RX_BUFFER_SIZE];
#endif

/* MQTT */
#if (WIFI_MQTT_ENABLED == 1U)
static WiFiClient       s_mqttWifiClient;
static PubSubClient     s_mqttClient(s_mqttWifiClient);
static uint32_t         s_mqttReconnTimer     = 0U;
static char             s_mqttRxTopic[WIFI_MQTT_TOPIC_MAX_LEN];
static char             s_mqttRxPayload[WIFI_PAYLOAD_MAX_LEN];
#endif

/* RX Callbacks */
static WiFiComm_RxCallback s_tcpClientRxCb   = NULL;
static WiFiComm_RxCallback s_tcpServerRxCb   = NULL;
static WiFiComm_RxCallback s_mqttRxCb        = NULL;

/* IP address buffer */
static char             s_ipAddressStr[16]   = "0.0.0.0";

/*==============================================================================
 *                          PRIVATE FUNCTION PROTOTYPES
 *============================================================================*/
static void WiFiComm__HandleWiFiSM(uint32_t currentTimeMs);
static void WiFiComm__HandleTCPClient(uint32_t currentTimeMs);
static void WiFiComm__HandleTCPServer(void);
static void WiFiComm__HandleMQTT(uint32_t currentTimeMs);
static void WiFiComm__OnWiFiConnected(void);
static void WiFiComm__OnWiFiDisconnected(void);

#if (WIFI_MQTT_ENABLED == 1U)
static void WiFiComm__MQTTInternalCallback(char          *topic,
                                            byte          *payload,
                                            unsigned int   length);
#endif

/*==============================================================================
 *                          PUBLIC FUNCTION IMPLEMENTATIONS
 *============================================================================*/

/**
 * @brief Initialize WiFi communication module
 * @details Connects to WiFi, starts TCP server if enabled,
 *          initializes MQTT client if enabled.
 *          Call once from setup().
 */
void WiFiComm__Init(void)
{
    /* FIX: extend WDT to 15s for MQTT TCP connect (IDF v5.x struct API) */
    {
        const esp_task_wdt_config_t wdt_cfg = { 15000U, (1 << 0), true };
        esp_task_wdt_reconfigure(&wdt_cfg);
    }
    WIFI_DBG_PRINTLN("\n[WiFiComm] ========== INITIALIZING ==========");
    WIFI_DBG_PRINTF("[WiFiComm] SSID        : %s\n", WIFI_SSID);
    WIFI_DBG_PRINTF("[WiFiComm] TCP Client  : %s (port %d) [%s]\n",
                  WIFI_TCP_CLIENT_SERVER_IP,
                  (int)WIFI_TCP_CLIENT_PORT,
                  (WIFI_TCP_CLIENT_ENABLED == 1U) ? "ENABLED" : "DISABLED");
    WIFI_DBG_PRINTF("[WiFiComm] TCP Server  : port %d [%s]\n",
                  (int)WIFI_TCP_SERVER_PORT,
                  (WIFI_TCP_SERVER_ENABLED == 1U) ? "ENABLED" : "DISABLED");
    WIFI_DBG_PRINTF("[WiFiComm] MQTT        : %s:%d [%s]\n",
                  WIFI_MQTT_BROKER_IP,
                  (int)WIFI_MQTT_BROKER_PORT,
                  (WIFI_MQTT_ENABLED == 1U) ? "ENABLED" : "DISABLED");

    /* Start WiFi connection */
    WiFi.mode(WIFI_STA);

    #if (WIFI_STATIC_IP_ENABLED == 1U)
{
    IPAddress staticIP, gateway, subnet, dns1, dns2;
    staticIP.fromString(WIFI_STATIC_IP);
    gateway.fromString(WIFI_STATIC_GATEWAY);
    subnet.fromString(WIFI_STATIC_SUBNET);
    dns1.fromString(WIFI_STATIC_DNS1);
    dns2.fromString(WIFI_STATIC_DNS2);
    WiFi.config(staticIP, gateway, subnet, dns1, dns2);
    WIFI_DBG_PRINTF("[WiFiComm] Static IP: %s\n", WIFI_STATIC_IP);
}
#endif

    /* BLE + WiFi coexistence -- critical when both radios run simultaneously.
     * WIFI_PS_MIN_MODEM allows modem sleep between DTIM beacons -- reduces
     * radio contention with BLE without dropping the connection.
     * Without this, ESP32 WiFi drops repeatedly when BLE is active.      */
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    /* Increase TX power to maximum for stable connection */
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    /* If WiFi already connected (by .ino direct connect), skip WiFi.begin
     * and go straight to CONNECTED state                                  */
    if (WiFi.status() == WL_CONNECTED)
    {
        s_wifiSMState       = WIFI_SM_CONNECTED;
        s_wifiErrorReported = false;
        WIFI_DBG_PRINTF("[WiFiComm] WiFi already connected — IP: %s\n",
                        WiFi.localIP().toString().c_str());
        WiFiComm__OnWiFiConnected();
    }
    else
    {
        /* NVS-first: use saved credentials if available, else hardcoded */
        strncpy(s_activeSSID, WIFI_SSID,    sizeof(s_activeSSID) - 1U);
        s_activeSSID[sizeof(s_activeSSID) - 1U] = '\0';
        strncpy(s_activePass, WIFI_PASSWORD, sizeof(s_activePass) - 1U);
        s_activePass[sizeof(s_activePass) - 1U] = '\0';
        {
            Preferences prefs;
            if (prefs.begin("wifi_creds", true))
            {
                String nvsSsid = prefs.getString("ssid",     "");
                String nvsPass = prefs.getString("password", "");
                prefs.end();
                if (nvsSsid.length() > 0U)
                {
                    strncpy(s_activeSSID, nvsSsid.c_str(), sizeof(s_activeSSID) - 1U);
                    s_activeSSID[sizeof(s_activeSSID) - 1U] = '\0';
                    strncpy(s_activePass, nvsPass.c_str(), sizeof(s_activePass) - 1U);
                    s_activePass[sizeof(s_activePass) - 1U] = '\0';
                    WIFI_DBG_PRINTF("[WiFiComm] NVS credentials found: %s\n", s_activeSSID);
                }
            }
        }
        WiFi.begin(s_activeSSID, s_activePass);
        s_wifiSMState          = WIFI_SM_CONNECTING;
        s_wifiConnectStartTime = millis();
        s_wifiErrorReported    = false;
        WIFI_DBG_PRINTLN("[WiFiComm] Connecting to WiFi...");
    }

#if (WIFI_MQTT_ENABLED == 1U)
    /* Configure MQTT broker */
    s_mqttClient.setServer(WIFI_MQTT_BROKER_IP, WIFI_MQTT_BROKER_PORT);
    s_mqttClient.setCallback(WiFiComm__MQTTInternalCallback);
    s_mqttClient.setKeepAlive(WIFI_MQTT_KEEPALIVE_SEC);
    WIFI_DBG_PRINTF("[WiFiComm] MQTT broker : %s:%d\n",
                  WIFI_MQTT_BROKER_IP, (int)WIFI_MQTT_BROKER_PORT);
#endif

    WIFI_DBG_PRINTLN("[WiFiComm] ========== INIT COMPLETE ==========\n");
}

/**
 * @brief WiFi communication periodic handler
 * @details Call every WIFI_HANDLER_INTERVAL_MS (100ms) from loop()
 */
void WiFiComm__Handler(void)
{
    uint32_t currentTime = millis();

    /* Always handle WiFi state machine first */
    WiFiComm__HandleWiFiSM(currentTime);

    /* Only process TCP/MQTT when WiFi is connected */
    if (s_wifiSMState == WIFI_SM_CONNECTED)
    {
#if (WIFI_TCP_CLIENT_ENABLED == 1U)
        WiFiComm__HandleTCPClient(currentTime);
#endif

#if (WIFI_TCP_SERVER_ENABLED == 1U)
        WiFiComm__HandleTCPServer();
#endif

#if (WIFI_MQTT_ENABLED == 1U)
        WiFiComm__HandleMQTT(currentTime);
#endif
    }
}

/*------------------------------------------------------------------------------------------------------------------------------------------------------------
 *  Callback Registration
 *--------------------------------------------------------------------------------------------------------------------------------------------------------*/


void WiFiComm__RegisterTCPClientRxCallback(WiFiComm_RxCallback callback)
{
    s_tcpClientRxCb = callback;
}

/**
 * @brief Register RX callback for TCP Server channel
 */
void WiFiComm__RegisterTCPServerRxCallback(WiFiComm_RxCallback callback)
{
    s_tcpServerRxCb = callback;
}

/**
 * @brief Register RX callback for MQTT channel
 */
void WiFiComm__RegisterMQTTRxCallback(WiFiComm_RxCallback callback)
{
    s_mqttRxCb = callback;
}

/*------------------------------------------------------------------------------------------------------------------------------------------------------------
 *  TCP Client API
 *--------------------------------------------------------------------------------------------------------------------------------------------------------*/

/**
 * @brief Send data via TCP Client channel
 */
WiFiComm_Status WiFiComm__TCPClientSend(const char *payload)
{
#if (WIFI_TCP_CLIENT_ENABLED == 1U)
    if (payload == NULL)
    {
        return WIFI_STATUS_ERROR;
    }
    if (s_tcpClientSMState != TCP_CLIENT_SM_CONNECTED)
    {
        return WIFI_STATUS_NO_CONN;
    }

    size_t len = strlen(payload);
    if (s_tcpClient.write((const uint8_t*)payload, len) == len)
    {
        WIFI_DBG_PRINTF("[WiFiComm] TCP-C TX: %s\n", payload);
        return WIFI_STATUS_OK;
    }
    return WIFI_STATUS_ERROR;
#else
    (void)payload;
    return WIFI_STATUS_ERROR;
#endif
}

/**
 * @brief Send raw bytes via TCP Client channel
 */
WiFiComm_Status WiFiComm__TCPClientSendRaw(const uint8_t *data,
                                            uint16_t       length)
{
#if (WIFI_TCP_CLIENT_ENABLED == 1U)
    if ((data == NULL) || (length == 0U))
    {
        return WIFI_STATUS_ERROR;
    }
    if (s_tcpClientSMState != TCP_CLIENT_SM_CONNECTED)
    {
        return WIFI_STATUS_NO_CONN;
    }

    if (s_tcpClient.write(data, length) == length)
    {
        return WIFI_STATUS_OK;
    }
    return WIFI_STATUS_ERROR;
#else
    (void)data;
    (void)length;
    return WIFI_STATUS_ERROR;
#endif
}

/**
 * @brief Get TCP Client connection state
 */
WiFiComm_TCPState WiFiComm__GetTCPClientState(void)
{
#if (WIFI_TCP_CLIENT_ENABLED == 1U)
    switch (s_tcpClientSMState)
    {
        case TCP_CLIENT_SM_CONNECTED:   return WIFI_TCP_CONNECTED;
        case TCP_CLIENT_SM_CONNECTING:  return WIFI_TCP_CONNECTING;
        default:                        return WIFI_TCP_DISCONNECTED;
    }
#else
    return WIFI_TCP_DISCONNECTED;
#endif
}

/*------------------------------------------------------------------------------------------------------------------------------------------------------------
 *  TCP Server API
 *--------------------------------------------------------------------------------------------------------------------------------------------------------*/

/**
 * @brief Send data via TCP Server to connected client
 */
WiFiComm_Status WiFiComm__TCPServerSend(const char *payload)
{
#if (WIFI_TCP_SERVER_ENABLED == 1U)
    if (payload == NULL)
    {
        return WIFI_STATUS_ERROR;
    }
    if (!s_tcpServerClient || !s_tcpServerClient.connected())
    {
        return WIFI_STATUS_NO_CONN;
    }

    size_t len = strlen(payload);
    if (s_tcpServerClient.write((const uint8_t*)payload, len) == len)
    {
        WIFI_DBG_PRINTF("[WiFiComm] TCP-S TX: %s\n", payload);
        return WIFI_STATUS_OK;
    }
    return WIFI_STATUS_ERROR;
#else
    (void)payload;
    return WIFI_STATUS_ERROR;
#endif
}

/**
 * @brief Send raw bytes via TCP Server channel
 */
WiFiComm_Status WiFiComm__TCPServerSendRaw(const uint8_t *data,
                                            uint16_t       length)
{
#if (WIFI_TCP_SERVER_ENABLED == 1U)
    if ((data == NULL) || (length == 0U))
    {
        return WIFI_STATUS_ERROR;
    }
    if (!s_tcpServerClient || !s_tcpServerClient.connected())
    {
        return WIFI_STATUS_NO_CONN;
    }

    if (s_tcpServerClient.write(data, length) == length)
    {
        return WIFI_STATUS_OK;
    }
    return WIFI_STATUS_ERROR;
#else
    (void)data;
    (void)length;
    return WIFI_STATUS_ERROR;
#endif
}

/**
 * @brief Get TCP Server state
 */
WiFiComm_TCPState WiFiComm__GetTCPServerState(void)
{
#if (WIFI_TCP_SERVER_ENABLED == 1U)
    if (!s_tcpServerStarted)
    {
        return WIFI_TCP_DISCONNECTED;
    }
    if (s_tcpServerClient && s_tcpServerClient.connected())
    {
        return WIFI_TCP_CONNECTED;
    }
    return WIFI_TCP_DISCONNECTED;
#else
    return WIFI_TCP_DISCONNECTED;
#endif
}

/**
 * @brief Check if TCP Server has a client connected
 */
bool WiFiComm__TCPServerHasClient(void)
{
#if (WIFI_TCP_SERVER_ENABLED == 1U)
    return (s_tcpServerClient && s_tcpServerClient.connected());
#else
    return false;
#endif
}

/*------------------------------------------------------------------------------------------------------------------------------------------------------------
 *  MQTT API
 *--------------------------------------------------------------------------------------------------------------------------------------------------------*/

/**
 * @brief Publish message to any MQTT topic
 */
WiFiComm_Status WiFiComm__MQTTPublish(const char *topic,
                                       const char *payload)
{
#if (WIFI_MQTT_ENABLED == 1U)
    if ((topic == NULL) || (payload == NULL))
    {
        return WIFI_STATUS_ERROR;
    }
    if (!s_mqttClient.connected())
    {
        return WIFI_STATUS_NO_CONN;
    }

    bool result = s_mqttClient.publish(topic, payload,
                                        (bool)WIFI_MQTT_RETAIN);
    if (result)
    {
        WIFI_DBG_PRINTF("[WiFiComm] MQTT PUB [%s]: %s\n", topic, payload);
        return WIFI_STATUS_OK;
    }
    return WIFI_STATUS_ERROR;
#else
    (void)topic;
    (void)payload;
    return WIFI_STATUS_ERROR;
#endif
}

/**
 * @brief Publish message with explicit QoS and retain flag
 */
WiFiComm_Status WiFiComm__MQTTPublishEx(const char *topic,
                                         const char *payload,
                                         uint8_t     qos,
                                         bool        retain)
{
#if (WIFI_MQTT_ENABLED == 1U)
    if ((topic == NULL) || (payload == NULL))
    {
        return WIFI_STATUS_ERROR;
    }
    if (!s_mqttClient.connected())
    {
        return WIFI_STATUS_NO_CONN;
    }

    /* PubSubClient only supports QoS 0 for publish -- the qos parameter
     * is accepted for API compatibility but NOT forwarded to the broker.
     * 'retain' IS forwarded correctly. Document this in WiFiComm_Cfg.h. */
    bool result = s_mqttClient.publish(topic,
                                        (const uint8_t*)payload,
                                        (unsigned int)strlen(payload),
                                        retain);
    if (result)
    {
        WIFI_DBG_PRINTF("[WiFiComm] MQTT PUB EX [%s] QoS=%d: %s\n",
                      topic, (int)qos, payload);
        return WIFI_STATUS_OK;
    }
    return WIFI_STATUS_ERROR;
#else
    (void)topic;
    (void)payload;
    (void)qos;
    (void)retain;
    return WIFI_STATUS_ERROR;
#endif
}

/**
 * @brief Subscribe to any MQTT topic
 */
WiFiComm_Status WiFiComm__MQTTSubscribe(const char *topic)
{
#if (WIFI_MQTT_ENABLED == 1U)
    if (topic == NULL)
    {
        return WIFI_STATUS_ERROR;
    }
    if (!s_mqttClient.connected())
    {
        return WIFI_STATUS_NO_CONN;
    }

    if (s_mqttClient.subscribe(topic, (int)WIFI_MQTT_QOS))
    {
        WIFI_DBG_PRINTF("[WiFiComm] MQTT SUB: %s\n", topic);
        return WIFI_STATUS_OK;
    }
    return WIFI_STATUS_ERROR;
#else
    (void)topic;
    return WIFI_STATUS_ERROR;
#endif
}

/**
 * @brief Unsubscribe from an MQTT topic
 */
WiFiComm_Status WiFiComm__MQTTUnsubscribe(const char *topic)
{
#if (WIFI_MQTT_ENABLED == 1U)
    if (topic == NULL)
    {
        return WIFI_STATUS_ERROR;
    }
    if (s_mqttClient.unsubscribe(topic))
    {
        WIFI_DBG_PRINTF("[WiFiComm] MQTT UNSUB: %s\n", topic);
        return WIFI_STATUS_OK;
    }
    return WIFI_STATUS_ERROR;
#else
    (void)topic;
    return WIFI_STATUS_ERROR;
#endif
}

/**
 * @brief Get MQTT connection state
 */
WiFiComm_MQTTState WiFiComm__GetMQTTState(void)
{
#if (WIFI_MQTT_ENABLED == 1U)
    if (s_mqttClient.connected())
    {
        return WIFI_MQTT_CONNECTED;
    }
    return WIFI_MQTT_DISCONNECTED;
#else
    return WIFI_MQTT_DISCONNECTED;
#endif
}

/*------------------------------------------------------------------------------------------------------------------------------------------------------------
 *  Status & Diagnostics
 *--------------------------------------------------------------------------------------------------------------------------------------------------------*/

/**
 * @brief Get current WiFi connection state
 */
WiFiComm_WiFiState WiFiComm__GetWiFiState(void)
{
    switch (s_wifiSMState)
    {
        case WIFI_SM_CONNECTED:    return WIFI_STATE_CONNECTED;
        case WIFI_SM_CONNECTING:   return WIFI_STATE_CONNECTING;
        case WIFI_SM_RECONNECTING: return WIFI_STATE_DISCONNECTED;
        default:                   return WIFI_STATE_DISCONNECTED;
    }
}

/**
 * @brief Check if WiFi is connected
 */
bool WiFiComm__IsWiFiConnected(void)
{
    return (s_wifiSMState == WIFI_SM_CONNECTED);
}

/**
 * @brief Get ESP32 IP address as string
 */
const char* WiFiComm__GetIPAddress(void)
{
    return s_ipAddressStr;
}

/**
 * @brief Get WiFi signal strength (RSSI)
 */
int32_t WiFiComm__GetRSSI(void)
{
    if (s_wifiSMState == WIFI_SM_CONNECTED)
    {
        return (int32_t)WiFi.RSSI();
    }
    return 0;
}

/**
 * @brief Clear saved WiFi credentials from NVS.
 */
void WiFiComm__ClearCredentials(void)
{
    Preferences prefs;
    prefs.begin("wifi_creds", false);
    prefs.clear();
    prefs.end();
    WIFI_DBG_PRINTLN("[WiFiComm] NVS WiFi credentials cleared");
}

/*==============================================================================
 *                          PRIVATE FUNCTION IMPLEMENTATIONS
 *============================================================================*/

/**
 * @brief WiFi state machine handler
 * @details Manages WiFi connection, reconnection and LED error indication
 */
static void WiFiComm__HandleWiFiSM(uint32_t currentTimeMs)
{
    switch (s_wifiSMState)
    {
        case WIFI_SM_IDLE:
        {
            /* Nothing to do -- Init not called yet */
            break;
        }

        case WIFI_SM_CONNECTING:
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                WiFiComm__OnWiFiConnected();
                s_wifiSMState = WIFI_SM_CONNECTED;
            }
            else if ((currentTimeMs - s_wifiConnectStartTime) >=
                     WIFI_CONNECT_TIMEOUT_MS)
            {
                WIFI_DBG_PRINTLN("[WiFiComm] WiFi connect timeout -- retrying...");
                s_wifiSMState      = WIFI_SM_RECONNECTING;
                s_wifiReconnectTimer = currentTimeMs;

                if (!s_wifiErrorReported)
                {
                    s_wifiErrorReported = true;
                    LEDHMI__ErrorActive();
                }
            }
            break;
        }

        case WIFI_SM_CONNECTED:
        {
            if (WiFi.status() != WL_CONNECTED)
            {
                WIFI_DBG_PRINTLN("[WiFiComm] WiFi lost -- reconnecting...");
                WiFiComm__OnWiFiDisconnected();   /* was never called -- dead function fixed */
                s_wifiSMState        = WIFI_SM_RECONNECTING;
                s_wifiReconnectTimer = currentTimeMs;

                if (!s_wifiErrorReported)
                {
                    s_wifiErrorReported = true;
                    LEDHMI__ErrorActive();
                }

                /* Reset TCP states on WiFi loss */
#if (WIFI_TCP_CLIENT_ENABLED == 1U)
                s_tcpClient.stop();
                s_tcpClientSMState = TCP_CLIENT_SM_IDLE;
#endif
            }
            break;
        }

        case WIFI_SM_RECONNECTING:
        {
            if ((currentTimeMs - s_wifiReconnectTimer) >=
                WIFI_RECONNECT_CHECK_MS)
            {
                s_wifiReconnectTimer = currentTimeMs;

                if (WiFi.status() == WL_CONNECTED)
                {
                    WiFiComm__OnWiFiConnected();
                    s_wifiSMState = WIFI_SM_CONNECTED;
                }
                else
                {
                    WIFI_DBG_PRINTLN("[WiFiComm] Retrying WiFi...");
                    /* Disconnect cleanly before retrying --
                     * calling reconnect() while a connection attempt is
                     * already in progress causes 'sta is connecting' error */
                        WiFi.disconnect(true);
                        esp_task_wdt_reset();
                        WiFi.begin(s_activeSSID, s_activePass);
                    s_wifiConnectStartTime = currentTimeMs;
                    s_wifiSMState          = WIFI_SM_CONNECTING;
                }
            }
            break;
        }

        default:
        {
            s_wifiSMState = WIFI_SM_IDLE;
            break;
        }
    }
}

/**
 * @brief Called when WiFi connects successfully
 */
static void WiFiComm__OnWiFiConnected(void)
{
    /* Store IP address string */
    IPAddress ip = WiFi.localIP();
    snprintf(s_ipAddressStr, sizeof(s_ipAddressStr),
             "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

    WIFI_DBG_PRINTF("[WiFiComm] WiFi connected! IP: %s  RSSI: %d dBm\n",
                  s_ipAddressStr, (int)WiFi.RSSI());

    /* Clear error LED */
    if (s_wifiErrorReported)
    {
        s_wifiErrorReported = false;
        LEDHMI__ErrorCleared();
    }

    /* Start TCP server if enabled */
#if (WIFI_TCP_SERVER_ENABLED == 1U)
    if (!s_tcpServerStarted)
    {
        s_tcpServer.begin();
        s_tcpServerStarted = true;
        WIFI_DBG_PRINTF("[WiFiComm] TCP Server started on port %d\n",
                      (int)WIFI_TCP_SERVER_PORT);
    }
#endif
}

/**
 * @brief Called when WiFi disconnects
 */
static void WiFiComm__OnWiFiDisconnected(void)
{
    WIFI_DBG_PRINTLN("[WiFiComm] WiFi disconnected");
    memset(s_ipAddressStr, 0, sizeof(s_ipAddressStr));
    strncpy(s_ipAddressStr, "0.0.0.0", sizeof(s_ipAddressStr) - 1U);
}

/**
 * @brief TCP Client state machine handler
 */
static void WiFiComm__HandleTCPClient(uint32_t currentTimeMs)
{
#if (WIFI_TCP_CLIENT_ENABLED == 1U)
    switch (s_tcpClientSMState)
    {
        case TCP_CLIENT_SM_IDLE:
        {
            /* First time -- attempt connect immediately */
            WIFI_DBG_PRINTF("[WiFiComm] TCP Client connecting to %s:%d...\n",
                          WIFI_TCP_CLIENT_SERVER_IP,
                          (int)WIFI_TCP_CLIENT_PORT);

            if (s_tcpClient.connect(WIFI_TCP_CLIENT_SERVER_IP,
                                    WIFI_TCP_CLIENT_PORT))
            {
                s_tcpClientSMState = TCP_CLIENT_SM_CONNECTED;
                WIFI_DBG_PRINTF("[WiFiComm] TCP Client connected to %s:%d\n",
                              WIFI_TCP_CLIENT_SERVER_IP,
                              (int)WIFI_TCP_CLIENT_PORT);
            }
            else
            {
                s_tcpClientSMState  = TCP_CLIENT_SM_RECONNECTING;
                s_tcpClientReconnTimer = currentTimeMs;
                WIFI_DBG_PRINTLN("[WiFiComm] TCP Client connect failed -- retrying...");
            }
            break;
        }

        case TCP_CLIENT_SM_CONNECTED:
        {
            if (!s_tcpClient.connected())
            {
                WIFI_DBG_PRINTLN("[WiFiComm] TCP Client disconnected -- retrying...");
                s_tcpClient.stop();
                s_tcpClientSMState     = TCP_CLIENT_SM_RECONNECTING;
                s_tcpClientReconnTimer = currentTimeMs;
                break;
            }

            /* Read incoming data */
            uint16_t bytesRead = 0U;
            while (s_tcpClient.available() &&
                   (bytesRead < (WIFI_RX_BUFFER_SIZE - 1U)))
            {
                s_tcpClientRxBuf[bytesRead] = (char)s_tcpClient.read();
                bytesRead++;
            }

            if (bytesRead > 0U)
            {
                s_tcpClientRxBuf[bytesRead] = '\0';
                WIFI_DBG_PRINTF("[WiFiComm] TCP-C RX (%d bytes): %s\n",
                              (int)bytesRead, s_tcpClientRxBuf);

                if (s_tcpClientRxCb != NULL)
                {
                    s_tcpClientRxCb(NULL, s_tcpClientRxBuf,
                                    (uint16_t)bytesRead);
                }
            }
            break;
        }

        case TCP_CLIENT_SM_RECONNECTING:
        {
            if ((currentTimeMs - s_tcpClientReconnTimer) >=
                WIFI_TCP_CLIENT_RECONNECT_MS)
            {
                s_tcpClientReconnTimer = currentTimeMs;
                s_tcpClientSMState     = TCP_CLIENT_SM_IDLE;
            }
            break;
        }

        default:
        {
            s_tcpClientSMState = TCP_CLIENT_SM_IDLE;
            break;
        }
    }
#else
    (void)currentTimeMs;
#endif
}

/**
 * @brief TCP Server handler -- accept clients and process RX
 */
static void WiFiComm__HandleTCPServer(void)
{
#if (WIFI_TCP_SERVER_ENABLED == 1U)
    if (!s_tcpServerStarted)
    {
        return;
    }

    /* Accept new client if none connected */
    if (!s_tcpServerClient || !s_tcpServerClient.connected())
    {
        WiFiClient newClient = s_tcpServer.accept();
        if (newClient)
        {
            s_tcpServerClient = newClient;
            WIFI_DBG_PRINTF("[WiFiComm] TCP Server: client connected from %s\n",
                          s_tcpServerClient.remoteIP().toString().c_str());
        }
        return;
    }

    /* Read incoming data from connected client */
    uint16_t bytesRead = 0U;
    while (s_tcpServerClient.available() &&
           (bytesRead < (WIFI_RX_BUFFER_SIZE - 1U)))
    {
        s_tcpServerRxBuf[bytesRead] = (char)s_tcpServerClient.read();
        bytesRead++;
    }

    if (bytesRead > 0U)
    {
        s_tcpServerRxBuf[bytesRead] = '\0';
        WIFI_DBG_PRINTF("[WiFiComm] TCP-S RX (%d bytes): %s\n",
                      (int)bytesRead, s_tcpServerRxBuf);

        if (s_tcpServerRxCb != NULL)
        {
            s_tcpServerRxCb(NULL, s_tcpServerRxBuf, (uint16_t)bytesRead);
        }
    }
#endif
}

/**
 * @brief MQTT connection handler -- maintains broker connection
 */
static void WiFiComm__HandleMQTT(uint32_t currentTimeMs)
{
#if (WIFI_MQTT_ENABLED == 1U)
    if (s_mqttClient.connected())
    {
        /* Keep MQTT alive -- process incoming messages */
        s_mqttClient.loop();
        return;
    }

    /* Reconnect at interval */
    if ((currentTimeMs - s_mqttReconnTimer) >= WIFI_MQTT_RECONNECT_MS)
    {
        s_mqttReconnTimer = currentTimeMs;

        WIFI_DBG_PRINTF("[WiFiComm] MQTT connecting to %s:%d as '%s'...\n",
                      WIFI_MQTT_BROKER_IP,
                      (int)WIFI_MQTT_BROKER_PORT,
                      WIFI_MQTT_CLIENT_ID);

        if (s_mqttClient.connect(WIFI_MQTT_CLIENT_ID))
        {
            WIFI_DBG_PRINTLN("[WiFiComm] MQTT connected!");

            /* Re-subscribe to the default command topic only.
             * Application-specific topics (rtc/set, ota/command) are
             * registered by App__Init() via WiFiComm__MQTTSubscribe() and
             * re-registered automatically via the MQTT-connect edge detection
             * in App_DisplayData(). Do NOT add hardcoded app topics here --
             * this module must remain generic (no application knowledge). */
            s_mqttClient.subscribe(WIFI_MQTT_TOPIC_COMMAND, (int)WIFI_MQTT_QOS);
            WIFI_DBG_PRINTF("[WiFiComm] MQTT subscribed: %s\n", WIFI_MQTT_TOPIC_COMMAND);
        }
        else
        {
            WIFI_DBG_PRINTF("[WiFiComm] MQTT connect failed. State: %d\n",
                          s_mqttClient.state());
        }
    }
#else
    (void)currentTimeMs;
#endif
}

/**
 * @brief MQTT internal callback -- forwards to user callback
 */
#if (WIFI_MQTT_ENABLED == 1U)
static void WiFiComm__MQTTInternalCallback(char         *topic,
                                            byte         *payload,
                                            unsigned int  length)
{
    /* Copy topic and payload safely */
    strncpy(s_mqttRxTopic, topic,
            (size_t)(WIFI_MQTT_TOPIC_MAX_LEN - 1U));
    s_mqttRxTopic[WIFI_MQTT_TOPIC_MAX_LEN - 1U] = '\0';

    uint16_t safeLen = (uint16_t)((length < (WIFI_PAYLOAD_MAX_LEN - 1U)) ?
                                   length : (WIFI_PAYLOAD_MAX_LEN - 1U));
    memcpy(s_mqttRxPayload, payload, safeLen);
    s_mqttRxPayload[safeLen] = '\0';

    WIFI_DBG_PRINTF("[WiFiComm] MQTT RX [%s]: %s\n",
                  s_mqttRxTopic, s_mqttRxPayload);

    if (s_mqttRxCb != NULL)
    {
        s_mqttRxCb(s_mqttRxTopic, s_mqttRxPayload, safeLen);
    }
}
#endif