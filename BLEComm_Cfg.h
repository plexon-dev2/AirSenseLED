/**
 * @file BLEComm_Cfg.h
 * @brief BLE Communication Module Configuration
 * @details Configuration parameters for BLE communication module.
 *          Designed to be reusable across projects -- not sensor-specific.
 *          Mirrors WiFiComm_Cfg.h pattern for consistency.
 *
 * QUICK START:
 * ============
 * 1. Set BLECOMM_DEVICE_NAME to your BLE device name
 * 2. Set UUIDs to match your host application (PyScript / phone app)
 * 3. AES key and auth token are loaded from NVS at runtime -- NOT compiled in.
 *    Provision via BLECOMM_NVS_NAMESPACE at first boot.
 *
 * @version 1.1.0
 *
 * v1.1.0 changes (review fixes):
 *   - Encoding artefacts (???) replaced with -- in comments
 *   - BLECOMM_DEBUG_* macros now use SERIAL_PRINTF from AppMutex.h
 *     (was Serial.printf -- bypassed serial mutex, caused Modbus RX corruption)
 *   - BLECOMM_FAN_FREQ_MIN_HZ / BLECOMM_FAN_FREQ_MAX_HZ added for validation
 *   - BLECOMM_NVS_NAMESPACE added for AES key NVS slot
 */

#ifndef BLECOMM_CFG_H
#define BLECOMM_CFG_H

#include "AppMutex.h"   /* For SERIAL_PRINTF mutex-safe debug output */

/*==============================================================================
 *                          DEVICE IDENTITY
 *============================================================================*/

/** @brief BLE device advertised name -- must match host app scan filter */
#define BLECOMM_DEVICE_NAME             "ESP32_AQI"

/*==============================================================================
 *                          SERVICE & CHARACTERISTIC UUIDs
 *  Must match your host application exactly.
 *============================================================================*/

/** @brief Primary BLE service UUID */
#define BLECOMM_SERVICE_UUID            "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"

/**
 * @brief Sensor data characteristic UUID
 * @details READ + NOTIFY -- ESP32 pushes sensor JSON to connected client
 */
#define BLECOMM_SENSOR_CHAR_UUID        "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

/**
 * @brief Identify characteristic UUID
 * @details WRITE -- client writes 16-byte AES-128 ECB encrypted token
 */
#define BLECOMM_IDENTIFY_CHAR_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

/**
 * @brief Fan duty percent characteristic UUID
 * @details WRITE -- client writes "0".."100" for direct duty control,
 *          or "AUTO" to return to sensor-based automatic control.
 */
#define BLECOMM_FAN_DUTY_CHAR_UUID      "6E400004-B5A3-F393-E0A9-E50E24DCCA9E"

/**
 * @brief Fan PWM frequency characteristic UUID
 * @details WRITE -- client writes frequency in Hz as a decimal string
 */
#define BLECOMM_FAN_FREQ_CHAR_UUID      "6E400005-B5A3-F393-E0A9-E50E24DCCA9E"

/*==============================================================================
 *                          AES AUTHENTICATION -- NVS KEYS
 *  AES key and auth token are NOT compiled in -- they are stored in NVS and
 *  loaded at runtime by BLEComm__Init().  Provision them at manufacturing
 *  time or on first boot via the provisioning AP.
 *
 *  NVS namespace: BLECOMM_NVS_NAMESPACE
 *  Key "aesKey"   : 16 bytes (raw binary stored as blob)
 *  Key "authToken": 16 bytes (raw binary stored as blob)
 *============================================================================*/

/** @brief NVS namespace for BLE authentication credentials */
#define BLECOMM_NVS_NAMESPACE           "bleAuth"

/** @brief NVS key for 16-byte AES-128 encryption key */
#define BLECOMM_NVS_KEY_AES             "aesKey"

/** @brief NVS key for 16-byte expected plaintext auth token */
#define BLECOMM_NVS_KEY_TOKEN           "authToken"

