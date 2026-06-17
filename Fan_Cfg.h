/**
 * @file Fan_Cfg.h
 * @brief Fan Control Configuration -- LED Project
 *
 * Pin sourced from HardwareConfig.h.
 *   FAN_PWM -> HW_PIN_FAN_PWM = GPIO6
 *
 * @version 2.4.0 -- pin now references HW_PIN_FAN_PWM macro
 */

#ifndef FAN_CFG_H
#define FAN_CFG_H

#include <stdint.h>
#include "HardwareConfig.h"

/*==============================================================================
 *                          PIN  (from HardwareConfig.h)
 *============================================================================*/
#define FAN_PIN                     HW_PIN_FAN_PWM  /**< GPIO6 */
#define FAN_TACH_PIN                (2U)            /**< GPIO2 -- ADC1_CH1, FAN_I */

/*==============================================================================
 *                          LEDC / PWM
 *============================================================================*/
#define FAN_LEDC_CHANNEL            (0U)
#define FAN_LEDC_TIMER              (0U)
#define FAN_PWM_FREQ_HZ             (25000U)
#define FAN_PWM_RESOLUTION_BITS     (8U)
#define FAN_PWM_RESOLUTION          FAN_PWM_RESOLUTION_BITS

/*==============================================================================
 *                          VOLTAGE / DUTY
 *============================================================================*/
#define FAN_VOLT_OFF                (0.5f)
#define FAN_VOLT_START              (2.0f)
#define FAN_VOLT_FULL               (5.0f)
#define FAN_SUPPLY_VOLTAGE_V        (5.0f)
#define FAN_DUTY_MAX                (255U)
#define FAN_DUTY_MIN                ((uint8_t)((FAN_VOLT_START / FAN_VOLT_FULL) * (float)FAN_DUTY_MAX))

/*==============================================================================
 *                          TACHOMETER
 *============================================================================*/
#define FAN_TACH_ENABLE             (0U)
#define FAN_TACH_ENABLED            FAN_TACH_ENABLE
#define FAN_FAIL_RPM_THRESHOLD      (200U)
#define FAN_TACH_PULSES_PER_REV     (2U)

/*==============================================================================
 *                          DEBUG
 *============================================================================*/
#define FAN_DEBUG_ENABLE            (0U)
#if (FAN_DEBUG_ENABLE == 1U)
    #define FAN_DEBUG_PRINTF(...)   Serial.printf(__VA_ARGS__)
    #define FAN_DEBUG_PRINTLN(x)    Serial.println(x)
#else
    #define FAN_DEBUG_PRINTF(...)
    #define FAN_DEBUG_PRINTLN(x)
#endif

/*==============================================================================
 *                          VALIDATION
 *============================================================================*/
#if (FAN_LEDC_CHANNEL > 7U)
#error "FAN_LEDC_CHANNEL must be 0-7"
#endif

#endif /* FAN_CFG_H */