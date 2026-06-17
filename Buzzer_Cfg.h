/**
 * @file Buzzer_Cfg.h
 * @brief Buzzer Configuration -- LED Project
 *
 * Pin sourced from HardwareConfig.h.
 *   BUZZER -> HW_PIN_BUZZER = GPIO42
 *
 * @version 2.4.0 -- pin now references HW_PIN_BUZZER macro
 */

#ifndef BUZZER_CFG_H
#define BUZZER_CFG_H

#include <stdint.h>
#include "HardwareConfig.h"

/*==============================================================================
 *                          PIN  (from HardwareConfig.h)
 *============================================================================*/
#define BUZZER_PIN                  HW_PIN_BUZZER   /**< GPIO42 */

/*==============================================================================
 *                          LEDC
 *============================================================================*/
#define BUZZER_LEDC_CHANNEL         (1U)
#define BUZZER_LEDC_TIMER           (1U)
#define BUZZER_FREQ_HZ              (2000U)
#define BUZZER_RESOLUTION           (8U)
#define BUZZER_PWM_RESOLUTION_BITS  BUZZER_RESOLUTION

/*==============================================================================
 *                          VOLUME
 *============================================================================*/
#define BUZZER_DEFAULT_VOLUME       (75U)
#define BUZZER_VOLUME_MIN           (0U)
#define BUZZER_VOLUME_MAX           (100U)

/*==============================================================================
 *                          BEEP TIMING
 *============================================================================*/
#define BUZZER_BEEP_MS              (100U)
#define BUZZER_BEEP_GAP_MS          (100U)
#define BUZZER_BEEP_LONG_MS         (500U)
#define BUZZER_SHORT_BEEP_MS        BUZZER_BEEP_MS
#define BUZZER_LONG_BEEP_MS_ALT     BUZZER_BEEP_LONG_MS

/*==============================================================================
 *                          TONE PRESETS
 *============================================================================*/
#define BUZZER_ALARM_FREQ_HZ        (2000U)
#define BUZZER_NOTIFY_FREQ_HZ       (1000U)
#define BUZZER_ERROR_FREQ_HZ        (500U)

/*==============================================================================
 *                          DEBUG
 *============================================================================*/
#define BUZZER_DEBUG_ENABLE         (0U)
#if (BUZZER_DEBUG_ENABLE == 1U)
    #define BUZZER_DEBUG_PRINTF(...)  Serial.printf(__VA_ARGS__)
    #define BUZZER_DEBUG_PRINTLN(x)   Serial.println(x)
#else
    #define BUZZER_DEBUG_PRINTF(...)
    #define BUZZER_DEBUG_PRINTLN(x)
#endif

/*==============================================================================
 *                          VALIDATION
 *============================================================================*/
#if (BUZZER_LEDC_CHANNEL > 7U)
#error "BUZZER_LEDC_CHANNEL must be 0-7"
#endif
#if (BUZZER_DEFAULT_VOLUME > 100U)
#error "BUZZER_DEFAULT_VOLUME must be 0-100"
#endif

#endif /* BUZZER_CFG_H */