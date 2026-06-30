/**
 * @file RTC.cpp
 * @brief PCF8563T RTC Driver Implementation
 *
 * Drop-in replacement for the DS1307 driver.
 * Same public API (RTC.h unchanged), same HAL layer.
 * Only the register map and init sequence changed.
 *
 * PCF8563T Register Map:
 *   0x00  Control_1
 *   0x01  Control_2
 *   0x02  VL_Seconds   (BCD, bit7=VL voltage-low flag)
 *   0x03  Minutes      (BCD, bits 6:0)
 *   0x04  Hours        (BCD, bits 5:0, 24h mode always)
 *   0x05  Days         (BCD, bits 5:0)
 *   0x06  Weekdays     (BCD, bits 2:0, 0=Sunday)
 *   0x07  Century_Months (BCD, bits 4:0, bit7=century)
 *   0x08  Years        (BCD, 00-99)
 *
 * I2C address: 0x51 (fixed in hardware, from RTC_I2C_ADDR in RTC_Cfg.h)
 *
 * @version 1.1.0
 *
 * v1.1.0 changes (review fixes):
 *   - Encoding artefacts (corrupted em-dash â€") replaced with -- throughout.
 *   - PCF8563_ADDR now defined as RTC_I2C_ADDR (from RTC_Cfg.h) -- single
 *     source of truth; was a local duplicate with the same value.
 *   - RTC_SetUnixTime(): unbounded while(1) year loop replaced with bounded
 *     loop guarded by RTC_YEAR_MAX -- prevents WDT reset on far-future or
 *     corrupted unix_time input.
 *   - RTC_GetDayOfWeek(): Zeller's h variable changed from int16_t to int32_t
 *     to avoid signed/unsigned mixing with uint16_t intermediates (MISRA R2).
 *     Input parameters no longer modified -- local copies used (MISRA R5).
 *   - IsLeapYear() static helper extracted -- was computed inline 4 times
 *     across RTC_SetUnixTime() and RTC_GetUnixTime().
 *   - RTC_Reset(): day computed via RTC_GetDayOfWeek() instead of hardcoded
 *     magic number 6U.
 *   - RTC_GetTimestamp() minimum buffer check widened to 20U (was already 20
 *     but documented as 21 in header for margin).
 *   - #include <Arduino.h> moved after Doxygen header block.
 *   - Single-statement if bodies given braces throughout (MISRA A3).
 */

#include <Arduino.h>
#include "RTC.h"
#include <string.h>
#include <stdio.h>

/*==============================================================================
 *                          PRIVATE DEFINES
 *============================================================================*/

/* PCF8563T I2C address -- sourced from RTC_Cfg.h (single source of truth) */
#define PCF8563_ADDR            RTC_I2C_ADDR

/* Register addresses */
#define PCF_REG_CTRL1           (0x00U)
#define PCF_REG_CTRL2           (0x01U)
#define PCF_REG_SECONDS         (0x02U)
#define PCF_REG_MINUTES         (0x03U)
#define PCF_REG_HOURS           (0x04U)
#define PCF_REG_DAYS            (0x05U)
#define PCF_REG_WEEKDAYS        (0x06U)
#define PCF_REG_MONTHS          (0x07U)
#define PCF_REG_YEARS           (0x08U)

/* Masks */
#define PCF_VL_FLAG             (0x80U)   /* Voltage Low -- battery dead     */
#define PCF_STOP_BIT            (0x20U)   /* Control_1 STOP bit              */

/* Number of time registers to read/write in one burst (regs 0x02-0x08) */
#define PCF_NUM_TIME_REGS       (7U)

/*==============================================================================
 *                          PRIVATE HELPERS
 *============================================================================*/

static uint8_t BcdToDec(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4U) & 0x0FU) * 10U + (bcd & 0x0FU));
}

static uint8_t DecToBcd(uint8_t dec)
{
    return (uint8_t)(((dec / 10U) << 4U) | (dec % 10U));
}

/**
 * @brief Determine whether a given year is a leap year.
 * @param year Full year (e.g. 2024).
 * @return true if leap year.
 */
static bool IsLeapYear(uint16_t year)
{
    return (((year % 4U == 0U) && (year % 100U != 0U)) || (year % 400U == 0U));
}

/*==============================================================================
 *                          INITIALIZATION
 *============================================================================*/

