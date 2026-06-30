/**
 * @file Fan.cpp
 * @brief Fan Module Implementation -- Voltage-based PWM control
 *
 * How it works:
 *   - User calls Fan__SetVoltage(v) with desired voltage 0-5V
 *   - Code converts voltage to duty cycle (0-255)
 *   - Writes to LEDC peripheral via Fan_Write()
 *
 * Circuit (NPN + P-channel MOSFET):
 *   GPIO HIGH -> NPN ON  -> Gate LOW  -> MOSFET ON  -> Fan ON
 *   GPIO LOW  -> NPN OFF -> Gate HIGH -> MOSFET OFF -> Fan OFF
 *
 * KICKSTART (non-blocking):
 *   When fan starts from rest, briefly runs at full duty for FAN_KICKSTART_MS
 *   to overcome static friction. Uses millis() timer -- never blocks.
 *   Fan__Handler() must be called every scheduler cycle.
 *
 * TACHOMETER (3-wire fan, FAN_TACH_ENABLED=1):
 *   Fan TACH wire -> FAN_TACH_PIN (with 10k pull-up to 3.3V)
 *   Each revolution generates FAN_TACH_PULSES_PER_REV falling edges.
 *   ISR counts pulses; Fan__Handler() converts to RPM every FAN_TACH_MEASURE_MS.
 *   Fan__IsFailed() returns true if RPM==0 after FAN_STALL_DETECT_MS.
 *
 * FAILURE DETECTION:
 *   FAN_TACH_ENABLED=0 (current 2-wire hardware):
 *     Fan__IsFailed() always returns false -- no physical detection possible.
 *   FAN_TACH_ENABLED=1 (future 3-wire hardware):
 *     Fan__IsFailed() -> duty>0 AND RPM==0 after FAN_STALL_DETECT_MS.
 *
 * @version 2.6.0
 *
 * v2.6.0 changes (review fixes):
 *   - Fan__SetVoltage(): added FAN_VOLT_OFF < voltage <= FAN_VOLT_START range
 *     guard -- was falling through to the linear interpolation with a negative
 *     ratio, producing duty < FAN_DUTY_MIN (below minimum running speed).
 *   - Fan__SetVoltage(): float-to-uint8_t duty clamped to FAN_DUTY_MAX before
 *     cast -- float rounding above 1.0 could produce > 255, wrapping silently.
 *   - Fan__SetDutyPercent(): parameter no longer modified directly -- uses
 *     local copy pct (MISRA Rule 17.8).
 *   - Fan__SetFrequency(): parameter no longer modified directly -- uses
 *     local copy freq (MISRA Rule 17.8).
 *   - All Serial.printf / Serial.println replaced with FAN_DEBUG_PRINTF
 *     (mutex-safe via SERIAL_PRINTF in Fan_Cfg.h).
 *   - Tachometer RPM Serial.printf gated behind FAN_DEBUG_PRINTF.
 *   - delay(10) in Fan__Init() documented as safe-at-init gate discharge delay.
 *   - Fan__GetDuty(): returns s_kickstart_target during active kickstart so
 *     callers get the intended speed, not the temporary full-burst duty.
 */

#include <Arduino.h>
#include "Fan.h"
#include "AppMutex.h"   /* SERIAL_PRINTF -- used by FAN_DEBUG_PRINTF in Fan_Cfg.h */

/*==============================================================================
 *                          PRIVATE VARIABLES
 *============================================================================*/

static bool     fan_initialized  = false;
static uint8_t  fan_duty         = 0U;
static bool     s_ledc_attached  = false;
static uint32_t s_fan_freq_hz    = FAN_PWM_FREQ_HZ;

/* Non-blocking kickstart state */
static bool     s_kickstart_active = false; /**< true during full-duty burst */
static uint32_t s_kickstart_start  = 0U;   /**< millis() when burst started  */
static uint8_t  s_kickstart_target = 0U;   /**< duty to apply after burst    */

/* Tachometer state -- compiled in only when FAN_TACH_ENABLED=1 */
#if (FAN_TACH_ENABLED == 1U)
static volatile uint32_t s_tach_pulse_count = 0U;
static uint16_t           s_fan_rpm          = 0U;
static uint32_t           s_tach_last_ms     = 0U;
static uint32_t           s_fan_on_time_ms   = 0U;
static bool               s_fan_ever_on      = false;
#endif

/*==============================================================================
 *                          PRIVATE FUNCTIONS
 *============================================================================*/

#if (FAN_TACH_ENABLED == 1U)
/**
 * @brief Tachometer pulse ISR -- IRAM resident for low latency.
 * @details Increments s_tach_pulse_count on each falling edge.
 *          Cleared atomically in Fan__Handler() using noInterrupts().
 */
static void IRAM_ATTR Fan_TachISR(void)
{
    s_tach_pulse_count++;
}
#endif

