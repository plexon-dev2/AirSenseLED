/**
 * @file RTC_Cfg.h
 * @brief RTC Module Configuration -- LED Project (PCF8563T)
 *
 * All I2C pins sourced from HardwareConfig.h -- do not hardcode here.
 *
 * CORRECTED PINS (from HardwareConfig.h):
 *   RTC_I2C_SDA_PIN : GPIO15  (HW_PIN_RTC_SDA)
 *   RTC_I2C_SCL_PIN : GPIO16  (HW_PIN_RTC_SCL)
 *   RTC_INT_PIN     : GPIO3   (HW_PIN_RTC_INT)
 *
 * Previous config had wrong pins:
 *   GPIO41 (SDA) -- corrected to -- GPIO15 (SDA)
 *   GPIO40 (SCL) -- corrected to -- GPIO16 (SCL)  GPIO40 = LED_MODBUS
 *   GPIO42 (INT) -- corrected to -- GPIO3  (INT)  GPIO42 = BUZZER
 *
 * @version 1.1.0
 *
 * v1.1.0 changes (review fixes):
 *   - Encoding artefacts (???) replaced with -- in all comments.
 *   - RTC_DEBUG_PRINT: now routes through SERIAL_PRINTF (mutex-safe).
 *     Raw Serial.printf from I2C task context causes Modbus RX corruption.
 *   - RTC_AUTO_START_OSCILLATOR: removed -- was defined but RTC.cpp
 *     unconditionally clears the STOP bit regardless of this value.
 *   - RTC_FORCE_24HR_MODE: removed -- PCF8563T is inherently 24h; no
 *     mode register exists for this on this chip.
 *   - RTC_I2C_ADDR: now used in RTC.cpp via PCF8563_ADDR alias --
 *     was defined in cfg but unused (RTC.cpp had its own local define).
 */

#ifndef RTC_CFG_H
#define RTC_CFG_H

#include "HardwareConfig.h"   /* Master GPIO pin definitions */
#include "AppMutex.h"         /* SERIAL_PRINTF -- mutex-safe debug output */

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

/** @brief I2C SDA pin -- from HardwareConfig.h */
#define RTC_I2C_SDA_PIN                     (HW_PIN_RTC_SDA)

/** @brief I2C SCL pin -- from HardwareConfig.h */
#define RTC_I2C_SCL_PIN                     (HW_PIN_RTC_SCL)

/** @brief RTC interrupt pin -- from HardwareConfig.h */
#define RTC_INT_PIN                         (HW_PIN_RTC_INT)

#endif /* RTC_PLATFORM_ARDUINO */

/*==============================================================================
 *                          I2C CONFIGURATION (STM32 HAL)
 *============================================================================*/
#if (RTC_PLATFORM == RTC_PLATFORM_STM32_HAL)

/** @brief I2C handle -- match your CubeMX-generated handle */
#define RTC_STM32_I2C_HANDLE                hi2c1

/** @brief I2C timeout in milliseconds */
#define RTC_STM32_I2C_TIMEOUT_MS            (100U)

#endif /* RTC_PLATFORM_STM32_HAL */

/*==============================================================================
 *                          PCF8563T CONFIGURATION
 *
 * IMPORTANT: Do NOT rename these macros.
 *   RTC.cpp references them by these exact names.
 *============================================================================*/

/**
 * @brief PCF8563T 7-bit I2C address (hardware fixed -- do not change).
 * @details Used in RTC.cpp as PCF8563_ADDR. Same value -- single source.
 */
#define RTC_I2C_ADDR                        (0x51U)

/**
 * @brief Year base offset.
 * @details PCF8563T stores years 00-99; this offset is added on read.
 * @note  RTC.cpp uses RTC_YEAR_OFFSET. Do not rename to RTC_YEAR_BASE.
 */
#define RTC_YEAR_OFFSET                     (2000U)

/** @brief Valid year range */
#define RTC_YEAR_MIN                        (2000U)
#define RTC_YEAR_MAX                        (2099U)

/**
 * @brief Unix epoch offset: seconds from 1970-01-01 to 2000-01-01.
 * @details Used by RTC_SetUnixTime() and RTC_GetUnixTime().
 */
#define RTC_UNIX_EPOCH_2000                 (946684800UL)

/*==============================================================================
 *                          DEBUG CONFIGURATION
 *  Routes through SERIAL_PRINTF (mutex-safe on dual-core).
 *  Raw Serial.printf from RTC task context causes Modbus RX corruption.
 *============================================================================*/

/** @brief Enable debug Serial prints: 1=enabled, 0=disabled */
#define RTC_DEBUG_ENABLE                    (1U)

#if (RTC_DEBUG_ENABLE == 1U)
    /**
     * @brief Mutex-safe RTC debug print.
     * @note  MISRA C:2012 Rule 20.10 advisory deviation: variadic macro.
     *        Rationale: no compliant alternative for printf-style debug.
     */
    #define RTC_DEBUG_PRINT(...)            SERIAL_PRINTF(__VA_ARGS__)
#else
    #define RTC_DEBUG_PRINT(...)            do {} while(0)
#endif

#endif /* RTC_CFG_H */