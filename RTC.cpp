/**
 * @file RTC.cpp
 * @brief General-purpose DS1307 RTC Driver Implementation
 *
 * To port to your platform, implement the three HAL functions
 * at the bottom of this file:
 *   - RTC_HAL_I2C_Write()
 *   - RTC_HAL_I2C_Read()
 *   - RTC_HAL_DelayMs()
 *
 * DS1307 Register Map:
 *   0x00 - Seconds   (bit7 = CH clock halt)
 *   0x01 - Minutes
 *   0x02 - Hours     (bit6 = 12/24hr mode, 0=24hr)
 *   0x03 - Day       (1-7)
 *   0x04 - Date      (1-31)
 *   0x05 - Month     (1-12)
 *   0x06 - Year      (0-99, offset from 2000)
 *   0x07 - Control
 *   0x08-0x3F - 56 bytes NVRAM
 */

#include "RTC.h"
#include "RTC_Cfg.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/*==============================================================================
 *                          DEFINES
 *============================================================================*/

/* DS1307 register addresses */
#define RTC_REG_SECONDS             (0x00U)
#define RTC_REG_MINUTES             (0x01U)
#define RTC_REG_HOURS               (0x02U)
#define RTC_REG_DAY                 (0x03U)
#define RTC_REG_DATE                (0x04U)
#define RTC_REG_MONTH               (0x05U)
#define RTC_REG_YEAR                (0x06U)
#define RTC_REG_CONTROL             (0x07U)

#define RTC_NUM_TIME_REGS           (7U)

/* Bit masks */
#define RTC_CH_BIT                  (0x80U)   /* Clock Halt bit in seconds reg  */
#define RTC_12HR_BIT                (0x40U)   /* 12/24hr mode bit in hours reg  */
#define RTC_PM_BIT                  (0x20U)   /* PM bit in 12hr mode            */
#define RTC_HOUR_12_MASK            (0x1FU)   /* Hour mask for 12hr mode        */
#define RTC_HOUR_24_MASK            (0x3FU)   /* Hour mask for 24hr mode        */

/* Use values from RTC_Cfg.h */
#define RTC_YEAR_BASE               RTC_YEAR_OFFSET

