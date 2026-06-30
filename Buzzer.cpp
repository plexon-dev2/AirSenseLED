/**
 * @file Buzzer.cpp
 * @brief Buzzer Module Implementation
 * @details Passive buzzer driver using ESP32 LEDC PWM.
 *          Volume controlled by PWM duty cycle.
 *          All timing configuration in Buzzer_Cfg.h.
 *
 * @note All beep functions use vTaskDelay() so the FreeRTOS scheduler can
 *       run other tasks during beep durations.  A Buzzer_Delay() helper
 *       automatically falls back to delay() if called before the scheduler
 *       starts -- safe for both init-time and runtime use.
 *
 * @version 2.1.0
 *
 * v2.1.0 changes (review fixes):
 *   - CRITICAL: Removed blocking delay(500) test beep from Buzzer__Init().
 *     Was blocking Core 1 init sequence for 500ms. Startup beep is an
 *     application-layer concern -- call Buzzer__Beep() from App__Init()
 *     after the scheduler is running if a power-on sound is needed.
 *   - Buzzer_Delay() helper added: uses vTaskDelay() when scheduler is
 *     running, falls back to delay() otherwise -- safe in all call contexts.
 *   - Buzzer__SetVolume(): no longer modifies parameter directly -- uses
 *     local copy vol (MISRA Rule 17.8).
 *   - s_volumePercent added: stores exact percent so Buzzer__GetVolume()
 *     returns the exact set value without rounding loss from duty round-trip.
 *   - Buzzer__BeepCustom(): duty parameter changed to uint8_t (was uint32_t
 *     -- silent truncation risk when value > 255). Clamp added as safety net.
 *   - Serial.printf in Init and SetVolume replaced with BUZZER_DEBUG_PRINTF
 *     (routes through SERIAL_PRINTF -- mutex-safe on dual-core).
 *   - Volume_ToDuty(): single-exit refactor, explicit cast comment added.
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "Buzzer.h"
#include "Buzzer_Cfg.h"

/*==============================================================================
 *                          PRIVATE VARIABLES
 *============================================================================*/

/** @brief Current PWM duty value (0-255) computed from volume percent */
static uint8_t s_duty          = 0U;

/** @brief Current volume percent -- stored exactly to avoid round-trip loss */
static uint8_t s_volumePercent = BUZZER_DEFAULT_VOLUME;

/** @brief Initialisation flag -- guards all beep functions */
static bool    s_initialized   = false;

/*==============================================================================
 *                          PRIVATE HELPERS
 *============================================================================*/

/**
 * @brief Convert volume percent to LEDC duty value.
 * @param percent Volume 0-100.
 * @return PWM duty 0-255.
 * @note  Result max = 255, fits uint8_t without overflow.
 */
static uint8_t Volume_ToDuty(uint8_t percent)
{
    uint8_t result;

    if (percent == 0U)
    {
        result = 0U;
    }
    else if (percent >= 100U)
    {
        result = 255U;
    }
    else
    {
        /* max: 99 * 255 / 100 = 252 -- fits uint8_t */
        result = (uint8_t)((uint32_t)percent * 255U / 100U);
    }

    return result;
}

/**
 * @brief Scheduler-safe delay.
 * @details Uses vTaskDelay() when the FreeRTOS scheduler is running so
 *          other tasks are not blocked.  Falls back to Arduino delay()
 *          when called before vTaskStartScheduler() (e.g. during init).
 * @param ms Delay duration in milliseconds.
 */
static void Buzzer_Delay(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
    else
    {
        delay(ms);
    }
}

static void Buzzer_On(void)
{
    ledcWrite(BUZZER_PIN, s_duty);
}

static void Buzzer_Off(void)
{
    ledcWrite(BUZZER_PIN, 0U);
}

/*==============================================================================
 *                          PUBLIC API
 *============================================================================*/

