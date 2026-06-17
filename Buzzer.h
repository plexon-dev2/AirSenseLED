/**
 * @file Buzzer.h
 * @brief Buzzer Module Interface
 * @details Passive buzzer driver using ESP32 LEDC PWM
 *          Supports volume control, short/long beeps, pattern beeps
 *
 * @note Edit Buzzer_Cfg.h to change pin, frequency, and timing
 *
 * Usage:
 *   Buzzer__Init();              // call once in setup()
 *   Buzzer__SetVolume(80);       // set volume 0-100%
 *   Buzzer__Beep();              // short beep — touch confirmation
 *   Buzzer__BeepLong();          // long beep — warning/error
 *   Buzzer__Pattern(3);          // 3 short beeps — alert
 *   Buzzer__BeepDouble();        // double beep — success/connect
 */

#ifndef BUZZER_H
#define BUZZER_H

#include "Buzzer_Cfg.h"

/*==============================================================================
 *                          FUNCTION PROTOTYPES
 *============================================================================*/

/**
 * @brief Initialize the buzzer module
 * Must be called once in setup() before any other Buzzer function.
 */
void Buzzer__Init(void);

/**
 * @brief Set buzzer volume
 * @param percent Volume level 0-100 (0=silent, 100=maximum)
 */
void Buzzer__SetVolume(uint8_t percent);

/**
 * @brief Get current volume percent
 * @return uint8_t current volume 0-100
 */
uint8_t Buzzer__GetVolume(void);

/**
 * @brief Short beep — touch confirmation
 */
void Buzzer__Beep(void);

/**
 * @brief Double beep — success/connect events
 */
void Buzzer__BeepDouble(void);

/**
 * @brief Long beep — warning/error events
 */
void Buzzer__BeepLong(void);

/**
 * @brief Pattern beep — N short beeps with gap
 * @param count Number of beeps (1-10)
 */
void Buzzer__Pattern(uint8_t count);

/**
 * @brief Custom beep with specific duty and duration
 * @param duty        PWM duty 0-255 (overrides volume setting)
 * @param duration_ms Duration in milliseconds
 */
void Buzzer__BeepCustom(uint32_t duty, uint32_t duration_ms);

#endif /* BUZZER_H */