/* Days per month (non-leap year) */
static const uint8_t RTC_DaysInMonth[13] =
{
    0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*==============================================================================
 *                          STATIC VARIABLES
 *============================================================================*/

static bool s_initialized = false;

/*==============================================================================
 *                          STATIC FUNCTION PROTOTYPES
 *============================================================================*/

static uint8_t  RTC_ToBCD(uint8_t val);
static uint8_t  RTC_FromBCD(uint8_t bcd);
static bool     RTC_IsLeapYear(uint16_t year);
static uint8_t  RTC_DaysInMonthVal(uint8_t month, uint16_t year);
static uint16_t RTC_DayOfYear(uint8_t date, uint8_t month, uint16_t year);

/*==============================================================================
 *                          INITIALIZATION APIs
 *============================================================================*/

int8_t RTC__Init(void)
{
    uint8_t reg = 0U;

    /* Attempt to read seconds register to verify communication */
    if (!RTC_HAL_I2C_Read(RTC_I2C_ADDR, RTC_REG_SECONDS, &reg, 1U))
    {
        return RTC_ERR_I2C;
    }

    /* If CH bit is set, oscillator is halted */
#if (RTC_AUTO_START_OSCILLATOR == 1U)
    if ((reg & RTC_CH_BIT) != 0U)
    {
        reg &= ~RTC_CH_BIT;
        if (!RTC_HAL_I2C_Write(RTC_I2C_ADDR, RTC_REG_SECONDS, &reg, 1U))
        {
            return RTC_ERR_I2C;
        }
        RTC_HAL_DelayMs(10U);
        RTC_DEBUG_PRINT("[RTC] Oscillator was halted — restarted\n");
    }
#endif

    /* Ensure 24-hour mode */
#if (RTC_FORCE_24HR_MODE == 1U)
    uint8_t hours = 0U;
    if (!RTC_HAL_I2C_Read(RTC_I2C_ADDR, RTC_REG_HOURS, &hours, 1U))
    {
        return RTC_ERR_I2C;
    }

    if ((hours & RTC_12HR_BIT) != 0U)
    {
        /* Convert from 12hr to 24hr */
        uint8_t h = RTC_FromBCD(hours & RTC_HOUR_12_MASK);
        if ((hours & RTC_PM_BIT) != 0U)
        {
            if (h != 12U) h += 12U;
        }
        else
        {
            if (h == 12U) h = 0U;
        }
        hours = RTC_ToBCD(h);
        if (!RTC_HAL_I2C_Write(RTC_I2C_ADDR, RTC_REG_HOURS, &hours, 1U))
        {
            return RTC_ERR_I2C;
        }
        RTC_DEBUG_PRINT("[RTC] Converted to 24hr mode\n");
    }
#endif /* RTC_FORCE_24HR_MODE */

    /* Enable 1Hz SQW output on DS1307 control register (0x07)
     * SQWE=1 (bit4), RS1=0 RS0=0 → 1Hz square wave on SQW pin */
#if (RTC_SQW_1HZ_ENABLE == 1U)
    {
        uint8_t ctrl = 0x10U;  /* SQWE=1, RS1=0, RS0=0 → 1Hz */
        if (!RTC_HAL_I2C_Write(RTC_I2C_ADDR, RTC_REG_CONTROL, &ctrl, 1U))
        {
            return RTC_ERR_I2C;
        }
        RTC_DEBUG_PRINT("[RTC] SQW 1Hz output enabled\n");
    }
#endif

    RTC_DEBUG_PRINT("[RTC] Init OK\n");
    s_initialized = true;
    return RTC_OK;
}

void RTC__DeInit(void)
{
    s_initialized = false;
}

bool RTC__IsRunning(void)
{
    uint8_t reg = 0U;

    if (!RTC_HAL_I2C_Read(RTC_I2C_ADDR, RTC_REG_SECONDS, &reg, 1U))
    {
        return false;
    }

    return ((reg & RTC_CH_BIT) == 0U);
}

/*==============================================================================
 *                          TIME CONFIGURATION APIs
 *============================================================================*/

int8_t RTC_SetDateTime(uint8_t  seconds,
                       uint8_t  minutes,
                       uint8_t  hours,
                       uint8_t  day,
                       uint8_t  date,
                       uint8_t  month,
                       uint16_t year)
{
    RTC_DateTime_t dt;

    dt.seconds = seconds;
    dt.minutes = minutes;
    dt.hours   = hours;
    dt.day     = day;
    dt.date    = date;
    dt.month   = month;
    dt.year    = year;

    return RTC_SetDateTimeStruct(&dt);
}

int8_t RTC_SetDateTimeStruct(const RTC_DateTime_t *p_dt)
{
    uint8_t regs[RTC_NUM_TIME_REGS];

    if (p_dt == NULL)
    {
        return RTC_ERR_INVALID_PARAM;
    }

    if (!RTC_ValidateDateTime(p_dt))
    {
        return RTC_ERR_INVALID_PARAM;
    }

    /* Pack registers — CH bit cleared to ensure oscillator runs */
    regs[0] = RTC_ToBCD(p_dt->seconds) & ~RTC_CH_BIT;
    regs[1] = RTC_ToBCD(p_dt->minutes);
    regs[2] = RTC_ToBCD(p_dt->hours) & RTC_HOUR_24_MASK;  /* 24hr mode */
    regs[3] = RTC_ToBCD(p_dt->day);
    regs[4] = RTC_ToBCD(p_dt->date);
    regs[5] = RTC_ToBCD(p_dt->month);
    regs[6] = RTC_ToBCD((uint8_t)(p_dt->year - RTC_YEAR_BASE));

    if (!RTC_HAL_I2C_Write(RTC_I2C_ADDR, RTC_REG_SECONDS, regs, RTC_NUM_TIME_REGS))
    {
        return RTC_ERR_I2C;
    }

    return RTC_OK;
}

int8_t RTC_SetUnixTime(uint32_t unix_time)
{
    RTC_DateTime_t dt;
    uint32_t       t;
    uint16_t       days;
    uint16_t       year;
    uint8_t        month;

    /* Convert unix_time to seconds since 2000-01-01 */
    if (unix_time < RTC_UNIX_EPOCH_2000)
    {
        return RTC_ERR_INVALID_PARAM;
    }

    t = unix_time - RTC_UNIX_EPOCH_2000;

    /* Extract time */
    dt.seconds = (uint8_t)(t % 60U);
    t /= 60U;
    dt.minutes = (uint8_t)(t % 60U);
    t /= 60U;
    dt.hours = (uint8_t)(t % 24U);
    t /= 24U;

    /* t now = days since 2000-01-01 */
    days = (uint16_t)t;

    /* Find year */
    year = 2000U;
    while (true)
    {
        uint16_t days_in_year = RTC_IsLeapYear(year) ? 366U : 365U;
        if (days < days_in_year) break;
        days -= days_in_year;
        year++;
        if (year > 2099U) return RTC_ERR_INVALID_PARAM;
    }
    dt.year = year;

    /* Find month */
    month = 1U;
    while (month <= 12U)
    {
        uint8_t dim = RTC_DaysInMonthVal(month, year);
        if (days < (uint16_t)dim) break;
        days -= (uint16_t)dim;
        month++;
    }
    dt.month = month;
    dt.date  = (uint8_t)(days + 1U);
    dt.day   = RTC_GetDayOfWeek(dt.date, dt.month, dt.year);

    return RTC_SetDateTimeStruct(&dt);
}

/*==============================================================================
 *                          TIME READ APIs
 *============================================================================*/

int8_t RTC_GetDateTime(RTC_DateTime_t *p_dt)
{
    uint8_t regs[RTC_NUM_TIME_REGS];

    if (p_dt == NULL)
    {
        return RTC_ERR_INVALID_PARAM;
    }

    if (!RTC_HAL_I2C_Read(RTC_I2C_ADDR, RTC_REG_SECONDS, regs, RTC_NUM_TIME_REGS))
    {
        return RTC_ERR_I2C;
    }

    /* Check oscillator halted */
    if ((regs[0] & RTC_CH_BIT) != 0U)
    {
        return RTC_ERR_NOT_RUNNING;
    }

    p_dt->seconds = RTC_FromBCD(regs[0] & 0x7FU);
    p_dt->minutes = RTC_FromBCD(regs[1] & 0x7FU);
    p_dt->hours   = RTC_FromBCD(regs[2] & RTC_HOUR_24_MASK);
    p_dt->day     = RTC_FromBCD(regs[3] & 0x07U);
    p_dt->date    = RTC_FromBCD(regs[4] & 0x3FU);
    p_dt->month   = RTC_FromBCD(regs[5] & 0x1FU);
    p_dt->year    = (uint16_t)RTC_FromBCD(regs[6]) + RTC_YEAR_BASE;

    return RTC_OK;
}

int8_t RTC_GetUnixTime(uint32_t *p_unix_time)
{
    RTC_DateTime_t dt;
    int8_t         result;
    uint32_t       days = 0UL;
    uint16_t       y;
    uint8_t        m;

    if (p_unix_time == NULL)
    {
        return RTC_ERR_INVALID_PARAM;
    }

    result = RTC_GetDateTime(&dt);
    if (result != RTC_OK)
    {
        return result;
    }

    /* Count days from 2000-01-01 to dt.year-1 */
    for (y = 2000U; y < dt.year; y++)
    {
        days += RTC_IsLeapYear(y) ? 366U : 365U;
    }

    /* Add days for each completed month in current year */
    for (m = 1U; m < dt.month; m++)
    {
        days += (uint32_t)RTC_DaysInMonthVal(m, dt.year);
    }

    /* Add days in current month (1-based) */
    days += (uint32_t)(dt.date - 1U);

    *p_unix_time = RTC_UNIX_EPOCH_2000
                 + (days * 86400UL)
                 + ((uint32_t)dt.hours   * 3600UL)
                 + ((uint32_t)dt.minutes * 60UL)
                 +  (uint32_t)dt.seconds;

    return RTC_OK;
}

int8_t RTC_GetTimestamp(char *p_buf, uint8_t buf_len)
{
    RTC_DateTime_t dt;
    int8_t         result;

    if ((p_buf == NULL) || (buf_len < 20U))
    {
        return RTC_ERR_INVALID_PARAM;
    }

    result = RTC_GetDateTime(&dt);
    if (result != RTC_OK)
    {
        return result;
    }

    snprintf(p_buf, buf_len,
             "%04u-%02u-%02u %02u:%02u:%02u",
             dt.year, dt.month, dt.date,
             dt.hours, dt.minutes, dt.seconds);

    return RTC_OK;
}

int8_t RTC_GetDateString(char *p_buf, uint8_t buf_len)
{
    RTC_DateTime_t dt;
    int8_t         result;

    if ((p_buf == NULL) || (buf_len < 11U))
    {
        return RTC_ERR_INVALID_PARAM;
    }

    result = RTC_GetDateTime(&dt);
    if (result != RTC_OK)
    {
        return result;
    }

    snprintf(p_buf, buf_len,
             "%04u-%02u-%02u",
             dt.year, dt.month, dt.date);

    return RTC_OK;
}

int8_t RTC_GetTimeString(char *p_buf, uint8_t buf_len)
{
    RTC_DateTime_t dt;
    int8_t         result;

    if ((p_buf == NULL) || (buf_len < 9U))
    {
        return RTC_ERR_INVALID_PARAM;
    }

    result = RTC_GetDateTime(&dt);
    if (result != RTC_OK)
    {
        return result;
    }

    snprintf(p_buf, buf_len,
             "%02u:%02u:%02u",
             dt.hours, dt.minutes, dt.seconds);

    return RTC_OK;
}

/*==============================================================================
 *                          UTILITY APIs
 *============================================================================*/

uint8_t RTC_GetDayOfWeek(uint8_t date, uint8_t month, uint16_t year)
{
    /* Tomohiko Sakamoto's algorithm — returns 0=Sunday, adjusted to 1=Mon..7=Sun */
    static const uint8_t t[] = {0U, 3U, 2U, 5U, 0U, 3U, 5U, 1U, 4U, 6U, 2U, 4U};
    uint16_t y = year;
    uint8_t  dow;

    if (month < 3U)
    {
        y--;
    }

    dow = (uint8_t)((y + y/4U - y/100U + y/400U + t[month - 1U] + date) % 7U);

    /* Convert: 0=Sunday → return 7, 1=Monday → return 1 ... 6=Saturday → return 6 */
    return (dow == 0U) ? 7U : dow;
}

bool RTC_ValidateDateTime(const RTC_DateTime_t *p_dt)
{
    if (p_dt == NULL)                               return false;
    if (p_dt->seconds > 59U)                        return false;
    if (p_dt->minutes > 59U)                        return false;
    if (p_dt->hours   > 23U)                        return false;
    if ((p_dt->day < 1U) || (p_dt->day > 7U))       return false;
    if ((p_dt->date < 1U) || (p_dt->date > 31U))    return false;
    if ((p_dt->month < 1U) || (p_dt->month > 12U))  return false;
    if ((p_dt->year < 2000U) || (p_dt->year > 2099U)) return false;

    /* Check date does not exceed days in that month */
    if (p_dt->date > RTC_DaysInMonthVal(p_dt->month, p_dt->year))
    {
        return false;
    }

    return true;
}

bool RTC_IsBatteryLow(void)
{
    /*
     * DS1307 has no dedicated battery-low flag.
     * A halted oscillator (CH bit set) typically means power was lost,
     * which implies the backup battery is dead or missing.
     */
    return !RTC__IsRunning();
}

int8_t RTC_Reset(void)
{
    return RTC_SetDateTime(0U, 0U, 0U, RTC_MONDAY, 1U, 1U, 2000U);
}

/*==============================================================================
 *                          STATIC HELPER FUNCTIONS
 *============================================================================*/

static uint8_t RTC_ToBCD(uint8_t val)
{
    return (uint8_t)(((val / 10U) << 4U) | (val % 10U));
}

static uint8_t RTC_FromBCD(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4U) * 10U) + (bcd & 0x0FU));
}

