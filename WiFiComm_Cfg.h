/**
 * @file WiFiComm_Cfg.h
 * @brief WiFi Communication Module Configuration
 * @details Generic WiFi configuration for ESP32-S3.
 *          Supports TCP Client, TCP Server, UDP (future), and MQTT.
 *          Designed to be reusable across projects -- not sensor-specific.
 *
 * QUICK START:
 * ============
 * 1. Set WIFI_SSID and WIFI_PASSWORD (or provision via NVS)
 * 2. Set WIFI_TCP_CLIENT_SERVER_IP to your PC IP
 * 3. Enable/disable modes via defines below
 * 4. For MQTT: install PubSubClient, set WIFI_MQTT_ENABLED 1
 *
 * @version 1.1.0
 *
 * v1.1.0 changes (review fixes):
 *   - ~290 lines of commented-out previous version removed (MISRA Rule 2.1).
 *   - Include guard and Doxygen header added -- active section previously had
 *     neither, allowing double-inclusion and offering no file identification.
 *   - Encoding artefacts (???) replaced with -- in all comments.
 *   - WIFI_STATUS_* enum negative values noted; fix applied in WiFiComm.h.
 */

#ifndef WIFI_COMM_CFG_H
#define WIFI_COMM_CFG_H

/*==============================================================================
 *                          STATIC IP CONFIGURATION
 *  Set WIFI_STATIC_IP_ENABLED to 0 to use DHCP instead.
 *  Recommended: use static IP so the PC always knows the ESP32 address.
 *============================================================================*/

/** @brief Enable static IP: 1 = use static IP, 0 = use DHCP */
#define WIFI_STATIC_IP_ENABLED          (0U)

/** @brief Static IP address for ESP32 -- must be free on your network */
#define WIFI_STATIC_IP                  "192.168.7.2"

/** @brief Gateway IP -- your router IP address */
#define WIFI_STATIC_GATEWAY             "192.168.7.1"

/** @brief Subnet mask */
#define WIFI_STATIC_SUBNET              "255.255.255.0"

/** @brief Primary DNS */
#define WIFI_STATIC_DNS1                "8.8.8.8"

/** @brief Secondary DNS */
#define WIFI_STATIC_DNS2                "8.8.4.4"

/*==============================================================================
 *                          WIFI CREDENTIALS
 *  @warning These are compiled into the binary and visible via "strings".
 *           Move to NVS provisioning before production deployment.
 *============================================================================*/

/** @brief WiFi network SSID */
#define WIFI_SSID                       "1st floor"

/** @brief WiFi network password */
#define WIFI_PASSWORD                   "Plexon5555"

/*==============================================================================
 *                          MODE ENABLE FLAGS
 *  Set to 1 to enable, 0 to disable.
 *============================================================================*/
#define WIFI_TCP_CLIENT_ENABLED         (0U)  /**< ESP32 connects TO remote host   */
#define WIFI_TCP_SERVER_ENABLED         (0U)  /**< Remote host connects TO ESP32   */
#define WIFI_UDP_ENABLED                (0U)  /**< UDP -- future use               */
#define WIFI_MQTT_ENABLED               (1U)  /**< MQTT -- set to 1 to activate    */

/*==============================================================================
 *                          TCP CLIENT SETTINGS
 *  ESP32 connects to PC as client.
 *  PC must be running WiFi Server (listening on WIFI_TCP_CLIENT_PORT).
 *============================================================================*/

/** @brief Remote host IP (PC running AQI Monitor app in Server mode) */
#define WIFI_TCP_CLIENT_SERVER_IP       "192.168.7.107"

/** @brief Remote host port -- PC WiFi Server listens on this port */
#define WIFI_TCP_CLIENT_PORT            (8081U)

/** @brief TCP client reconnect interval in milliseconds */
#define WIFI_TCP_CLIENT_RECONNECT_MS    (5000U)

/** @brief TCP client connection timeout in milliseconds */
#define WIFI_TCP_CLIENT_TIMEOUT_MS      (3000U)

/*==============================================================================
 *                          TCP SERVER SETTINGS
 *  PC connects to ESP32 as client.
 *============================================================================*/

/** @brief TCP server listen port */
#define WIFI_TCP_SERVER_PORT            (8080U)

/** @brief Maximum simultaneous TCP server clients */
#define WIFI_TCP_SERVER_MAX_CLIENTS     (1U)

/*==============================================================================
 *                          UDP SETTINGS (future use)
 *============================================================================*/

