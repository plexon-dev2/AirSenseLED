/**
 * @file SensorConfig.h
 * @brief Sensor type configuration and screen mapping
 * @details Defines sensor types, screen counts and NVS storage key.
 *          Sensor type is auto-detected from frame start byte.
 *          Stored in NVS — survives restart and power off.
 *
 * Detection:
 *   ZPHS01B → frame starts with 0xFF → SENSOR_9PARAM → 3 screens
 *   ZPHS01C → frame starts with 0x16 → SENSOR_5PARAM → 2 screens
 */

#ifndef SENSOR_CONFIG_H
#define SENSOR_CONFIG_H

#include <stdint.h>

/*==============================================================================
 *                          SENSOR TYPE CONSTANTS
 *============================================================================*/

/** @brief 9-parameter sensor (ZPHS01B) — NO2, CH2O, CO, O3, PM2.5, CO2, TVOC, T, H */
#define SENSOR_9PARAM               (0xA5A5U)

/** @brief 5-parameter sensor (ZPHS01C) — PM2.5, CO2, CH2O/VOC, T, H */
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
 *                          SCREEN COUNT
 *============================================================================*/

/** @brief Number of main screens for 9-parameter sensor */
#define SENSOR_9PARAM_SCREEN_COUNT  (3U)

/** @brief Number of main screens for 5-parameter sensor */
#define SENSOR_5PARAM_SCREEN_COUNT  (2U)

/*==============================================================================
 *                          AUTO SCREEN SWITCH
 *============================================================================*/

/** @brief Number of frames to display before auto-switching to next screen */
#define SENSOR_FRAMES_PER_SCREEN    (3U)

/*==============================================================================
 *                          NVS CONFIGURATION
 *============================================================================*/

/** @brief NVS namespace for sensor config */
#define SENSOR_NVS_NAMESPACE        "sensor_cfg"

/** @brief NVS key for sensor type */
#define SENSOR_NVS_KEY_TYPE         "type"


/*==============================================================================
 *                          SENSOR DATA STRUCT
 *
 *  Shared between Core 0 (writer) and Core 1 (reader) via:
 *    volatile SensorData currentData;   ← defined in .ino
 *
 *  Sensor fields  : written by App.cpp (Core 0) after each CmdParser read
 *  RTC fields     : written by Scheduler_RtcTickHandler (Core 1) every 1s
 *  NOTE: This struct is added for the LED build. The TFT build defines it
 *        in a separate display-layer header — not needed here.
 *============================================================================*/

typedef struct
{
    /* Air Quality Parameters (Core 0 writes, Core 1 reads for threshold check) */
    float    temperature;    /**< °C                                 — both sensors */
    float    humidity;       /**< %RH                                — both sensors */
    float    co2;            /**< ppm                                — both sensors */
    float    tvoc;           /**< grade 0–3                          — both sensors */
    float    pm25;           /**< µg/m³                              — both sensors */
    float    formaldehyde;   /**< µg/m³  (ch2o raw × 0.001 mg/m³)  — ZPHS01B only */
    float    co;             /**< ppm    (co  raw × 0.1)            — ZPHS01B only */
    float    o3;             /**< ppb    (o3  ppm × 1000)           — ZPHS01B only */
    float    no2;            /**< ppm    (no2 raw × 0.01)           — ZPHS01B only */
    float    pm1_0;          /**< µg/m³                             — ZPHS01B only */
    float    pm10;           /**< µg/m³                             — ZPHS01B only */

    /* RTC Date / Time (Core 1 writes every 1 s from DS1307) */
    uint8_t  second;         /**< 0–59                                            */
    uint8_t  minute;         /**< 0–59                                            */
    uint8_t  hour;           /**< 0–23 (24-hour)                                  */
    uint8_t  day;            /**< 1–7  (1=Monday … 7=Sunday)                     */
    uint8_t  date;           /**< 1–31                                            */
    uint8_t  month;          /**< 1–12                                            */
    uint16_t year;           /**< 2000–2099                                       */

    /* Status */
    bool     sensor_valid;   /**< true = latest CmdParser frame was valid         */

} SensorData;

#endif /* SENSOR_CONFIG_H */