static bool RTC_IsLeapYear(uint16_t year)
{
    return (((year % 4U) == 0U) && (((year % 100U) != 0U) || ((year % 400U) == 0U)));
}

static uint8_t RTC_DaysInMonthVal(uint8_t month, uint16_t year)
{
    if ((month == 2U) && RTC_IsLeapYear(year))
    {
        return 29U;
    }
    return RTC_DaysInMonth[month];
}

static uint16_t RTC_DayOfYear(uint8_t date, uint8_t month, uint16_t year)
{
    uint16_t doy = 0U;
    uint8_t  m;

    for (m = 1U; m < month; m++)
    {
        doy += (uint16_t)RTC_DaysInMonthVal(m, year);
    }
    doy += (uint16_t)date;

    return doy;
}

/*==============================================================================
 *                          HAL IMPLEMENTATION
 *
 *  Implement the three functions below for your target platform.
 *  Examples are provided for Arduino (Wire) and STM32 HAL.
 *  Uncomment the block that matches your platform.
 *============================================================================*/

/* ── ARDUINO (Wire) ──────────────────────────────────────────────────────── */
#if defined(ARDUINO)
#include <Wire.h>

bool RTC_HAL_I2C_Write(uint8_t dev_addr, uint8_t reg_addr,
                       const uint8_t *p_data, uint8_t len)
{
    Wire.beginTransmission(dev_addr);
    Wire.write(reg_addr);
    for (uint8_t i = 0U; i < len; i++)
    {
        Wire.write(p_data[i]);
    }
    return (Wire.endTransmission() == 0);
}

