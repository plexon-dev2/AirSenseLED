// /**
//  * @file WiFiComm_Cfg.h
//  * @brief WiFi Communication Module Configuration
//  * @details Generic WiFi configuration for ESP32-S3.
//  *          Supports TCP Client, TCP Server, UDP (future), and MQTT.
//  *          Designed to be reusable across projects ??? not sensor-specific.
//  *
//  * QUICK START:
//  * ============
//  * 1. Set WIFI_SSID and WIFI_PASSWORD
//  * 2. Set WIFI_TCP_CLIENT_SERVER_IP to your PC IP
//  * 3. Enable/disable modes via defines below
//  * 4. For MQTT: install PubSubClient, set WIFI_MQTT_ENABLED 1
//  *
//  * @author
//  * @date 2025-12-19
//  * @version 1.0.0
//  */

// #ifndef WIFI_COMM_CFG_H
// #define WIFI_COMM_CFG_H

// /*==============================================================================
//  *                          STATIC IP CONFIGURATION
//  *          Set WIFI_STATIC_IP_ENABLED to 0 to use DHCP instead
//  *          Recommended: use static IP so PC always knows ESP32 address
//  *============================================================================*/

// /** @brief Enable static IP: 1 = use static IP, 0 = use DHCP */
// #define WIFI_STATIC_IP_ENABLED          (0U)

// /** @brief Static IP address for ESP32 ??? must be free on your network */
// #define WIFI_STATIC_IP                  "192.168.7.2"

// /** @brief Gateway IP ??? your router IP address */
// #define WIFI_STATIC_GATEWAY             "192.168.7.1"

// /** @brief Subnet mask */
// #define WIFI_STATIC_SUBNET              "255.255.255.0"

// /** @brief Primary DNS */
// #define WIFI_STATIC_DNS1                "8.8.8.8"

// /** @brief Secondary DNS */
// #define WIFI_STATIC_DNS2                "8.8.4.4"

// /*==============================================================================
//  *                          WIFI CREDENTIALS
//  *============================================================================*/

// /**
//  * @brief WiFi network SSID
//  * @note  Replace with your WiFi network name
//  */
// #define WIFI_SSID                       "1st floor"

// /**
//  * @brief WiFi network password
//  * @note  Replace with your WiFi password
//  */
// #define WIFI_PASSWORD                   "Plexon5555"

// /*==============================================================================
//  *                          MODE ENABLE FLAGS
//  *============================================================================*/

// /**
//  * @defgroup WiFiComm_ModeFlags Communication mode enable flags
//  * @brief Set to 1 to enable, 0 to disable
//  * @{
//  */
// #define WIFI_TCP_CLIENT_ENABLED         (0U)  /**< ESP32 connects TO remote host  */
// #define WIFI_TCP_SERVER_ENABLED         (0U)  /**< Remote host connects TO ESP32  */
// #define WIFI_UDP_ENABLED                (0U)  /**< UDP ??? future use               */
// #define WIFI_MQTT_ENABLED               (1U)  /**< MQTT ??? change to 1 to activate */
// /** @} */

// /*==============================================================================
//  *                          TCP CLIENT SETTINGS
//  *          ESP32 connects to PC as client
//  *          PC must be running WiFi Server (listening on WIFI_TCP_CLIENT_PORT)
//  *============================================================================*/

// /**
//  * @brief Remote host IP (PC running AQI Monitor app in Server mode)
//  */
// #define WIFI_TCP_CLIENT_SERVER_IP       "192.168.7.107"    /**< PC WiFi IP */

// /**
//  * @brief Remote host port ??? PC WiFi Server listens on this port
//  * @note  PC app: click <- SERVER button (listens on 8081)
//  */
// #define WIFI_TCP_CLIENT_PORT            (8081U)

// /**
//  * @brief TCP client reconnect interval in milliseconds
//  */
// #define WIFI_TCP_CLIENT_RECONNECT_MS    (5000U)

// /**
//  * @brief TCP client connection timeout in milliseconds
//  */
// #define WIFI_TCP_CLIENT_TIMEOUT_MS      (3000U)

// /*==============================================================================
//  *                          TCP SERVER SETTINGS
//  *          PC connects to ESP32 as client
//  *          PC must use WiFi Client button ??? ESP32 IP : WIFI_TCP_SERVER_PORT
//  *============================================================================*/

// /**
//  * @brief TCP server listen port
//  * @note  PC app: enter ESP32 IP + this port, click CLIENT -> button
//  */
// #define WIFI_TCP_SERVER_PORT            (8080U)