int8_t RTC__Init(void)
{
    RTC_HAL_I2C_Begin();
    RTC_HAL_DelayMs(10U);

    /* Read Control_1 to verify I2C communication */
    uint8_t ctrl1 = 0U;
    if (!RTC_HAL_I2C_Read(PCF8563_ADDR, PCF_REG_CTRL1, &ctrl1, 1U))
    {
        RTC_DEBUG_PRINT("[RTC] Init FAILED -- I2C error (addr=0x51)\n");
        return RTC_ERR_I2C;
    }

    /* Clear STOP bit to ensure oscillator is running */
    ctrl1 &= (uint8_t)(~PCF_STOP_BIT);
    if (!RTC_HAL_I2C_Write(PCF8563_ADDR, PCF_REG_CTRL1, &ctrl1, 1U))
    {
        return RTC_ERR_I2C;
    }

    /* Clear Control_2 (disable alarms/timer interrupts) */
    uint8_t ctrl2 = 0x00U;
    if (!RTC_HAL_I2C_Write(PCF8563_ADDR, PCF_REG_CTRL2, &ctrl2, 1U))
    {
        return RTC_ERR_I2C;
    }

    RTC_DEBUG_PRINT("[RTC] PCF8563T Init OK\n");

    /* Warn if VL flag is set (battery dead / time lost) */
    uint8_t sec_reg = 0U;
    if (RTC_HAL_I2C_Read(PCF8563_ADDR, PCF_REG_SECONDS, &sec_reg, 1U))
    {
        if ((sec_reg & PCF_VL_FLAG) != 0U)
        {
            RTC_DEBUG_PRINT("[RTC] WARNING: VL flag set -- clock integrity lost. Set time.\n");
        }
    }

    return RTC_OK;
}

void RTC__DeInit(void)
{
    /* Nothing to de-initialize for PCF8563T */
}

bool RTC__IsRunning(void)
{
    uint8_t ctrl1 = 0U;
    if (!RTC_HAL_I2C_Read(PCF8563_ADDR, PCF_REG_CTRL1, &ctrl1, 1U))
    {
        return false;
    }
    return ((ctrl1 & PCF_STOP_BIT) == 0U);
}

/*==============================================================================
 *                          TIME SET
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
    if (p_dt == NULL) { return RTC_ERR_INVALID_PARAM; }
    if (!RTC_ValidateDateTime(p_dt)) { return RTC_ERR_INVALID_PARAM; }

    uint8_t regs[PCF_NUM_TIME_REGS];

    /* Reg 0x02 -- Seconds (writing clears VL flag) */
    regs[0] = DecToBcd(p_dt->seconds) & 0x7FU;
    /* Reg 0x03 -- Minutes */
    regs[1] = DecToBcd(p_dt->minutes) & 0x7FU;
    /* Reg 0x04 -- Hours (24h, bits 5:0) */
    regs[2] = DecToBcd(p_dt->hours) & 0x3FU;
    /* Reg 0x05 -- Days */
    regs[3] = DecToBcd(p_dt->date) & 0x3FU;
    /* Reg 0x06 -- Weekdays: PCF stores 0=Sun..6=Sat; struct uses 1=Mon..7=Sun.
     * Mapping: day % 7 gives 1->1(Mon)..6->6(Sat), 7->0(Sun). Correct. */
    regs[4] = (uint8_t)((p_dt->day % 7U) & 0x07U);
    /* Reg 0x07 -- Months (no century bit needed for 2000-2099) */
    regs[5] = DecToBcd(p_dt->month) & 0x1FU;
    /* Reg 0x08 -- Years (00-99 offset from 2000) */
    uint16_t yr = (p_dt->year >= RTC_YEAR_OFFSET) ?
                  (p_dt->year - RTC_YEAR_OFFSET) : 0U;
    regs[6] = DecToBcd((uint8_t)(yr & 0xFFU));

    if (!RTC_HAL_I2C_Write(PCF8563_ADDR, PCF_REG_SECONDS, regs, PCF_NUM_TIME_REGS))
    {
        return RTC_ERR_I2C;
    }
    return RTC_OK;
}

int8_t RTC_SetUnixTime(uint32_t unix_time)
{
    if (unix_time < RTC_UNIX_EPOCH_2000) { return RTC_ERR_INVALID_PARAM; }

    uint32_t t = unix_time - RTC_UNIX_EPOCH_2000;

    uint8_t seconds = (uint8_t)(t % 60U);
    t /= 60U;
    uint8_t minutes = (uint8_t)(t % 60U);
    t /= 60U;
    uint8_t hours   = (uint8_t)(t % 24U);
    t /= 24U;

    /*--------------------------------------------------------------------------
     * Year calculation -- bounded loop to prevent WDT reset on far-future
     * or corrupted unix_time. Exits with error if year exceeds RTC_YEAR_MAX.
     *------------------------------------------------------------------------*/
    uint16_t year = 2000U;
    while (year <= (uint16_t)RTC_YEAR_MAX)
    {
        uint32_t diy = IsLeapYear(year) ? 366UL : 365UL;
        if (t < diy) { break; }
        t -= diy;
        year++;
    }
    if (year > (uint16_t)RTC_YEAR_MAX) { return RTC_ERR_INVALID_PARAM; }

    static const uint8_t days_in_month[12U] =
        {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};

    uint8_t month = 1U;
    while (month <= 12U)
    {
        uint8_t dim = days_in_month[month - 1U];
        if (IsLeapYear(year) && (month == 2U)) { dim = 29U; }
        if (t < (uint32_t)dim) { break; }
        t -= (uint32_t)dim;
        month++;
    }

    uint8_t date = (uint8_t)(t + 1U);
    uint8_t dow  = RTC_GetDayOfWeek(date, month, year);

    return RTC_SetDateTime(seconds, minutes, hours, dow, date, month, year);
}