bool RTC_HAL_I2C_Read(uint8_t dev_addr, uint8_t reg_addr,
                      uint8_t *p_data, uint8_t len)
{
    Wire.beginTransmission(dev_addr);
    Wire.write(reg_addr);
    if (Wire.endTransmission(false) != 0) return false;

    Wire.requestFrom((uint8_t)dev_addr, len);
    for (uint8_t i = 0U; i < len; i++)
    {
        if (!Wire.available()) return false;
        p_data[i] = (uint8_t)Wire.read();
    }
    return true;
}

void RTC_HAL_DelayMs(uint32_t ms)
{
    delay(ms);
}

/* Call this once in setup() before RTC__Init() */
void RTC_HAL_I2C_Begin(void)
{
#if (RTC_I2C_SDA_PIN >= 0) && (RTC_I2C_SCL_PIN >= 0)
    Wire.begin(RTC_I2C_SDA_PIN, RTC_I2C_SCL_PIN);
#else
    Wire.begin();
#endif
    Wire.setClock(RTC_I2C_FREQ_HZ);
}

/* ── STM32 HAL ───────────────────────────────────────────────────────────── */
#elif defined(USE_HAL_DRIVER)
extern I2C_HandleTypeDef RTC_STM32_I2C_HANDLE;

bool RTC_HAL_I2C_Write(uint8_t dev_addr, uint8_t reg_addr,
                       const uint8_t *p_data, uint8_t len)
{
    uint8_t buf[32];
    if (len > 31U) return false;
    buf[0] = reg_addr;
    memcpy(&buf[1], p_data, len);
    return (HAL_I2C_Master_Transmit(&RTC_STM32_I2C_HANDLE,
                                    (uint16_t)(dev_addr << 1U),
                                    buf, (uint16_t)(len + 1U),
                                    RTC_STM32_I2C_TIMEOUT_MS) == HAL_OK);
}