/**
 * @brief Write logical duty to hardware.
 * @details duty=0: detach LEDC, drive GPIO LOW (fan off).
 *          duty>0: attach LEDC if needed, write PWM duty.
 *          Tracks fan-on time for tachometer stall detection.
 * @param[in] logical_duty PWM duty 0-255.
 */
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
            ledcAttach(FAN_PIN, s_fan_freq_hz, FAN_PWM_RESOLUTION);
            s_ledc_attached = true;
        }
        ledcWrite(FAN_PIN, logical_duty);
    }

    fan_duty = logical_duty;

#if (FAN_TACH_ENABLED == 1U)
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
        s_fan_ever_on = false;
        s_fan_rpm     = 0U;
    }
#endif
}

/**
 * @brief Start non-blocking kickstart burst then settle to target duty.
 * @param[in] target_duty Duty to apply after FAN_KICKSTART_MS elapses.
 */
static void Fan_StartKickstart(uint8_t target_duty)
{
    s_kickstart_target = target_duty;
    s_kickstart_active = true;
    s_kickstart_start  = millis();
    Fan_Write(FAN_DUTY_MAX);  /* full-duty burst to overcome static friction */
}

/*==============================================================================
 *                          PUBLIC FUNCTIONS
 *============================================================================*/

void Fan__Init(void)
{
    pinMode(FAN_PIN, OUTPUT);
    digitalWrite(FAN_PIN, LOW);

    /* Brief delay to allow MOSFET gate to fully discharge before LEDC attach.
     * Safe here -- Init() is called before Scheduler_Start(). */
    delay(10U);

    Fan_Write(0U);
    fan_initialized = true;

#if (FAN_TACH_ENABLED == 1U)
    pinMode(FAN_TACH_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FAN_TACH_PIN), Fan_TachISR, FALLING);
    s_tach_last_ms = millis();
    FAN_DEBUG_PRINTF("[FAN] Tachometer enabled -- GPIO%d, %d pulses/rev\n",
                     (int)FAN_TACH_PIN, (int)FAN_TACH_PULSES_PER_REV);
#endif

    /* Init log -- Serial.printf is safe here (before scheduler, single core) */
    Serial.printf("[FAN] Init OK -- GPIO%d, %dHz, %.0f-%.0fV range, tach=%s\n",
                  (int)FAN_PIN, (int)FAN_PWM_FREQ_HZ,
                  (double)FAN_VOLT_START, (double)FAN_VOLT_FULL,
                  (FAN_TACH_ENABLED != 0U) ? "YES" : "NO");
}

void Fan__SetVoltage(float voltage)
{
    if (!fan_initialized) { return; }

    uint8_t duty;

    if (voltage <= FAN_VOLT_OFF)
    {
        /* Below off threshold -- fan off */
        duty = 0U;
    }
    else if (voltage >= FAN_VOLT_FULL)
    {
        /* At or above full voltage -- maximum duty */
        duty = FAN_DUTY_MAX;
    }
    else if (voltage <= FAN_VOLT_START)
    {
        /* Between off threshold and minimum running voltage --
         * use minimum running duty to avoid stalling the fan.
         * Previously this fell through to linear interpolation,
         * producing a negative ratio and duty below FAN_DUTY_MIN. */
        duty = FAN_DUTY_MIN;
    }
    else
    {
        /* Linear interpolation between start and full voltage */
        float ratio = (voltage - FAN_VOLT_START) /
                      (FAN_VOLT_FULL - FAN_VOLT_START);

        /* Clamp ratio to [0, 1] to handle float rounding above 1.0 */
        if (ratio > 1.0f) { ratio = 1.0f; }
        if (ratio < 0.0f) { ratio = 0.0f; }

        float duty_f = (float)FAN_DUTY_MIN + ratio * (float)(FAN_DUTY_MAX - FAN_DUTY_MIN);

        /* Clamp to valid range before uint8_t cast */
        if (duty_f > (float)FAN_DUTY_MAX) { duty_f = (float)FAN_DUTY_MAX; }
        duty = (uint8_t)duty_f;
    }

    /* Non-blocking kickstart: burst to full duty when starting from stopped state */
    if ((fan_duty == 0U) && (duty > 0U) && (duty < FAN_DUTY_MAX) && !s_kickstart_active)
    {
        Fan_StartKickstart(duty);
        return;
    }

    /* If kickstart active, update target in case voltage changed mid-burst */
    if (s_kickstart_active && (duty > 0U))
    {
        s_kickstart_target = duty;
        return;
    }

    Fan_Write(duty);
}

