/**
 * @file Fan.cpp
 * @brief Fan Module Implementation — Voltage-based PWM control
 *
 * How it works:
 *   - User calls Fan__SetVoltage(v) with desired voltage 0-5V
 *   - Code converts voltage to duty cycle (0-255)
 *   - Writes to LEDC peripheral
 *
 * Circuit (NPN + P-channel MOSFET):
 *   GPIO HIGH → NPN ON  → Gate LOW  → MOSFET ON  → Fan ON
 *   GPIO LOW  → NPN OFF → Gate HIGH → MOSFET OFF → Fan OFF
 *
 * TACHOMETER (3-wire fan, FAN_TACH_ENABLED=1):
 *   Fan TACH wire → FAN_TACH_PIN (with 10k pull-up to 3.3V)
 *   Each revolution generates FAN_TACH_PULSES_PER_REV falling edges.
 *   ISR counts pulses; Fan__Handler() converts to RPM every FAN_TACH_MEASURE_MS.
 *   Fan__IsFailed() returns true if RPM==0 after FAN_STALL_DETECT_MS.
 *
 * FAILURE DETECTION:
 *   FAN_TACH_ENABLED=0 (current 2-wire hardware):
 *     Fan__IsFailed() → duty commanded >0 but reads back 0 (software fault)
 *   FAN_TACH_ENABLED=1 (future 3-wire hardware):
 *     Fan__IsFailed() → duty >0 AND RPM==0 after spin-up period (real fault)
 */

#include <Arduino.h>
#include "Fan.h"

/*==============================================================================
 *                          PRIVATE VARIABLES
 *============================================================================*/

static bool    fan_initialized = false;
static uint8_t fan_duty        = 0U;
static bool    s_ledc_attached = false;

/* Tachometer state — only used when FAN_TACH_ENABLED=1 */
#if (FAN_TACH_ENABLED == 1U)
static volatile uint32_t s_tach_pulse_count = 0U; /**< ISR increments this     */
static uint16_t           s_fan_rpm          = 0U; /**< Last calculated RPM     */
static uint32_t           s_tach_last_ms     = 0U; /**< Last RPM calc timestamp */
static uint32_t           s_fan_on_time_ms   = 0U; /**< Timestamp fan was turned ON */
static bool               s_fan_ever_on      = false; /**< Fan has been commanded ON */
#endif

/*==============================================================================
 *                          PRIVATE FUNCTIONS
 *============================================================================*/

/* Tachometer ISR — counts falling edges from tach wire */
#if (FAN_TACH_ENABLED == 1U)
static void IRAM_ATTR Fan_TachISR(void)
{
    s_tach_pulse_count++;
}
#endif

/* Write duty to LEDC with clean OFF handling for ESP32-S3 */
static void Fan_Write(uint8_t logical_duty)
{
    if (logical_duty == 0U)
    {
        if (s_ledc_attached)
        {
            ledcDetach(FAN_PIN);
            s_ledc_attached = false;
        }
        pinMode(FAN_PIN, OUTPUT);
        digitalWrite(FAN_PIN, LOW);
    }
    else
    {
        if (!s_ledc_attached)
        {
            ledcAttach(FAN_PIN, FAN_PWM_FREQ_HZ, FAN_PWM_RESOLUTION);
            s_ledc_attached = true;
        }
        ledcWrite(FAN_PIN, logical_duty);
    }

    fan_duty = logical_duty;

#if (FAN_TACH_ENABLED == 1U)
    /* Track when fan is first commanded ON for stall detection timer */
    if (logical_duty > 0U)
    {
        if (!s_fan_ever_on)
        {
            s_fan_on_time_ms = millis();
            s_fan_ever_on    = true;
        }
    }
    else
    {
        /* Fan turned OFF — reset stall detection */
        s_fan_ever_on = false;
        s_fan_rpm     = 0U;
    }
#endif
}

/*==============================================================================
 *                          PUBLIC FUNCTIONS
 *============================================================================*/

void Fan__Init(void)
{
    pinMode(FAN_PIN, OUTPUT);
    digitalWrite(FAN_PIN, LOW);
    delay(10);

    Fan_Write(0U);
    fan_initialized = true;

#if (FAN_TACH_ENABLED == 1U)
    /* Configure tachometer input with internal pull-up */
    pinMode(FAN_TACH_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FAN_TACH_PIN), Fan_TachISR, FALLING);
    s_tach_last_ms = millis();
    Serial.printf("[FAN] Tachometer enabled — GPIO%d, %d pulses/rev\n",
                  (int)FAN_TACH_PIN, (int)FAN_TACH_PULSES_PER_REV);
