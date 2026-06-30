/**
 * @file RTC.h
 * @brief PCF8563T RTC Driver Interface
 *
 * Platform-independent driver for the NXP PCF8563T real-time clock.
 * I2C address: 0x51 (hardware fixed).
 *
 * To port to a new platform, implement the HAL functions at the
 * bottom of RTC.cpp:
 *   - RTC_HAL_I2C_Write()
 *   - RTC_HAL_I2C_Read()
 *   - RTC_HAL_DelayMs()
 *   - RTC_HAL_I2C_Begin()
 *
 * @note All time values use 24-hour format.
 * @note The PCF8563T has a Voltage Low (VL) flag in the seconds register
 *       that is set when the supply voltage drops below the oscillator
 *       operating level. Check RTC_IsBatteryLow() after RTC__Init().
 *
 * @version 1.1.0
 *
 * v1.1.0 changes (review fixes):
 *   - Header updated from DS1307 to PCF8563T -- was describing wrong chip,
 *     wrong I2C address (0x68 vs 0x51), wrong battery-low mechanism.
 *   - RTC_IsBatteryLow() documentation corrected: PCF8563T DOES have a
 *     dedicated VL flag (bit 7 of seconds register) -- opposite of DS1307.
 */

#ifndef RTC_H
#define RTC_H

#include <stdint.h>
#include <stdbool.h>
#include "RTC_Cfg.h"

/*==============================================================================
 *                          DEFINES
 *============================================================================*/

/* Return codes */
#define RTC_OK                      (0)
#define RTC_ERR_I2C                 (-1)
#define RTC_ERR_INVALID_PARAM       (-2)
#define RTC_ERR_NOT_RUNNING         (-3)

/* Day of week definitions (1=Monday .. 7=Sunday) */
#define RTC_MONDAY                  (1U)
#define RTC_TUESDAY                 (2U)
#define RTC_WEDNESDAY               (3U)
#define RTC_THURSDAY                (4U)
#define RTC_FRIDAY                  (5U)
#define RTC_SATURDAY                (6U)
#define RTC_SUNDAY                  (7U)

/*==============================================================================
 *                          DATA STRUCTURES
 *============================================================================*/

/**
 * @brief Date and time structure
 */
typedef struct
{
    uint8_t  seconds;   /**< 0-59                              */
    uint8_t  minutes;   /**< 0-59                              */
    uint8_t  hours;     /**< 0-23 (24-hour format always)      */
    uint8_t  day;       /**< 1-7  (1=Monday ... 7=Sunday)      */
    uint8_t  date;      /**< 1-31                              */
    uint8_t  month;     /**< 1-12                              */
    uint16_t year;      /**< 2000-2099                         */
} RTC_DateTime_t;

/*==============================================================================
 *                          INITIALIZATION APIs
 *============================================================================*/

/**
 * @brief Initialize the RTC module and I2C peripheral.
 * @details Clears STOP bit to start oscillator, disables alarms/timer,
 *          and checks the VL (Voltage Low) flag.
 * @return RTC_OK on success, RTC_ERR_I2C on I2C failure.
 */
int8_t RTC__Init(void);

/**
 * @brief De-initialize the RTC module.
 */
void RTC__DeInit(void);

/**
 * @brief Check if RTC oscillator is running.
 * @return true if running (STOP bit clear), false if halted.
 */
bool RTC__IsRunning(void);

/*==============================================================================
 *                          TIME CONFIGURATION APIs
 *============================================================================*/

/**
 * @brief Set date and time using individual parameters.
 * @param seconds  0-59
 * @param minutes  0-59
 * @param hours    0-23
 * @param day      1-7 (1=Monday)
 * @param date     1-31
 * @param month    1-12
 * @param year     2000-2099
 * @return RTC_OK on success, error code on failure.
 */
int8_t RTC_SetDateTime(uint8_t  seconds,
                       uint8_t  minutes,
                       uint8_t  hours,
                       uint8_t  day,
                       uint8_t  date,
                       uint8_t  month,
                       uint16_t year);

/**
 * @brief Set date and time using a struct.
 * @param p_dt Pointer to populated RTC_DateTime_t.
 * @return RTC_OK on success, error code on failure.
 */
int8_t RTC_SetDateTimeStruct(const RTC_DateTime_t *p_dt);

/**
 * @brief Set date and time from Unix timestamp (seconds since 1970-01-01).
 * @param unix_time Unix timestamp. Must be >= RTC_UNIX_EPOCH_2000.
 * @return RTC_OK on success, RTC_ERR_INVALID_PARAM if out of supported range.
 */