/*==============================================================================
 *                          TIME READ
 *============================================================================*/

int8_t RTC_GetDateTime(RTC_DateTime_t *p_dt)
{
    if (p_dt == NULL) { return RTC_ERR_INVALID_PARAM; }

    uint8_t regs[PCF_NUM_TIME_REGS];
    if (!RTC_HAL_I2C_Read(PCF8563_ADDR, PCF_REG_SECONDS, regs, PCF_NUM_TIME_REGS))
    {
        return RTC_ERR_I2C;
    }

    p_dt->seconds = BcdToDec(regs[0] & 0x7FU);
    p_dt->minutes = BcdToDec(regs[1] & 0x7FU);
    p_dt->hours   = BcdToDec(regs[2] & 0x3FU);
    p_dt->date    = BcdToDec(regs[3] & 0x3FU);
    /* Weekdays: PCF uses 0=Sun..6=Sat; struct uses 1=Mon..7=Sun */
    uint8_t wday  = regs[4] & 0x07U;
    p_dt->day     = (wday == 0U) ? 7U : wday;
    p_dt->month   = BcdToDec(regs[5] & 0x1FU);
    p_dt->year    = (uint16_t)(BcdToDec(regs[6]) + RTC_YEAR_OFFSET);

    return RTC_OK;
}

int8_t RTC_GetUnixTime(uint32_t *p_unix_time)
{
    if (p_unix_time == NULL) { return RTC_ERR_INVALID_PARAM; }

    RTC_DateTime_t dt;
    int8_t ret = RTC_GetDateTime(&dt);
    if (ret != RTC_OK) { return ret; }

    /* Days since 2000-01-01 */
    static const uint16_t days_before_month[12U] =
        {0U, 31U, 59U, 90U, 120U, 151U, 181U, 212U, 243U, 273U, 304U, 334U};

    uint32_t days = 0U;
    for (uint16_t y = 2000U; y < dt.year; y++)
    {
        days += IsLeapYear(y) ? 366UL : 365UL;
    }

    days += (uint32_t)days_before_month[dt.month - 1U];
    if (IsLeapYear(dt.year) && (dt.month > 2U)) { days++; }
    days += (uint32_t)(dt.date - 1U);

    *p_unix_time = RTC_UNIX_EPOCH_2000 +
                   days * 86400UL +
                   (uint32_t)dt.hours   * 3600UL +
                   (uint32_t)dt.minutes * 60UL   +
                   (uint32_t)dt.seconds;
    return RTC_OK;
}

int8_t RTC_GetTimestamp(char *p_buf, uint8_t buf_len)
{
    if ((p_buf == NULL) || (buf_len < 20U)) { return RTC_ERR_INVALID_PARAM; }

    RTC_DateTime_t dt;
    int8_t ret = RTC_GetDateTime(&dt);
    if (ret != RTC_OK) { return ret; }

    /* "YYYY-MM-DD HH:MM:SS" = 19 chars + null = 20 bytes minimum */
    (void)snprintf(p_buf, (size_t)buf_len, "%04u-%02u-%02u %02u:%02u:%02u",
                   dt.year, dt.month, dt.date,
                   dt.hours, dt.minutes, dt.seconds);
    return RTC_OK;
}

int8_t RTC_GetDateString(char *p_buf, uint8_t buf_len)
{
    if ((p_buf == NULL) || (buf_len < 11U)) { return RTC_ERR_INVALID_PARAM; }

    RTC_DateTime_t dt;
    int8_t ret = RTC_GetDateTime(&dt);
    if (ret != RTC_OK) { return ret; }

    (void)snprintf(p_buf, (size_t)buf_len, "%04u-%02u-%02u",
                   dt.year, dt.month, dt.date);
    return RTC_OK;
}