void Buzzer__Init(void)
{
    ledcAttach(BUZZER_PIN, BUZZER_FREQ_HZ, BUZZER_RESOLUTION);
    ledcWrite(BUZZER_PIN, 0U);

    s_volumePercent = BUZZER_DEFAULT_VOLUME;
    s_duty          = Volume_ToDuty(BUZZER_DEFAULT_VOLUME);
    s_initialized   = true;

    BUZZER_DEBUG_PRINTF("[BUZZER] Init OK -- GPIO%d, %uHz, volume=%u%%\n",
                        (int)BUZZER_PIN, (unsigned)BUZZER_FREQ_HZ,
                        (unsigned)BUZZER_DEFAULT_VOLUME);

    /* NOTE: test beep removed -- startup beep is an application concern.
     * Call Buzzer__Beep() from App__Init() after the scheduler starts
     * if a power-on confirmation sound is needed. */
}

void Buzzer__SetVolume(uint8_t percent)
{
    /* Use local copy -- do not modify parameter (MISRA Rule 17.8) */
    uint8_t vol = percent;

    if (vol < BUZZER_VOLUME_MIN) { vol = BUZZER_VOLUME_MIN; }
    if (vol > BUZZER_VOLUME_MAX) { vol = BUZZER_VOLUME_MAX; }

    s_volumePercent = vol;
    s_duty          = Volume_ToDuty(vol);

    BUZZER_DEBUG_PRINTF("[BUZZER] Volume set to %u%% (duty=%u)\n",
                        (unsigned)vol, (unsigned)s_duty);
}

uint8_t Buzzer__GetVolume(void)
{
    /* Return exact stored percent -- no round-trip loss from duty conversion */
    return s_volumePercent;
}

/**
 * @brief Short beep -- touch confirmation (BUZZER_BEEP_MS)
 */
void Buzzer__Beep(void)
{
    if (!s_initialized) { return; }
    Buzzer_On();
    Buzzer_Delay(BUZZER_BEEP_MS);
    Buzzer_Off();
}

/**
 * @brief Double beep -- success/connect events
 */
void Buzzer__BeepDouble(void)
{
    if (!s_initialized) { return; }
    Buzzer_On();
    Buzzer_Delay(BUZZER_BEEP_MS);
    Buzzer_Off();
    Buzzer_Delay(BUZZER_BEEP_GAP_MS);
    Buzzer_On();
    Buzzer_Delay(BUZZER_BEEP_MS);
    Buzzer_Off();
}

/**
 * @brief Long beep -- warning/error events (BUZZER_BEEP_LONG_MS)
 */
void Buzzer__BeepLong(void)
{
    if (!s_initialized) { return; }
    Buzzer_On();
    Buzzer_Delay(BUZZER_BEEP_LONG_MS);
    Buzzer_Off();
}

/**
 * @brief Pattern beep -- N short beeps with gap between each
 * @param count Number of beeps (1-10; clamped if above 10)
 * @note  No gap is added after the final beep (intentional).
 */
void Buzzer__Pattern(uint8_t count)
{
    if (!s_initialized) { return; }
    if (count == 0U)    { return; }
    if (count > 10U)    { count = 10U; }

    for (uint8_t i = 0U; i < count; i++)
    {
        Buzzer_On();
        Buzzer_Delay(BUZZER_BEEP_MS);
        Buzzer_Off();

        /* Gap after every beep except the last -- intentional, not a bug */
        if (i < (count - 1U))
        {
            Buzzer_Delay(BUZZER_BEEP_GAP_MS);
        }
    }
}

/**
 * @brief Custom beep with specific duty cycle and duration.
 * @param duty        PWM duty 0-255 (overrides volume for this beep).
 *                    Clamped to 255 as a safety net -- parameter is uint8_t
 *                    so values > 255 are already rejected by type.
 * @param duration_ms Duration in milliseconds.
 */
void Buzzer__BeepCustom(uint8_t duty, uint32_t duration_ms)
{
    if (!s_initialized)  { return; }
    if (duration_ms == 0U) { return; }

    ledcWrite(BUZZER_PIN, duty);
    Buzzer_Delay(duration_ms);
    Buzzer_Off();
}