/** @brief UDP local port (receive) */
#define WIFI_UDP_LOCAL_PORT             (9000U)

/** @brief UDP remote port (transmit) */
#define WIFI_UDP_REMOTE_PORT            (9001U)

/** @brief UDP remote IP */
#define WIFI_UDP_REMOTE_IP              "192.168.7.102"

/*==============================================================================
 *                          MQTT SETTINGS
 *  Requires: PubSubClient library.
 *  @warning WIFI_MQTT_BROKER_IP is a public broker. The OTA command topic is
 *           predictable from WIFI_MQTT_CLIENT_ID. Use a private broker with
 *           TLS and authentication before production deployment.
 *  @note PubSubClient only supports QoS 0 publish. WIFI_MQTT_QOS controls
 *        subscribe QoS only. WiFiComm__MQTTPublishEx() qos parameter is
 *        accepted for API compatibility but not forwarded to the broker.
 *============================================================================*/

/** @brief MQTT broker address (public cloud broker -- works from any location) */
#define WIFI_MQTT_BROKER_IP             "broker.hivemq.com"

/** @brief MQTT broker port (1883 = unencrypted, 8883 = TLS) */
#define WIFI_MQTT_BROKER_PORT           (1883U)

/** @brief MQTT client ID -- must be unique per device on broker */
#define WIFI_MQTT_CLIENT_ID             "ESP32_AQI_001"

/** @brief MQTT keep-alive interval in seconds */
#define WIFI_MQTT_KEEPALIVE_SEC         (60U)

/** @brief MQTT reconnect interval in milliseconds */
#define WIFI_MQTT_RECONNECT_MS          (5000U)

/** @brief MQTT QoS level for subscribe (0, 1, or 2) */
#define WIFI_MQTT_QOS                   (1U)

/** @brief MQTT retained message flag for publish (0 = not retained) */
#define WIFI_MQTT_RETAIN                (0U)

/* -- MQTT Topic Definitions ------------------------------------------------
 *  These are defaults only. WiFiComm__MQTTPublish() and
 *  WiFiComm__MQTTSubscribe() accept any topic string.               */
#define WIFI_MQTT_TOPIC_SENSOR_DATA     "aqi/sensor/data"    /**< Sensor JSON publish   */
#define WIFI_MQTT_TOPIC_COMMAND         "aqi/command"        /**< Command receive       */
#define WIFI_MQTT_TOPIC_STATUS          "aqi/status"         /**< Device status publish */
#define WIFI_MQTT_TOPIC_ALARM           "aqi/alarm"          /**< Alarm event publish   */
#define WIFI_MQTT_TOPIC_CONFIG          "aqi/config"         /**< Config receive        */

/*==============================================================================
 *                          GENERAL SETTINGS
 *============================================================================*/

/** @brief WiFi connection timeout in milliseconds (increased for BLE+WiFi coex) */
#define WIFI_CONNECT_TIMEOUT_MS         (20000U)

/** @brief WiFi reconnect check interval in milliseconds */
#define WIFI_RECONNECT_CHECK_MS         (3000U)

/** @brief Handler call interval in milliseconds */
#define WIFI_HANDLER_INTERVAL_MS        (100U)

/** @brief TX buffer size in bytes */
#define WIFI_TX_BUFFER_SIZE             (512U)

/** @brief RX buffer size in bytes */
#define WIFI_RX_BUFFER_SIZE             (512U)

/** @brief Maximum topic string length for MQTT */
#define WIFI_MQTT_TOPIC_MAX_LEN         (128U)

/** @brief Maximum payload string length */
#define WIFI_PAYLOAD_MAX_LEN            (512U)

/*==============================================================================
 *                          VALIDATION
 *============================================================================*/

#if (WIFI_TCP_CLIENT_PORT == WIFI_TCP_SERVER_PORT)
#error "WIFI_TCP_CLIENT_PORT and WIFI_TCP_SERVER_PORT must be different"
#endif

#if (WIFI_TX_BUFFER_SIZE < 64U)
#error "WIFI_TX_BUFFER_SIZE must be at least 64 bytes"
#endif

#if (WIFI_RX_BUFFER_SIZE < 64U)
#error "WIFI_RX_BUFFER_SIZE must be at least 64 bytes"
#endif

#if (WIFI_MQTT_QOS > 2U)
#error "WIFI_MQTT_QOS must be 0, 1, or 2"
#endif

#endif /* WIFI_COMM_CFG_H */