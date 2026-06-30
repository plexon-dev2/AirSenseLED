/**
 * @file Buzzer.h
 * @brief Buzzer Module Interface
 * @details Passive buzzer driver using ESP32 LEDC PWM.
 *          Supports volume control, short/long beeps, pattern beeps.
 *          All beep functions use vTaskDelay() -- safe to call from any
 *          FreeRTOS task. Do NOT call from ISR or timer callback context.
 *
 * @note Edit Buzzer_Cfg.h to change pin, frequency, and timing.
 *
 * Usage:
 *   Buzzer__Init();              // call once in setup() / init sequence
 *   Buzzer__SetVolume(80);       // set volume 0-100%
 *   Buzzer__Beep();              // short beep -- touch confirmation
 *   Buzzer__BeepLong();          // long beep -- warning/error
 *   Buzzer__Pattern(3);          // 3 short beeps -- alert
 *   Buzzer__BeepDouble();        // double beep -- success/connect
 *
 * @version 2.1.0
 *
 * v2.1.0 changes (review fixes):
 *   - Buzzer__BeepCustom(): duty parameter changed from uint32_t to uint8_t
 *     (LEDC is 8-bit resolution; uint32_t was misleading and caused silent
 *     truncation -- values > 255 would silently wrap to 0)
 *   - Header note added: vTaskDelay context requirement documented
 */

#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>
#include "Buzzer_Cfg.h"

/*==============================================================================
 *                          FUNCTION PROTOTYPES
 *============================================================================*/

/**
 * @brief Initialize the buzzer module.
 * @details Attaches LEDC PWM, sets default volume.
 *          Safe to call before FreeRTOS scheduler starts.
 *          Does NOT perform a test beep -- startup beep belongs in App layer.
 */
void Buzzer__Init(void);

/**
 * @brief Set buzzer volume.
 * @param percent Volume level 0-100 (0=silent, 100=maximum).
 *                Clamped to [BUZZER_VOLUME_MIN, BUZZER_VOLUME_MAX].
 */
void Buzzer__SetVolume(uint8_t percent);

/**
 * @brief Get current volume percent.
 * @return Current volume 0-100 (exact value set, no rounding loss).
 */
uint8_t Buzzer__GetVolume(void);

/**
 * @brief Short beep -- touch confirmation (BUZZER_BEEP_MS duration).
 * @note  Uses vTaskDelay() -- must be called from a FreeRTOS task context.
 */
void Buzzer__Beep(void);

/**
 * @brief Double beep -- success/connect events.
 * @note  Uses vTaskDelay() -- must be called from a FreeRTOS task context.
 */
void Buzzer__BeepDouble(void);

/**
 * @brief Long beep -- warning/error events (BUZZER_BEEP_LONG_MS duration).
 * @note  Uses vTaskDelay() -- must be called from a FreeRTOS task context.
 */
void Buzzer__BeepLong(void);

/**
 * @brief Pattern beep -- N short beeps with gap.
 * @param count Number of beeps (1-10; values above 10 are clamped).
 * @note  Uses vTaskDelay() -- must be called from a FreeRTOS task context.
 */
void Buzzer__Pattern(uint8_t count);

/**
 * @brief Custom beep with specific duty cycle and duration.
 * @param duty        PWM duty 0-255 (overrides volume setting for this beep).
 * @param duration_ms Duration in milliseconds.
 * @note  Uses vTaskDelay() -- must be called from a FreeRTOS task context.
 */
void Buzzer__BeepCustom(uint8_t duty, uint32_t duration_ms);

#endif /* BUZZER_H */