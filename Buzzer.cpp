/**
 * @file Buzzer.cpp
 * @brief Buzzer Module Implementation
 * @details Passive buzzer driver using ESP32 LEDC PWM.
 *          Volume is controlled by PWM duty cycle.
 *          All timing configuration is in Buzzer_Cfg.h.
 *
 * @note All blocking delay() calls replaced with vTaskDelay() so the
 *       FreeRTOS scheduler can run other tasks during beep durations.
 *       This prevents Task_50ms_Core1 from missing its deadline when
 *       touch/alarm handlers call Buzzer functions.
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "Buzzer.h"

/*==============================================================================
 *                          PRIVATE VARIABLES
 *============================================================================*/

static uint8_t s_duty        = 0U;
static bool    s_initialized = false;

/*==============================================================================
 *                          PRIVATE HELPERS
 *============================================================================*/

static uint8_t Volume_ToDuty(uint8_t percent)
{
    if (percent == 0U)   return 0U;
    if (percent >= 100U) return 255U;
    return (uint8_t)((uint32_t)percent * 255U / 100U);
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
    s_duty        = Volume_ToDuty(BUZZER_DEFAULT_VOLUME);
    s_initialized = true;
    Serial.printf("[BUZZER] Init OK — GPIO%d, %dHz, volume=%d%%\n",
                  (int)BUZZER_PIN, (int)BUZZER_FREQ_HZ, (int)BUZZER_DEFAULT_VOLUME);
}

void Buzzer__SetVolume(uint8_t percent)
{
    if (percent < BUZZER_VOLUME_MIN) percent = BUZZER_VOLUME_MIN;
    if (percent > BUZZER_VOLUME_MAX) percent = BUZZER_VOLUME_MAX;
    s_duty = Volume_ToDuty(percent);
    Serial.printf("[BUZZER] Volume set to %d%% (duty=%d)\n", (int)percent, (int)s_duty);
}

uint8_t Buzzer__GetVolume(void)
{
    return (uint8_t)((uint32_t)s_duty * 100U / 255U);
}

/**
 * @brief Short beep — touch confirmation (30ms)
 * @note  vTaskDelay yields to scheduler during the beep — does not block the task.
 */
void Buzzer__Beep(void)
{
    if (!s_initialized) { return; }
    Buzzer_On();
    vTaskDelay(pdMS_TO_TICKS(BUZZER_BEEP_MS));
    Buzzer_Off();
}

/**
 * @brief Double beep — success/connect events (30ms + gap + 30ms)
 */
void Buzzer__BeepDouble(void)
{
    if (!s_initialized) { return; }
    Buzzer_On();
    vTaskDelay(pdMS_TO_TICKS(BUZZER_BEEP_MS));
    Buzzer_Off();
    vTaskDelay(pdMS_TO_TICKS(BUZZER_BEEP_GAP_MS));
    Buzzer_On();
    vTaskDelay(pdMS_TO_TICKS(BUZZER_BEEP_MS));
    Buzzer_Off();
}

/**
 * @brief Long beep — warning/error events (500ms)
 * @note  With delay() this blocked Task_50ms_Core1 for 500ms — 10 missed cycles.
 *        vTaskDelay yields instead, keeping the scheduler responsive.
 */
void Buzzer__BeepLong(void)
{
    if (!s_initialized) { return; }
    Buzzer_On();
    vTaskDelay(pdMS_TO_TICKS(BUZZER_BEEP_LONG_MS));
    Buzzer_Off();
}

/**
 * @brief Pattern beep — N short beeps with gap
 * @param count Number of beeps (1-10)
 */
void Buzzer__Pattern(uint8_t count)
{
    if (!s_initialized) { return; }
    if (count == 0U)    { return; }
    if (count > 10U)    { count = 10U; }

    for (uint8_t i = 0U; i < count; i++)
    {
        Buzzer_On();
        vTaskDelay(pdMS_TO_TICKS(BUZZER_BEEP_MS));
        Buzzer_Off();
        if (i < (count - 1U))
        {
            vTaskDelay(pdMS_TO_TICKS(BUZZER_BEEP_GAP_MS));
        }
    }
}

/**
 * @brief Custom beep with specific duty and duration
 * @param duty        PWM duty 0-255
 * @param duration_ms Duration in milliseconds
 */
void Buzzer__BeepCustom(uint32_t duty, uint32_t duration_ms)
{
    if (!s_initialized) { return; }
    ledcWrite(BUZZER_PIN, (uint8_t)duty);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    Buzzer_Off();
}