/** @brief Default AES key used only when NVS has no provisioned key.
 *  @warning CHANGE THIS before production deployment -- this default is
 *           publicly visible in source and provides no real security.
 *           Replace with a per-device key written to NVS at provisioning. */
#define BLECOMM_DEFAULT_AES_KEY         "AirSense2024Key!"

/** @brief Default auth token used only when NVS has no provisioned token.
 *  @warning Same warning as BLECOMM_DEFAULT_AES_KEY above. */
#define BLECOMM_DEFAULT_AUTH_TOKEN      "AIRSENSE_VALID01"

/*==============================================================================
 *                          CLIENT MODE (ESP32 connects TO remote device)
 *============================================================================*/

/** @brief Target device name when ESP32 connects as BLE client */
#define BLECOMM_PYSCRIPT_DEVICE_NAME    "PyScript_AQI"

/** @brief Remote service UUID when ESP32 is client */
#define BLECOMM_PYSCRIPT_SERVICE_UUID   "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"

/** @brief Remote characteristic UUID when ESP32 is client */
#define BLECOMM_PYSCRIPT_CHAR_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

/*==============================================================================
 *                          SCAN SETTINGS
 *============================================================================*/

/** @brief BLE scan duration in seconds */
#define BLECOMM_SCAN_DURATION_SEC       (5U)

/** @brief Maximum number of devices shown in scan results */
#define BLECOMM_MAX_DEVICES             (5U)

/** @brief BLE scan interval (units of 0.625ms) */
#define BLECOMM_SCAN_INTERVAL           (100U)

/**
 * @brief BLE scan window (units of 0.625ms)
 * @note  Must be <= BLECOMM_SCAN_INTERVAL
 */
#define BLECOMM_SCAN_WINDOW             (99U)

/*==============================================================================
 *                          FAN FREQUENCY LIMITS
 *============================================================================*/

/** @brief Minimum accepted fan PWM frequency from BLE WRITE (Hz) */
#define BLECOMM_FAN_FREQ_MIN_HZ         (1000U)

/** @brief Maximum accepted fan PWM frequency from BLE WRITE (Hz) */
#define BLECOMM_FAN_FREQ_MAX_HZ         (50000U)

/*==============================================================================
 *                          CONNECT TASK SETTINGS
 *============================================================================*/

/**
 * @brief Stack size for BLE connect background task (bytes)
 * @note  BLE connect() needs generous stack -- do not reduce below 6144
 */
#define BLECOMM_CONNECT_TASK_STACK      (16384U)

/** @brief Priority of BLE connect background task */
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
 *  Uses SERIAL_PRINTF from AppMutex.h -- mutex-safe across dual-core tasks.
 *  Bypassing the mutex (raw Serial.printf) causes Modbus RX byte corruption.
 *============================================================================*/

/** @brief Enable BLE debug serial output -- 1=enabled, 0=disabled */
#define BLECOMM_DEBUG_ENABLE            (1U)

#if (BLECOMM_DEBUG_ENABLE == 1U)
    /**
     * @brief Mutex-safe debug print -- routes through SERIAL_PRINTF.
     * @note  MISRA C:2012 Rule 20.10 advisory deviation: variadic macro.
     *        Rationale: no compliant alternative for printf-style debug wrapper;
     *        deviation isolated to this file and documented here.
     */
    #define BLECOMM_DEBUG_PRINT(x)      SERIAL_PRINTF("%s", x)
    #define BLECOMM_DEBUG_PRINTLN(x)    SERIAL_PRINTF("%s\n", x)
    #define BLECOMM_DEBUG_PRINTF(...)   SERIAL_PRINTF(__VA_ARGS__)
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

#if (BLECOMM_FAN_FREQ_MIN_HZ >= BLECOMM_FAN_FREQ_MAX_HZ)
#error "BLECOMM_FAN_FREQ_MIN_HZ must be less than BLECOMM_FAN_FREQ_MAX_HZ"
#endif

#endif /* BLECOMM_CFG_H */