bool RTC_HAL_I2C_Read(uint8_t dev_addr, uint8_t reg_addr,
                      uint8_t *p_data, uint8_t len)
{
    if (HAL_I2C_Master_Transmit(&RTC_STM32_I2C_HANDLE,
                                (uint16_t)(dev_addr << 1U),
                                &reg_addr, 1U,
                                RTC_STM32_I2C_TIMEOUT_MS) != HAL_OK) return false;
    return (HAL_I2C_Master_Receive(&RTC_STM32_I2C_HANDLE,
                                   (uint16_t)(dev_addr << 1U),
                                   p_data, (uint16_t)len,
                                   RTC_STM32_I2C_TIMEOUT_MS) == HAL_OK);
}

void RTC_HAL_DelayMs(uint32_t ms)
{
    HAL_Delay(ms);
}

/* ── OTHER PLATFORM ──────────────────────────────────────────────────────── */
#else
/*
 * Implement these three functions for your platform:
 *
 * bool RTC_HAL_I2C_Write(uint8_t dev_addr, uint8_t reg_addr,
 *                        const uint8_t *p_data, uint8_t len) { ... }
 *
 * bool RTC_HAL_I2C_Read(uint8_t dev_addr, uint8_t reg_addr,
 *                       uint8_t *p_data, uint8_t len) { ... }
 *
 * void RTC_HAL_DelayMs(uint32_t ms) { ... }
 */
#error "RTC HAL not implemented for this platform. See RTC.cpp HAL section."
#endif