// /**
//  * @brief Maximum simultaneous TCP server clients
//  */
// #define WIFI_TCP_SERVER_MAX_CLIENTS     (1U)

// /*==============================================================================
//  *                          UDP SETTINGS (Future use)
//  *============================================================================*/

// /**
//  * @brief UDP local port (receive)
//  */
// #define WIFI_UDP_LOCAL_PORT             (9000U)

// /**
//  * @brief UDP remote port (transmit)
//  */
// #define WIFI_UDP_REMOTE_PORT            (9001U)

// /**
//  * @brief UDP remote IP
//  */
// #define WIFI_UDP_REMOTE_IP              "192.168.7.102"

// /*==============================================================================
//  *                          MQTT SETTINGS
//  *          Requires: PubSubClient library
//  *          Install mosquitto on PC: https://mosquitto.org/download/
//  *          Enable: set WIFI_MQTT_ENABLED to 1
//  *============================================================================*/

// /**
//  * @brief MQTT broker IP address
//  * @note  For local broker: use PC IP (mosquitto running on PC)
//  *        For cloud: use broker URL e.g. "broker.hivemq.com"
//  */
// #define WIFI_MQTT_BROKER_IP             "192.168.7.3"

// /**
//  * @brief MQTT broker port
//  * @note  Standard: 1883 (unencrypted), 8883 (TLS)
//  */
// #define WIFI_MQTT_BROKER_PORT           (1883U)

// /**
//  * @brief MQTT client ID ??? must be unique per device on broker
//  */
// #define WIFI_MQTT_CLIENT_ID             "ESP32_AQI_001"

// /**
//  * @brief MQTT keep-alive interval in seconds
//  */
// #define WIFI_MQTT_KEEPALIVE_SEC         (60U)

// /**
//  * @brief MQTT reconnect interval in milliseconds
//  */
// #define WIFI_MQTT_RECONNECT_MS          (5000U)

// /**
//  * @brief MQTT QoS level for publish
//  * @note  0 = at most once, 1 = at least once, 2 = exactly once
//  */
// #define WIFI_MQTT_QOS                   (1U)

// /**
//  * @brief MQTT retained message flag for publish
//  * @note  0 = not retained, 1 = retained (broker keeps last message)
//  */
// #define WIFI_MQTT_RETAIN                (0U)

// /**
//  * @defgroup WiFiComm_MQTTTopics MQTT Topic Definitions
//  * @brief Default topics ??? can be overridden or extended freely
//  *        These are just defaults ??? WiFiComm__MQTTPublish() and
//  *        WiFiComm__MQTTSubscribe() accept ANY topic string
//  * @{
//  */
// #define WIFI_MQTT_TOPIC_SENSOR_DATA     "aqi/sensor/data"    /**< Sensor JSON publish    */
// #define WIFI_MQTT_TOPIC_COMMAND         "aqi/command"        /**< Command receive        */
// #define WIFI_MQTT_TOPIC_STATUS          "aqi/status"         /**< Device status publish  */
// #define WIFI_MQTT_TOPIC_ALARM           "aqi/alarm"          /**< Alarm event publish    */
// #define WIFI_MQTT_TOPIC_CONFIG          "aqi/config"         /**< Config receive         */
// /** @} */

// /*==============================================================================
//  *                          GENERAL SETTINGS
//  *============================================================================*/

// /**
//  * @brief WiFi connection timeout in milliseconds
//  */
// #define WIFI_CONNECT_TIMEOUT_MS         (20000U)  /**< Increased for BLE+WiFi coex */

// /**
//  * @brief WiFi reconnect check interval in milliseconds
//  */
// #define WIFI_RECONNECT_CHECK_MS         (3000U)   /**< Faster reconnect check      */

// /**
//  * @brief Handler call interval in milliseconds
//  * @note  Call WiFiComm__Handler() at this rate from main loop
//  */
// #define WIFI_HANDLER_INTERVAL_MS        (100U)

// /**
//  * @brief TX buffer size in bytes
//  */
// #define WIFI_TX_BUFFER_SIZE             (512U)

// /**
//  * @brief RX buffer size in bytes
//  */
// #define WIFI_RX_BUFFER_SIZE             (512U)

// /**
//  * @brief Maximum topic string length for MQTT
//  */
// #define WIFI_MQTT_TOPIC_MAX_LEN         (128U)

// /**
//  * @brief Maximum payload string length
//  */
// #define WIFI_PAYLOAD_MAX_LEN            (512U)

// /*==============================================================================
//  *                          VALIDATION
//  *============================================================================*/

