/**
 * @file WiFiComm.h
 * @brief Generic WiFi Communication Module Interface
 * @details Provides TCP Client, TCP Server, UDP (future), and MQTT
 *          communication over WiFi for ESP32-S3.
 *
 *          Designed as a fully independent, reusable module.
 *          Not tied to any specific application or sensor.
 *
 * USAGE:
 * ======
 *   setup():
 *     WiFiComm__Init();
 *
 *   loop() every 100ms:
 *     WiFiComm__Handler();
 *
 *   Send data anytime:
 *     WiFiComm__TCPClientSend("{\"t\":25,\"h\":60}");
 *     WiFiComm__TCPServerSend("{\"t\":25,\"h\":60}");
 *     WiFiComm__MQTTPublish("aqi/sensor/data", "{\"t\":25}");
 *
 * @author
 * @date 2025-12-19
 * @version 1.0.0
 */

#ifndef WIFI_COMM_H
#define WIFI_COMM_H

/*==============================================================================
 *                              INCLUDES
 *============================================================================*/
#include <stdint.h>
#include <stdbool.h>

/*==============================================================================
 *                              PUBLIC TYPES
 *============================================================================*/

/**
 * @brief WiFi connection state
 */
typedef enum
{
    WIFI_STATE_DISCONNECTED = 0, /**< Not connected to WiFi          */
    WIFI_STATE_CONNECTING   = 1, /**< Connecting to WiFi             */
    WIFI_STATE_CONNECTED    = 2, /**< Connected to WiFi network      */
    WIFI_STATE_ERROR        = 3  /**< Connection error               */
} WiFiComm_WiFiState;

/**
 * @brief TCP connection state (shared for client and server)
 */
typedef enum
{
    WIFI_TCP_DISCONNECTED = 0, /**< TCP not connected               */
    WIFI_TCP_CONNECTING   = 1, /**< TCP connection in progress      */
    WIFI_TCP_CONNECTED    = 2  /**< TCP connected and ready         */
} WiFiComm_TCPState;

/**
 * @brief MQTT connection state
 */
typedef enum
{
    WIFI_MQTT_DISCONNECTED = 0, /**< MQTT not connected             */
    WIFI_MQTT_CONNECTING   = 1, /**< MQTT connecting to broker      */
    WIFI_MQTT_CONNECTED    = 2  /**< MQTT connected to broker       */
} WiFiComm_MQTTState;

/**
 * @brief Operation status
 */
typedef enum
{
    WIFI_STATUS_OK      =  0, /**< Operation successful            */
    WIFI_STATUS_ERROR   = -1, /**< Operation failed                */
    WIFI_STATUS_BUSY    = -2, /**< Module busy                     */
    WIFI_STATUS_NO_CONN = -3  /**< No active connection            */
} WiFiComm_Status;

/**
 * @brief RX callback function type
 * @details Called when data is received on any channel.
 *          topic is NULL for TCP/UDP, set for MQTT.
 * @param[in] topic   MQTT topic string (NULL for TCP/UDP)
 * @param[in] payload Received data (null-terminated string)
 * @param[in] length  Payload length in bytes
 */
typedef void (*WiFiComm_RxCallback)(const char *topic,
                                     const char *payload,
                                     uint16_t    length);

/*==============================================================================
 *                          PUBLIC FUNCTION DECLARATIONS
 *============================================================================*/

/**
 * @brief Initialize WiFi communication module
 * @details Connects to WiFi, starts TCP server if enabled,
 *          initializes MQTT client if enabled.
 *          Call once from setup().
 */
void WiFiComm__Init(void);

/**
 * @brief WiFi communication periodic handler
 * @details Maintains all connections, handles reconnects,
 *          processes incoming data, drives LED error indication.
 *          Call every WIFI_HANDLER_INTERVAL_MS (100ms) from loop().
 */
void WiFiComm__Handler(void);

/*──────────────────────────────────────────────────────────────────────────────
 *  Callback Registration
 *────────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Register RX callback for TCP Client channel
 * @details Callback is called when data is received from remote TCP server
 * @param[in] callback Function pointer to RX handler
 */
void WiFiComm__RegisterTCPClientRxCallback(WiFiComm_RxCallback callback);

/**
 * @brief Register RX callback for TCP Server channel
 * @details Callback is called when data is received from connected TCP client
 * @param[in] callback Function pointer to RX handler
 */
void WiFiComm__RegisterTCPServerRxCallback(WiFiComm_RxCallback callback);

/**
 * @brief Register RX callback for MQTT channel
 * @details Callback is called when subscribed topic receives a message
 * @param[in] callback Function pointer to RX handler (topic is set)
 */
void WiFiComm__RegisterMQTTRxCallback(WiFiComm_RxCallback callback);