int8_t RTC_SetUnixTime(uint32_t unix_time);

/*==============================================================================
 *                          TIME READ APIs
 *============================================================================*/

/**
 * @brief Read current date and time into struct.
 * @param p_dt Pointer to RTC_DateTime_t to fill.
 * @return RTC_OK on success, error code on failure.
 */
int8_t RTC_GetDateTime(RTC_DateTime_t *p_dt);

/**
 * @brief Get current time as Unix timestamp.
 * @param p_unix_time Pointer to store result.
 * @return RTC_OK on success, error code on failure.
 */
int8_t RTC_GetUnixTime(uint32_t *p_unix_time);

/**
 * @brief Get formatted timestamp string "YYYY-MM-DD HH:MM:SS".
 * @param p_buf   Buffer to write into (minimum 21 bytes recommended).
 * @param buf_len Length of buffer (must be >= 20).
 * @return RTC_OK on success, error code on failure.
 */
int8_t RTC_GetTimestamp(char *p_buf, uint8_t buf_len);

/**
 * @brief Get formatted date string "YYYY-MM-DD".
 * @param p_buf   Buffer to write into (minimum 11 bytes).
 * @param buf_len Length of buffer (must be >= 11).
 * @return RTC_OK on success, error code on failure.
 */
int8_t RTC_GetDateString(char *p_buf, uint8_t buf_len);

/**
 * @brief Get formatted time string "HH:MM:SS".
 * @param p_buf   Buffer to write into (minimum 9 bytes).
 * @param buf_len Length of buffer (must be >= 9).
 * @return RTC_OK on success, error code on failure.
 */
int8_t RTC_GetTimeString(char *p_buf, uint8_t buf_len);

/*==============================================================================
 *                          UTILITY APIs
 *============================================================================*/

/**
 * @brief Get day of week for a given date (Zeller's congruence).
 * @param date  Day of month (1-31).
 * @param month Month (1-12).
 * @param year  Full year (e.g. 2025).
 * @return Day index: 1=Monday ... 7=Sunday.
 */
uint8_t RTC_GetDayOfWeek(uint8_t date, uint8_t month, uint16_t year);

/**
 * @brief Validate a date/time struct for range correctness.
 * @param p_dt Pointer to RTC_DateTime_t to validate.
 * @return true if all fields are within valid ranges.
 */
bool RTC_ValidateDateTime(const RTC_DateTime_t *p_dt);

/**
 * @brief Check if RTC backup battery is low (VL flag).
 * @details The PCF8563T sets the Voltage Low (VL) flag in bit 7 of the
 *          seconds register when VDD drops below the oscillator threshold.
 *          When set, the time may be invalid -- call RTC_SetDateTime() to
 *          restore accurate time and clear the flag.
 * @return true if VL flag is set (battery low or power was lost).
 */
bool RTC_IsBatteryLow(void);

/**
 * @brief Reset RTC to 2000-01-01 00:00:00 Saturday and restart oscillator.
 * @return RTC_OK on success, error code on failure.
 */
int8_t RTC_Reset(void);

/*==============================================================================
 *                          HAL INTERFACE
 *  Implement these functions in RTC.cpp for your platform.
 *============================================================================*/

/**
 * @brief Initialize I2C bus for RTC.
 * @details Arduino platform: uses RTC_I2C_SDA_PIN / RTC_I2C_SCL_PIN from
 *          RTC_Cfg.h. Call before RTC__Init().
 */
void RTC_HAL_I2C_Begin(void);

/**
 * @brief Write bytes to I2C device.
 * @param dev_addr  7-bit I2C device address.
 * @param reg_addr  Register address to write to.
 * @param p_data    Pointer to data buffer.
 * @param len       Number of bytes to write.
 * @return true on success, false on failure.
 */
bool RTC_HAL_I2C_Write(uint8_t dev_addr, uint8_t reg_addr,
                       const uint8_t *p_data, uint8_t len);

/**
 * @brief Read bytes from I2C device.
 * @param dev_addr  7-bit I2C device address.
 * @param reg_addr  Register address to read from.
 * @param p_data    Pointer to buffer to fill.
 * @param len       Number of bytes to read.
 * @return true on success, false on failure.
 */
bool RTC_HAL_I2C_Read(uint8_t dev_addr, uint8_t reg_addr,
                      uint8_t *p_data, uint8_t len);

/**
 * @brief Blocking delay.
 * @param ms Milliseconds to wait.
 */
void RTC_HAL_DelayMs(uint32_t ms);

#endif /* RTC_H */