// #if (WIFI_TCP_CLIENT_PORT == WIFI_TCP_SERVER_PORT)
// #error "WIFI_TCP_CLIENT_PORT and WIFI_TCP_SERVER_PORT must be different"
// #endif

// #if (WIFI_TX_BUFFER_SIZE < 64U)
// #error "WIFI_TX_BUFFER_SIZE must be at least 64 bytes"
// #endif

// #if (WIFI_RX_BUFFER_SIZE < 64U)
// #error "WIFI_RX_BUFFER_SIZE must be at least 64 bytes"
// #endif

// #if (WIFI_MQTT_QOS > 2U)
// #error "WIFI_MQTT_QOS must be 0, 1, or 2"
// #endif

// #endif /* WIFI_COMM_CFG_H */

/**
 * @file WiFiComm_Cfg.h
 * @brief WiFi Communication Module Configuration
 * @details Generic WiFi configuration for ESP32-S3.
 *          Supports TCP Client, TCP Server, UDP (future), and MQTT.
 *          Designed to be reusable across projects ??? not sensor-specific.
 *
 * QUICK START:
 * ============
 * 1. Set WIFI_SSID and WIFI_PASSWORD
 * 2. Set WIFI_TCP_CLIENT_SERVER_IP to your PC IP
 * 3. Enable/disable modes via defines below
 * 4. For MQTT: install PubSubClient, set WIFI_MQTT_ENABLED 1
 *
 * @author
 * @date 2025-12-19
 * @version 1.0.0
 */

#ifndef WIFI_COMM_CFG_H
#define WIFI_COMM_CFG_H

/*==============================================================================
 *                          STATIC IP CONFIGURATION
 *          Set WIFI_STATIC_IP_ENABLED to 0 to use DHCP instead
 *          Recommended: use static IP so PC always knows ESP32 address
 *============================================================================*/

/** @brief Enable static IP: 1 = use static IP, 0 = use DHCP */
#define WIFI_STATIC_IP_ENABLED          (0U)

/** @brief Static IP address for ESP32 ??? must be free on your network */
#define WIFI_STATIC_IP                  "192.168.7.2"

/** @brief Gateway IP ??? your router IP address */
#define WIFI_STATIC_GATEWAY             "192.168.7.1"

/** @brief Subnet mask */
#define WIFI_STATIC_SUBNET              "255.255.255.0"

/** @brief Primary DNS */
#define WIFI_STATIC_DNS1                "8.8.8.8"

/** @brief Secondary DNS */
#define WIFI_STATIC_DNS2                "8.8.4.4"

/*==============================================================================
 *                          WIFI CREDENTIALS
 *============================================================================*/

/**
 * @brief WiFi network SSID
 * @note  Replace with your WiFi network name
 */
#define WIFI_SSID                       "1st floor"

/**
 * @brief WiFi network password
 * @note  Replace with your WiFi password
 */
#define WIFI_PASSWORD                   "Plexon5555"

/*==============================================================================
 *                          MODE ENABLE FLAGS
 *============================================================================*/

/**
 * @defgroup WiFiComm_ModeFlags Communication mode enable flags
 * @brief Set to 1 to enable, 0 to disable
 * @{
 */
#define WIFI_TCP_CLIENT_ENABLED         (0U)  /**< ESP32 connects TO remote host  */
#define WIFI_TCP_SERVER_ENABLED         (0U)  /**< Remote host connects TO ESP32  */
#define WIFI_UDP_ENABLED                (0U)  /**< UDP ??? future use               */
#define WIFI_MQTT_ENABLED               (1U)  /**< MQTT ??? change to 1 to activate */
/** @} */

/*==============================================================================
 *                          TCP CLIENT SETTINGS
 *          ESP32 connects to PC as client
 *          PC must be running WiFi Server (listening on WIFI_TCP_CLIENT_PORT)
 *============================================================================*/

/**
 * @brief Remote host IP (PC running AQI Monitor app in Server mode)
 */
#define WIFI_TCP_CLIENT_SERVER_IP       "192.168.7.107"    /**< PC WiFi IP */

/**
 * @brief Remote host port ??? PC WiFi Server listens on this port
 * @note  PC app: click <- SERVER button (listens on 8081)
 */
#define WIFI_TCP_CLIENT_PORT            (8081U)

/**
 * @brief TCP client reconnect interval in milliseconds
 */
#define WIFI_TCP_CLIENT_RECONNECT_MS    (5000U)

/**
 * @brief TCP client connection timeout in milliseconds
 */
#define WIFI_TCP_CLIENT_TIMEOUT_MS      (3000U)