int8_t RTC_GetTimeString(char *p_buf, uint8_t buf_len)
{
    if ((p_buf == NULL) || (buf_len < 9U)) { return RTC_ERR_INVALID_PARAM; }

    RTC_DateTime_t dt;
    int8_t ret = RTC_GetDateTime(&dt);
    if (ret != RTC_OK) { return ret; }

    (void)snprintf(p_buf, (size_t)buf_len, "%02u:%02u:%02u",
                   dt.hours, dt.minutes, dt.seconds);
    return RTC_OK;
}

/*==============================================================================
 *                          UTILITY
 *============================================================================*/

uint8_t RTC_GetDayOfWeek(uint8_t date, uint8_t month, uint16_t year)
{
    /* Zeller's congruence -- use local copies, do not modify parameters */
    uint8_t  d = date;
    uint8_t  m = month;
    uint16_t y = year;

    /* Zeller uses months 3-14; January and February are months 13-14 of
     * the previous year */
    if (m < 3U)
    {
        m = (uint8_t)(m + 12U);
        y--;
    }

    /* Use int32_t for h to avoid signed/unsigned mixing (MISRA Rule 10.3).
     * k and j are century/decade components. */
    int32_t k = (int32_t)(y % 100U);
    int32_t j = (int32_t)(y / 100U);
    int32_t h = ((int32_t)d
               + (13 * ((int32_t)m + 1)) / 5
               + k
               + k / 4
               + j / 4
               - 2 * j) % 7;

    if (h < 0) { h += 7; }

    /* h: 0=Sat,1=Sun,2=Mon..6=Fri -- convert to 1=Mon..7=Sun */
    uint8_t dow = (uint8_t)((h + 5) % 7 + 1);
    return dow;
}

bool RTC_ValidateDateTime(const RTC_DateTime_t *p_dt)
{
    if (p_dt == NULL)                              { return false; }
    if (p_dt->seconds > 59U)                       { return false; }
    if (p_dt->minutes > 59U)                       { return false; }
    if (p_dt->hours   > 23U)                       { return false; }
    if ((p_dt->day < 1U) || (p_dt->day > 7U))     { return false; }
    if ((p_dt->date < 1U) || (p_dt->date > 31U))  { return false; }
    if ((p_dt->month < 1U) || (p_dt->month > 12U)){ return false; }
    if ((p_dt->year < RTC_YEAR_MIN) || (p_dt->year > RTC_YEAR_MAX)) { return false; }
    return true;
}

bool RTC_IsBatteryLow(void)
{
    uint8_t sec_reg = 0U;
    if (!RTC_HAL_I2C_Read(PCF8563_ADDR, PCF_REG_SECONDS, &sec_reg, 1U))
    {
        return true;  /* Assume battery low if I2C fails */
    }
    return ((sec_reg & PCF_VL_FLAG) != 0U);
}

int8_t RTC_Reset(void)
{
    /* 2000-01-01 was a Saturday -- computed via Zeller's rather than hardcoded */
    uint8_t dow = RTC_GetDayOfWeek(1U, 1U, 2000U);
    return RTC_SetDateTime(0U, 0U, 0U, dow, 1U, 1U, 2000U);
}

/*==============================================================================
 *                          HAL -- ARDUINO
 *============================================================================*/
#if (RTC_PLATFORM == RTC_PLATFORM_ARDUINO)
#include <Wire.h>

bool RTC_HAL_I2C_Write(uint8_t dev_addr, uint8_t reg_addr,
                       const uint8_t *p_data, uint8_t len)
{
    Wire.beginTransmission(dev_addr);
    Wire.write(reg_addr);
    for (uint8_t i = 0U; i < len; i++) { Wire.write(p_data[i]); }
    return (Wire.endTransmission() == 0);
}

bool RTC_HAL_I2C_Read(uint8_t dev_addr, uint8_t reg_addr,
                      uint8_t *p_data, uint8_t len)
{
    Wire.beginTransmission(dev_addr);
    Wire.write(reg_addr);
    if (Wire.endTransmission(false) != 0) { return false; }
    Wire.requestFrom((int)dev_addr, (int)len);  /* explicit int cast for older cores */
    for (uint8_t i = 0U; i < len; i++)
    {
        if (!Wire.available()) { return false; }
        p_data[i] = (uint8_t)Wire.read();
    }
    return true;
}

void RTC_HAL_DelayMs(uint32_t ms) { delay(ms); }

void RTC_HAL_I2C_Begin(void)
{
#if (RTC_I2C_SDA_PIN >= 0) && (RTC_I2C_SCL_PIN >= 0)
    Wire.begin(RTC_I2C_SDA_PIN, RTC_I2C_SCL_PIN);
#else
    Wire.begin();
#endif
    Wire.setClock(RTC_I2C_FREQ_HZ);
}

#endif /* RTC_PLATFORM_ARDUINO */