/*──────────────────────────────────────────────────────────────────────────────
 *  TCP Client API  (ESP32 → Remote host)
 *────────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Send data via TCP Client channel
 * @details Sends null-terminated string to remote TCP server.
 *          Generic — any string payload accepted.
 * @param[in] payload Null-terminated string to send
 * @return WIFI_STATUS_OK on success, error code otherwise
 */
WiFiComm_Status WiFiComm__TCPClientSend(const char *payload);

/**
 * @brief Send raw bytes via TCP Client channel
 * @param[in] data   Pointer to data buffer
 * @param[in] length Number of bytes to send
 * @return WIFI_STATUS_OK on success, error code otherwise
 */
WiFiComm_Status WiFiComm__TCPClientSendRaw(const uint8_t *data,
                                            uint16_t       length);

/**
 * @brief Get TCP Client connection state
 * @return Current WiFiComm_TCPState
 */
WiFiComm_TCPState WiFiComm__GetTCPClientState(void);

/*──────────────────────────────────────────────────────────────────────────────
 *  TCP Server API  (Remote host → ESP32)
 *────────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Send data via TCP Server channel to connected client
 * @details Sends null-terminated string to connected TCP client (PC).
 *          Generic — any string payload accepted.
 * @param[in] payload Null-terminated string to send
 * @return WIFI_STATUS_OK on success, error code otherwise
 */
WiFiComm_Status WiFiComm__TCPServerSend(const char *payload);

/**
 * @brief Send raw bytes via TCP Server channel
 * @param[in] data   Pointer to data buffer
 * @param[in] length Number of bytes to send
 * @return WIFI_STATUS_OK on success, error code otherwise
 */
WiFiComm_Status WiFiComm__TCPServerSendRaw(const uint8_t *data,
                                            uint16_t       length);

/**
 * @brief Get TCP Server state
 * @return Current WiFiComm_TCPState
 */
WiFiComm_TCPState WiFiComm__GetTCPServerState(void);

/**
 * @brief Check if TCP Server has a client connected
 * @return true if client connected, false otherwise
 */
bool WiFiComm__TCPServerHasClient(void);

/*──────────────────────────────────────────────────────────────────────────────
 *  MQTT API  (Generic publish/subscribe — any topic)
 *────────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Publish message to any MQTT topic
 * @details Generic publish — not limited to sensor data.
 *          Topic and payload are fully user-defined.
 * @param[in] topic   MQTT topic string (e.g. "aqi/sensor/data")
 * @param[in] payload Null-terminated payload string
 * @return WIFI_STATUS_OK on success, error code otherwise
 */
WiFiComm_Status WiFiComm__MQTTPublish(const char *topic,
                                       const char *payload);

/**
 * @brief Publish message to any MQTT topic with explicit QoS and retain
 * @param[in] topic   MQTT topic string
 * @param[in] payload Null-terminated payload string
 * @param[in] qos     QoS level (0, 1, or 2)
 * @param[in] retain  Retained message flag
 * @return WIFI_STATUS_OK on success, error code otherwise
 */
WiFiComm_Status WiFiComm__MQTTPublishEx(const char *topic,
                                         const char *payload,
                                         uint8_t     qos,
                                         bool        retain);

/**
 * @brief Subscribe to any MQTT topic
 * @details Generic subscribe — any topic string accepted.
 *          Received messages are delivered via registered MQTT RX callback.
 * @param[in] topic MQTT topic string (wildcards # and + supported)
 * @return WIFI_STATUS_OK on success, error code otherwise
 */
WiFiComm_Status WiFiComm__MQTTSubscribe(const char *topic);

/**
 * @brief Unsubscribe from an MQTT topic
 * @param[in] topic MQTT topic string to unsubscribe
 * @return WIFI_STATUS_OK on success, error code otherwise
 */
WiFiComm_Status WiFiComm__MQTTUnsubscribe(const char *topic);

/**
 * @brief Get MQTT connection state
 * @return Current WiFiComm_MQTTState
 */
WiFiComm_MQTTState WiFiComm__GetMQTTState(void);

/*──────────────────────────────────────────────────────────────────────────────
 *  Status & Diagnostics
 *────────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Get current WiFi connection state
 * @return Current WiFiComm_WiFiState
 */
WiFiComm_WiFiState WiFiComm__GetWiFiState(void);

/**
 * @brief Check if WiFi is connected
 * @return true if WiFi connected, false otherwise
 */
bool WiFiComm__IsWiFiConnected(void);
void WiFiComm__ClearCredentials(void);

/**
 * @brief Get ESP32 IP address as string
 * @return Pointer to null-terminated IP string, or "0.0.0.0" if not connected
 */
const char* WiFiComm__GetIPAddress(void);

/**
 * @brief Get WiFi signal strength (RSSI)
 * @return RSSI in dBm, or 0 if not connected
 */
int32_t WiFiComm__GetRSSI(void);

#endif /* WIFI_COMM_H */