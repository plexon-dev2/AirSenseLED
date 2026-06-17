/**
 * @file RTC_Cfg.h
 * @brief RTC Module Configuration ??? LED Project
 *
 * All I2C pins sourced from HardwareConfig.h ??? do not hardcode here.
 *
 * CORRECTED PINS (from HardwareConfig.h):
 *   RTC_I2C_SDA_PIN : GPIO15  (HW_PIN_RTC_SDA)
 *   RTC_I2C_SCL_PIN : GPIO16  (HW_PIN_RTC_SCL)
 *   RTC_INT_PIN     : GPIO3   (HW_PIN_RTC_INT)
 *
 * Previous config had wrong pins:
 *   GPIO41 (SDA) ??? GPIO15 (SDA)
 *   GPIO40 (SCL) ??? GPIO16 (SCL)  GPIO40 = LED_MODBUS
 *   GPIO42 (INT) ??? GPIO3  (INT)  GPIO42 = BUZZER
 */

#ifndef RTC_CFG_H
#define RTC_CFG_H

#include "HardwareConfig.h"   /* Master GPIO pin definitions */

/*==============================================================================
 *                          PLATFORM SELECTION
 *============================================================================*/
#define RTC_PLATFORM_ARDUINO                (0U)
#define RTC_PLATFORM_STM32_HAL              (1U)
#define RTC_PLATFORM_CUSTOM                 (2U)

#define RTC_PLATFORM                        (RTC_PLATFORM_ARDUINO)

/*==============================================================================
 *                          I2C CONFIGURATION (ARDUINO)
 *============================================================================*/
#if (RTC_PLATFORM == RTC_PLATFORM_ARDUINO)

#define RTC_I2C_FREQ_HZ                     (100000UL)

/**
 * @brief I2C SDA pin ??? from HardwareConfig.h
 */
#define RTC_I2C_SDA_PIN                     (HW_PIN_RTC_SDA)

/**
 * @brief I2C SCL pin ??? from HardwareConfig.h
 */
#define RTC_I2C_SCL_PIN                     (HW_PIN_RTC_SCL)

/**
 * @brief RTC interrupt pin ??? from HardwareConfig.h
 */
#define RTC_INT_PIN                         (HW_PIN_RTC_INT)

#endif /* RTC_PLATFORM_ARDUINO */

/*==============================================================================
 *                          I2C CONFIGURATION (STM32 HAL)
 *============================================================================*/
#if (RTC_PLATFORM == RTC_PLATFORM_STM32_HAL)

/** @brief I2C handle ??? match your CubeMX-generated handle */
#define RTC_STM32_I2C_HANDLE                hi2c1

/** @brief I2C timeout in milliseconds */
#define RTC_STM32_I2C_TIMEOUT_MS            (100U)

#endif /* RTC_PLATFORM_STM32_HAL */

/*==============================================================================
 *                          DS1307 CONFIGURATION
 *
 * IMPORTANT: Do NOT rename these macros.
 *   RTC.cpp references them by these exact names.
 *   If your LED project's RTC_Cfg.h used different names
 *   (RTC_I2C_ADDRESS, RTC_YEAR_BASE, RTC_DEBUG_PRINTF) that is
 *   why you got "was not declared in this scope" errors.
 *============================================================================*/

/** @brief DS1307 7-bit I2C address (hardware fixed ??? do not change) */
#define RTC_I2C_ADDR                        (0x68U)

/**
 * @brief Year base offset
 * @details DS1307 stores 0-99; this offset is added on read.
 * @note  RTC.cpp uses RTC_YEAR_OFFSET. Do not rename to RTC_YEAR_BASE.
 */
#define RTC_YEAR_OFFSET                     (2000U)

/** @brief Valid year range */
#define RTC_YEAR_MIN                        (2000U)
#define RTC_YEAR_MAX                        (2099U)

/**
 * @brief Unix epoch offset: seconds from 1970-01-01 to 2000-01-01
 * @details Must be present ??? RTC_SetUnixTime() and RTC_GetUnixTime() use it.
 */
#define RTC_UNIX_EPOCH_2000                 (946684800UL)

/*==============================================================================
 *                          BEHAVIOUR CONFIGURATION
 *============================================================================*/

/** @brief Auto-start oscillator on Init if CH bit is set: 1=enabled, 0=disabled */
#define RTC_AUTO_START_OSCILLATOR           (1U)

/** @brief Force 24-hour mode on Init: 1=enabled, 0=disabled */
#define RTC_FORCE_24HR_MODE                 (1U)

/** @brief Enable debug Serial prints: 1=enabled, 0=disabled */
#define RTC_DEBUG_ENABLE                    (0U)

/**
 * @brief Debug print macro
 * @note  RTC.cpp calls RTC_DEBUG_PRINT("..."). Do not rename to RTC_DEBUG_PRINTF.
 */
#if (RTC_DEBUG_ENABLE == 1U)
    #define RTC_DEBUG_PRINT(...)            Serial.printf(__VA_ARGS__)
#else
    #define RTC_DEBUG_PRINT(...)            do {} while(0)
#endif

#endif /* RTC_CFG_H */