void Fan__SetDutyPercent(uint8_t percent)
{
    if (!fan_initialized) { return; }

    /* Use local copy -- do not modify parameter (MISRA Rule 17.8) */
    uint8_t pct = percent;
    if (pct > 100U) { pct = 100U; }

    uint8_t duty = (uint8_t)(((uint32_t)pct * (uint32_t)FAN_DUTY_MAX) / 100U);

    if ((fan_duty == 0U) && (duty > 0U) && (duty < FAN_DUTY_MAX) && !s_kickstart_active)
    {
        Fan_StartKickstart(duty);
        FAN_DEBUG_PRINTF("[FAN] Duty set to %d%% -- kickstart started\n", (int)pct);
        return;
    }

    if (s_kickstart_active && (duty > 0U))
    {
        s_kickstart_target = duty;
        return;
    }

    Fan_Write(duty);
    FAN_DEBUG_PRINTF("[FAN] Duty set to %d%% (raw=%d)\n", (int)pct, (int)duty);
}

void Fan__Off(void)
{
    if (!fan_initialized) { return; }
    s_kickstart_active = false;
    Fan_Write(0U);
    FAN_DEBUG_PRINTLN("[FAN] Forced OFF");
}

uint8_t Fan__GetDuty(void)
{
    /* During kickstart burst the physical duty is FAN_DUTY_MAX, but the
     * intended speed is s_kickstart_target. Return the intended value so
     * callers can reason about actual fan speed, not the temporary burst. */
    if (s_kickstart_active) { return s_kickstart_target; }
    return fan_duty;
}

void Fan__Handler(void)
{
    if (!fan_initialized) { return; }

    /*--------------------------------------------------------------------------
     * Non-blocking kickstart completion check.
     * When burst timer expires, apply the stored target duty.
     *------------------------------------------------------------------------*/
    if (s_kickstart_active)
    {
        if ((millis() - s_kickstart_start) >= (uint32_t)FAN_KICKSTART_MS)
        {
            s_kickstart_active = false;
            Fan_Write(s_kickstart_target);
            FAN_DEBUG_PRINTF("[FAN] Kickstart done -- settled to duty=%d\n",
                             (int)s_kickstart_target);
        }
        return;  /* Do not run tach calculation during kickstart burst */
    }

#if (FAN_TACH_ENABLED == 1U)
    uint32_t now = millis();
    if ((now - s_tach_last_ms) >= (uint32_t)FAN_TACH_MEASURE_MS)
    {
        /* Atomic read + clear of ISR counter */
        noInterrupts();
        uint32_t pulses    = s_tach_pulse_count;
        s_tach_pulse_count = 0U;
        interrupts();

        if (fan_duty > 0U)
        {
            s_fan_rpm = (uint16_t)((pulses * 60000UL) /
                        ((uint32_t)FAN_TACH_PULSES_PER_REV * (uint32_t)FAN_TACH_MEASURE_MS));
        }
        else
        {
            s_fan_rpm = 0U;
        }

        s_tach_last_ms = now;
        FAN_DEBUG_PRINTF("[FAN] RPM=%u  duty=%d\n", (unsigned)s_fan_rpm, (int)fan_duty);
    }
#endif
}

bool Fan__IsFailed(void)
{
    if (!fan_initialized) { return false; }

#if (FAN_TACH_ENABLED == 1U)
    if (fan_duty == 0U)                                                { return false; }
    if (!s_fan_ever_on)                                                { return false; }
    if ((millis() - s_fan_on_time_ms) < (uint32_t)FAN_STALL_DETECT_MS) { return false; }
    return (s_fan_rpm == 0U);
#else
    /* 2-wire fan, no tachometer.
     * No physical RPM feedback -- cannot detect real stall.
     * Always return false to avoid false fault triggering. */
    return false;
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

void Fan__SetFrequency(uint32_t hz)
{
    /* Use local copy -- do not modify parameter (MISRA Rule 17.8) */
    uint32_t freq = hz;
    if (freq < (uint32_t)FAN_FREQ_MIN_HZ) { freq = (uint32_t)FAN_FREQ_MIN_HZ; }
    if (freq > (uint32_t)FAN_FREQ_MAX_HZ) { freq = (uint32_t)FAN_FREQ_MAX_HZ; }
    if (freq == s_fan_freq_hz)            { return; }

    s_fan_freq_hz = freq;

    /* Detach existing LEDC channel before changing frequency */
    if (s_ledc_attached)
    {
        ledcDetach(FAN_PIN);
        s_ledc_attached = false;
    }

    /* Re-attach with new frequency using current duty.
     * Note: if fan_duty==0, Fan_Write(0) drives GPIO LOW without LEDC --
     * frequency change has no effect until next non-zero duty write. */
    Fan_Write(fan_duty);

    FAN_DEBUG_PRINTF("[FAN] Frequency set to %luHz\n", (unsigned long)s_fan_freq_hz);
}

uint32_t Fan__GetFrequency(void)
{
    return s_fan_freq_hz;
}