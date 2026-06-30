/**
 * @file SensorConfig.h
 * @brief Sensor type configuration and shared sensor data structure
 * @details Defines sensor types, frame detection bytes, NVS storage keys,
 *          and the SensorData struct shared between Core 0 (writer) and
 *          Core 1 (reader).
 *          Sensor type is auto-detected from frame start byte by CmdParser
 *          and persisted to NVS -- survives restart and power-off.
 *
 * Detection:
 *   ZPHS01B -> frame starts with 0xFF -> SENSOR_9PARAM
 *   ZPHS01C -> frame starts with 0x16 -> SENSOR_5PARAM
 *
 * @version 1.1.0
 *
 * v1.1.0 changes (review fixes):
 *   - Non-ASCII characters (degree, micro, em-dash, ellipsis) in struct
 *     field comments replaced with ASCII equivalents.
 *   - RTC comment: "DS1307" corrected to "PCF8563T".
 *   - SensorData: @warning added documenting cross-core race condition --
 *     the struct is shared between cores without a mutex.
 *   - SENSOR_FRAMES_PER_SCREEN and SENSOR_*_SCREEN_COUNT: marked as unused
 *     in the LED build (no display). Retained for TFT build compatibility.
 *   - Compile-time assert added: SENSOR_9PARAM != SENSOR_5PARAM.
 */

#ifndef SENSOR_CONFIG_H
#define SENSOR_CONFIG_H

#include <stdint.h>

/*==============================================================================
 *                          SENSOR TYPE CONSTANTS
 *============================================================================*/

/** @brief 9-parameter sensor (ZPHS01B) -- NO2, CH2O, CO, O3, PM2.5, CO2, TVOC, T, H */
#define SENSOR_9PARAM               (0xA5A5U)

/** @brief 5-parameter sensor (ZPHS01C) -- PM2.5, CO2, CH2O/VOC, T, H */
#define SENSOR_5PARAM               (0x5A5AU)

/** @brief Default sensor type if NVS has no stored value */
#define SENSOR_TYPE_DEFAULT         SENSOR_9PARAM

/*==============================================================================
 *                          FRAME DETECTION BYTES
 *============================================================================*/

/** @brief ZPHS01B frame start byte */
#define SENSOR_9PARAM_START_BYTE    (0xFFU)

/** @brief ZPHS01C frame start byte */
#define SENSOR_5PARAM_START_BYTE    (0x16U)

/*==============================================================================
 *                          SCREEN COUNT (LED build: unused -- no display)
 *  Retained for TFT build compatibility. Not referenced in LED build.
 *============================================================================*/

/** @brief Number of main screens for 9-parameter sensor (TFT build only) */
#define SENSOR_9PARAM_SCREEN_COUNT  (3U)

/** @brief Number of main screens for 5-parameter sensor (TFT build only) */
#define SENSOR_5PARAM_SCREEN_COUNT  (2U)

/** @brief Frames to display before auto-switching screen (TFT build only) */
#define SENSOR_FRAMES_PER_SCREEN    (3U)

/*==============================================================================
 *                          NVS CONFIGURATION
 *============================================================================*/

/** @brief NVS namespace for sensor config */
#define SENSOR_NVS_NAMESPACE        "sensor_cfg"

/** @brief NVS key for sensor type */
#define SENSOR_NVS_KEY_TYPE         "type"

/*==============================================================================
 *                          COMPILE-TIME VALIDATION
 *============================================================================*/

#ifdef __cplusplus
    #define _SC_ASSERT(expr, msg)   static_assert(expr, msg)
#else
    #define _SC_ASSERT(expr, msg)   _Static_assert(expr, msg)
#endif

_SC_ASSERT(SENSOR_9PARAM != SENSOR_5PARAM,
           "SENSOR_9PARAM and SENSOR_5PARAM must have different values");
_SC_ASSERT(SENSOR_9PARAM_START_BYTE != SENSOR_5PARAM_START_BYTE,
           "SENSOR_9PARAM_START_BYTE and SENSOR_5PARAM_START_BYTE must differ");

/*==============================================================================
 *                          SENSOR DATA STRUCT
 *
 *  Shared between Core 0 (writer) and Core 1 (reader) via:
 *    volatile SensorData currentData;  -- defined in AirSense_LEDBased_1V7.ino
 *
 *  Sensor fields : written by App.cpp (Core 0) after each CmdParser read
 *  RTC fields    : written by Scheduler_RtcTickHandler (Core 1) every 1s
 *                  (currently disabled -- RTC hardware not yet verified)
 *
 *  @warning No mutex protects this struct. Core 0 writes multiple float fields
 *           in App_DisplayData() while Core 1 (LEDHMI__Handler) reads them for
 *           threshold comparison. A torn read can cause mismatched sensor values
 *           (e.g. CO2 from new frame, PM2.5 from previous frame) and incorrect
 *           alarm decisions. Full fix: introduce a SensorData mutex API
 *           (SensorData__GetSnapshot / SensorData__Update). Current mitigation:
 *           LEDHMI__Handler copies volatile fields to locals before comparison.
 *============================================================================*/

typedef struct
{
    /* Air Quality Parameters (Core 0 writes, Core 1 reads for threshold check) */
    float    temperature;    /**< degC                                -- both sensors */
    float    humidity;       /**< %RH                                 -- both sensors */
    float    co2;            /**< ppm                                 -- both sensors */
    float    tvoc;           /**< grade 0-3                           -- both sensors */
    float    pm25;           /**< ug/m3                               -- both sensors */
    float    formaldehyde;   /**< ug/m3  (ch2o raw * 0.001 mg/m3)   -- ZPHS01B only */
    float    co;             /**< ppm    (co  raw * 0.1)             -- ZPHS01B only */
    float    o3;             /**< ppb    (o3  ppm * 1000)            -- ZPHS01B only */
    float    no2;            /**< ppm    (no2 raw * 0.01)            -- ZPHS01B only */
    float    pm1_0;          /**< ug/m3                              -- ZPHS01B only */
    float    pm10;           /**< ug/m3                              -- ZPHS01B only */

    /* RTC Date / Time (Core 1 writes every 1s from PCF8563T) */
    uint8_t  second;         /**< 0-59                                            */
    uint8_t  minute;         /**< 0-59                                            */
    uint8_t  hour;           /**< 0-23 (24-hour)                                  */
    uint8_t  day;            /**< 1-7  (1=Monday .. 7=Sunday)                     */
    uint8_t  date;           /**< 1-31                                            */
    uint8_t  month;          /**< 1-12                                            */
    uint16_t year;           /**< 2000-2099                                       */

    /* Status */
    bool     sensor_valid;   /**< true = latest CmdParser frame was valid         */

} SensorData;

#endif /* SENSOR_CONFIG_H */