/*==============================================================================
 *                          TCP SERVER SETTINGS
 *          PC connects to ESP32 as client
 *          PC must use WiFi Client button ??? ESP32 IP : WIFI_TCP_SERVER_PORT
 *============================================================================*/

/**
 * @brief TCP server listen port
 * @note  PC app: enter ESP32 IP + this port, click CLIENT -> button
 */
#define WIFI_TCP_SERVER_PORT            (8080U)

/**
 * @brief Maximum simultaneous TCP server clients
 */
#define WIFI_TCP_SERVER_MAX_CLIENTS     (1U)

/*==============================================================================
 *                          UDP SETTINGS (Future use)
 *============================================================================*/

/**
 * @brief UDP local port (receive)
 */
#define WIFI_UDP_LOCAL_PORT             (9000U)

/**
 * @brief UDP remote port (transmit)
 */
#define WIFI_UDP_REMOTE_PORT            (9001U)

/**
 * @brief UDP remote IP
 */
#define WIFI_UDP_REMOTE_IP              "192.168.7.102"

/*==============================================================================
 *                          MQTT SETTINGS
 *          Requires: PubSubClient library
 *          Install mosquitto on PC: https://mosquitto.org/download/
 *          Enable: set WIFI_MQTT_ENABLED to 1
 *============================================================================*/

/**
 * @brief MQTT broker IP address
 * @note  For local broker: use PC IP (mosquitto running on PC)
 *        For cloud: use broker URL e.g. "broker.hivemq.com"
 */
#define WIFI_MQTT_BROKER_IP             "broker.hivemq.com"   /* public cloud broker -- works from any location */

/**
 * @brief MQTT broker port
 * @note  Standard: 1883 (unencrypted), 8883 (TLS)
 */
#define WIFI_MQTT_BROKER_PORT           (1883U)

/**
 * @brief MQTT client ID ??? must be unique per device on broker
 */
#define WIFI_MQTT_CLIENT_ID             "ESP32_AQI_001"

/**
 * @brief MQTT keep-alive interval in seconds
 */
#define WIFI_MQTT_KEEPALIVE_SEC         (60U)

/**
 * @brief MQTT reconnect interval in milliseconds
 */
#define WIFI_MQTT_RECONNECT_MS          (5000U)

/**
 * @brief MQTT QoS level for publish
 * @note  0 = at most once, 1 = at least once, 2 = exactly once
 */
#define WIFI_MQTT_QOS                   (1U)

/**
 * @brief MQTT retained message flag for publish
 * @note  0 = not retained, 1 = retained (broker keeps last message)
 */
#define WIFI_MQTT_RETAIN                (0U)

/**
 * @defgroup WiFiComm_MQTTTopics MQTT Topic Definitions
 * @brief Default topics ??? can be overridden or extended freely
 *        These are just defaults ??? WiFiComm__MQTTPublish() and
 *        WiFiComm__MQTTSubscribe() accept ANY topic string
 * @{
 */
#define WIFI_MQTT_TOPIC_SENSOR_DATA     "aqi/sensor/data"    /**< Sensor JSON publish    */
#define WIFI_MQTT_TOPIC_COMMAND         "aqi/command"        /**< Command receive        */
#define WIFI_MQTT_TOPIC_STATUS          "aqi/status"         /**< Device status publish  */
#define WIFI_MQTT_TOPIC_ALARM           "aqi/alarm"          /**< Alarm event publish    */
#define WIFI_MQTT_TOPIC_CONFIG          "aqi/config"         /**< Config receive         */
/** @} */

/*==============================================================================
 *                          GENERAL SETTINGS
 *============================================================================*/

/**
 * @brief WiFi connection timeout in milliseconds
 */
#define WIFI_CONNECT_TIMEOUT_MS         (20000U)  /**< Increased for BLE+WiFi coex */

/**
 * @brief WiFi reconnect check interval in milliseconds
 */
#define WIFI_RECONNECT_CHECK_MS         (3000U)   /**< Faster reconnect check      */

/**
 * @brief Handler call interval in milliseconds
 * @note  Call WiFiComm__Handler() at this rate from main loop
 */
#define WIFI_HANDLER_INTERVAL_MS        (100U)

/**
 * @brief TX buffer size in bytes
 */
#define WIFI_TX_BUFFER_SIZE             (512U)

/**
 * @brief RX buffer size in bytes
 */
#define WIFI_RX_BUFFER_SIZE             (512U)

/**
 * @brief Maximum topic string length for MQTT
 */
#define WIFI_MQTT_TOPIC_MAX_LEN         (128U)

/**
 * @brief Maximum payload string length
 */
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