/**
 * @file App_Cfg.h
 * @brief Application Module Configuration -- LED Project
 *
 * Pins sourced from HardwareConfig.h.
 *   SENSOR_POWER -> HW_PIN_SENSOR_POWER = GPIO48
 *
 * @version 3.4.0 -- SENSOR_POWER pin added from HardwareConfig.h
 */

#ifndef APP_CFG_H
#define APP_CFG_H

#include <stdint.h>
#include <stdbool.h>
#include "HardwareConfig.h"

/*==============================================================================
 *                          PIN CONFIGURATION  (from HardwareConfig.h)
 *============================================================================*/

/**
 * @brief Sensor power supply enable -- GPIO48
 * @details Drive HIGH to power the ZPHS01B/C sensor.
 *          Drive LOW to cut power for hard reset.
 */
#define APP_SENSOR_POWER_PIN        HW_PIN_SENSOR_POWER     /**< GPIO48 */

/**
 * @brief RS485 isolated power supply monitor -- not in HardwareConfig.h
 * @details Keep as direct value until added to HardwareConfig.h.
 *          HIGH = power supply error, LOW = normal.
 */
#define APP_RS485_ISO_POWER_PIN     (3U)

/*==============================================================================
 *                          TIMING
 *============================================================================*/
#define APP_ISO_POWER_DEBOUNCE_COUNT    (10U)
#define APP_DISPLAY_UPDATE_INTERVAL     (10U)

/*==============================================================================
 *                          FEATURE FLAGS
 *============================================================================*/
#define APP_DEBUG_ENABLE                (1U)
#define APP_COMM_MONITORING_ENABLE      (1U)
#define APP_HEAP_MONITORING_ENABLE      (1U)
#define APP_HEAP_LOG_INTERVAL           (100U)

/*==============================================================================
 *                          DEBUG MACROS
 *============================================================================*/
#if (APP_DEBUG_ENABLE == 1U)
    #define APP_DBG_PRINT(x)      Serial.print(x)
    #define APP_DBG_PRINTLN(x)    Serial.println(x)
    #define APP_DBG_PRINTF(...)   Serial.printf(__VA_ARGS__)
#else
    #define APP_DBG_PRINT(x)
    #define APP_DBG_PRINTLN(x)
    #define APP_DBG_PRINTF(...)
#endif

/*==============================================================================
 *                          VALIDATION
 *============================================================================*/
#if (APP_ISO_POWER_DEBOUNCE_COUNT < 1U) || (APP_ISO_POWER_DEBOUNCE_COUNT > 100U)
#error "APP_ISO_POWER_DEBOUNCE_COUNT must be 1-100"
#endif
#if (APP_DISPLAY_UPDATE_INTERVAL < 1U) || (APP_DISPLAY_UPDATE_INTERVAL > 100U)
#error "APP_DISPLAY_UPDATE_INTERVAL must be 1-100"
#endif

#endif /* APP_CFG_H */