/**
 * @file BLEComm_Cfg.h
 * @brief BLE Communication Module Configuration
 * @details Configuration parameters for BLE communication module.
 *          Designed to be reusable across projects ??? not sensor-specific.
 *
 *          Mirrors WiFiComm_Cfg.h pattern for consistency.
 *
 * QUICK START:
 * ============
 * 1. Set BLECOMM_DEVICE_NAME to your BLE device name
 * 2. Set UUIDs to match your host application (PyScript / phone app)
 * 3. Set BLECOMM_IDENTIFY_TOKEN to match your host identification string
 *
 * @version 1.0.0
 */

#ifndef BLECOMM_CFG_H
#define BLECOMM_CFG_H

/*==============================================================================
 *                          DEVICE IDENTITY
 *============================================================================*/

/**
 * @brief BLE device advertised name
 * @note  Must match what your host app (PyScript / phone) scans for
 */
#define BLECOMM_DEVICE_NAME             "ESP32_AQI"

/*==============================================================================
 *                          SERVICE & CHARACTERISTIC UUIDs
 * @note  Must match your host application exactly
 *============================================================================*/

/** @brief Primary BLE service UUID */
#define BLECOMM_SERVICE_UUID            "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"

/**
 * @brief Sensor data characteristic UUID
 * @details READ + NOTIFY ??? ESP32 pushes sensor JSON to connected client
 */
#define BLECOMM_SENSOR_CHAR_UUID        "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

/**
 * @brief Identify characteristic UUID
 * @details WRITE ??? client writes identify token to enable sensor data stream
 */
#define BLECOMM_IDENTIFY_CHAR_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

/*==============================================================================
 *                          IDENTIFICATION HANDSHAKE
 *============================================================================*/

/**
 * @brief Identify token ??? client must write this exact string to enable data
 * @details Only clients that write this token receive sensor NOTIFY data.
 *          Generic phone connections are accepted but receive no data.
 */
#define BLECOMM_IDENTIFY_TOKEN          "IDENTIFY:PyScript_AQI"

/*==============================================================================
 *                          CLIENT MODE (ESP32 connects TO remote device)
 *============================================================================*/

/**
 * @brief Target device name when ESP32 connects as BLE client
 * @details ESP32 connects TO this device (e.g. PyScript running on PC)
 */
#define BLECOMM_PYSCRIPT_DEVICE_NAME    "PyScript_AQI"

/**
 * @brief Remote service UUID when ESP32 is client
 */
#define BLECOMM_PYSCRIPT_SERVICE_UUID   "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"

/**
 * @brief Remote characteristic UUID when ESP32 is client
 */
#define BLECOMM_PYSCRIPT_CHAR_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

/*==============================================================================
 *                          SCAN SETTINGS
 *============================================================================*/

/**
 * @brief BLE scan duration in seconds
 */
#define BLECOMM_SCAN_DURATION_SEC       (5U)

/**
 * @brief Maximum number of devices shown in scan results
 */
#define BLECOMM_MAX_DEVICES             (5U)

/**
 * @brief BLE scan interval (units of 0.625ms)
 */
#define BLECOMM_SCAN_INTERVAL           (100U)

/**
 * @brief BLE scan window (units of 0.625ms)
 * @note  Must be <= BLECOMM_SCAN_INTERVAL
 */
#define BLECOMM_SCAN_WINDOW             (99U)

/*==============================================================================
 *                          CONNECT TASK SETTINGS
 *============================================================================*/

/**
 * @brief Stack size for BLE connect background task (bytes)
 * @note  BLE connect() needs generous stack ??? do not reduce below 6144
 */
#define BLECOMM_CONNECT_TASK_STACK      (16384U)

/**
 * @brief Priority of BLE connect background task
 */
#define BLECOMM_CONNECT_TASK_PRIORITY   (1U)

/*==============================================================================
 *                          HANDLER SETTINGS
 *============================================================================*/

/**
 * @brief Expected handler call interval in milliseconds
 * @note  Call BLEComm__Handler() at this rate from scheduler
 */
#define BLECOMM_HANDLER_INTERVAL_MS     (100U)

/*==============================================================================
 *                          DEBUG
 *============================================================================*/

/**
 * @brief Enable BLE debug serial output
 * @details Set to 1 to enable, 0 to disable
 */
#define BLECOMM_DEBUG_ENABLE            (1U)

#if (BLECOMM_DEBUG_ENABLE == 1U)
    #define BLECOMM_DEBUG_PRINT(x)      Serial.print(x)
    #define BLECOMM_DEBUG_PRINTLN(x)    Serial.println(x)
    #define BLECOMM_DEBUG_PRINTF(...)   Serial.printf(__VA_ARGS__)
#else
    #define BLECOMM_DEBUG_PRINT(x)
    #define BLECOMM_DEBUG_PRINTLN(x)
    #define BLECOMM_DEBUG_PRINTF(...)
#endif

/*==============================================================================
 *                          VALIDATION
 *============================================================================*/

#if (BLECOMM_SCAN_WINDOW > BLECOMM_SCAN_INTERVAL)
#error "BLECOMM_SCAN_WINDOW must be <= BLECOMM_SCAN_INTERVAL"
#endif

#if (BLECOMM_MAX_DEVICES < 1U) || (BLECOMM_MAX_DEVICES > 10U)
#error "BLECOMM_MAX_DEVICES must be between 1 and 10"
#endif

#if (BLECOMM_CONNECT_TASK_STACK < 6144U)
#error "BLECOMM_CONNECT_TASK_STACK must be at least 6144 bytes"
#endif

#endif /* BLECOMM_CFG_H */