#endif

    Serial.printf("[FAN] Init OK — GPIO%d, %dHz, %.0f-%.0fV range, tach=%s\n",
                  (int)FAN_PIN, (int)FAN_PWM_FREQ_HZ,
                  FAN_VOLT_START, FAN_VOLT_FULL,
                  (FAN_TACH_ENABLED ? "YES" : "NO"));
}

void Fan__SetVoltage(float voltage)
{
    if (!fan_initialized) { return; }

    uint8_t duty = 0U;

    if (voltage <= FAN_VOLT_OFF)
    {
        duty = 0U;
    }
    else if (voltage >= FAN_VOLT_FULL)
    {
        duty = FAN_DUTY_MAX;
    }
    else
    {
        float ratio = (voltage - FAN_VOLT_START) /
                      (FAN_VOLT_FULL - FAN_VOLT_START);
        duty = (uint8_t)(FAN_DUTY_MIN + ratio * (float)(FAN_DUTY_MAX - FAN_DUTY_MIN));
    }

    Fan_Write(duty);

    // Serial.printf("[FAN] V=%.2fV  duty=%d (%d%%)\n",
    //               voltage, duty, (duty * 100) / 255);
}

void Fan__Off(void)
{
    if (!fan_initialized) { return; }
    Fan_Write(0U);
    Serial.println("[FAN] Forced OFF");
}

uint8_t Fan__GetDuty(void)
{
    return fan_duty;
}

void Fan__Handler(void)
{
    if (!fan_initialized) { return; }

#if (FAN_TACH_ENABLED == 1U)
    /*--------------------------------------------------------------------------
     * RPM calculation — runs every FAN_TACH_MEASURE_MS
     * RPM = (pulse_count / pulses_per_rev) / (window_sec) * 60
     *     = (pulse_count * 60000) / (pulses_per_rev * window_ms)
     *------------------------------------------------------------------------*/
    uint32_t now = millis();

    if ((now - s_tach_last_ms) >= FAN_TACH_MEASURE_MS)
    {
        /* Atomically snapshot and reset counter */
        noInterrupts();
        uint32_t pulses = s_tach_pulse_count;
        s_tach_pulse_count = 0U;
        interrupts();

        if (fan_duty > 0U)
        {
            s_fan_rpm = (uint16_t)((pulses * 60000UL) /
                        ((uint32_t)FAN_TACH_PULSES_PER_REV * FAN_TACH_MEASURE_MS));
        }
        else
        {
            s_fan_rpm = 0U;
        }

        s_tach_last_ms = now;

        Serial.printf("[FAN] RPM=%u  duty=%d\n", (unsigned)s_fan_rpm, (int)fan_duty);
    }
#endif
}

bool Fan__IsFailed(void)
{
    if (!fan_initialized) { return false; }

#if (FAN_TACH_ENABLED == 1U)
    /*--------------------------------------------------------------------------
     * 3-wire fan with tachometer — REAL physical failure detection
     * Condition: fan commanded ON + spin-up time elapsed + RPM still zero
     *------------------------------------------------------------------------*/
    if (fan_duty == 0U)
    {
        return false; /* Fan is off — not a failure */
    }

    if (!s_fan_ever_on)
    {
        return false; /* Fan not yet commanded ON */
    }

    /* Allow spin-up time before declaring stall */
    if ((millis() - s_fan_on_time_ms) < FAN_STALL_DETECT_MS)
    {
        return false; /* Still within spin-up grace period */
    }

    return (s_fan_rpm == 0U); /* RPM zero after spin-up = stall/disconnect */

#else
    /*--------------------------------------------------------------------------
     * 2-wire fan, no tachometer — SOFTWARE FAULT detection only
     * Condition: duty was written > 0 but reads back as 0
     * This catches LEDC/driver failures, not physical fan disconnection.
     *------------------------------------------------------------------------*/
    return (fan_duty == 0U);

#endif
}

uint16_t Fan__GetRPM(void)
{
#if (FAN_TACH_ENABLED == 1U)
    return s_fan_rpm;
#else
    return 0U;
#endif
}