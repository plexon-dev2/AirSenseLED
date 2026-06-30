/**
 * @file Buzzer_Cfg.h
 * @brief Buzzer Configuration -- LED Project
 *
 * Pin sourced from HardwareConfig.h.
 *   BUZZER -> HW_PIN_BUZZER = GPIO42
 *
 * @version 2.6.0
 *
 * v2.6.0 changes (review fixes):
 *   - Dead defines removed: BUZZER_LEDC_CHANNEL, BUZZER_LEDC_TIMER
 *     (ledcAttach() allocates channels automatically -- not needed)
 *   - Dead alias defines removed: BUZZER_SHORT_BEEP_MS, BUZZER_LONG_BEEP_MS_ALT
 *     (duplicated BUZZER_BEEP_MS and BUZZER_BEEP_LONG_MS -- caused confusion)
 *   - Tone preset defines retained but marked "reserved for future use" --
 *     currently unused since frequency is fixed at ledcAttach() init time
 *   - BUZZER_DEBUG_PRINTF: placeholder comment added -- will route through
 *     SERIAL_PRINTF once ProjectConfig.h is integrated
 *   - Note added: debug macros currently disabled (BUZZER_DEBUG_ENABLE = 0)
 */

#ifndef BUZZER_CFG_H
#define BUZZER_CFG_H

#include <stdint.h>
#include "HardwareConfig.h"
#include "AppMutex.h"       /* SERIAL_PRINTF -- mutex-safe debug output */

/*==============================================================================
 *                          PIN  (from HardwareConfig.h)
 *============================================================================*/
#define BUZZER_PIN                  HW_PIN_BUZZER   /**< GPIO42 */

/*==============================================================================
 *                          LEDC
 *  Channel and timer assigned automatically by ledcAttach() -- no manual
 *  assignment needed with the current ESP32 Arduino core API.
 *============================================================================*/
#define BUZZER_FREQ_HZ              (3500U)  /**< Matches buzzer resonant frequency */
#define BUZZER_RESOLUTION           (8U)     /**< 8-bit duty: 0-255                 */

/*==============================================================================
 *                          VOLUME
 *============================================================================*/
#define BUZZER_DEFAULT_VOLUME       (50U)    /**< Default volume percent at init    */
#define BUZZER_VOLUME_MIN           (0U)     /**< Minimum volume (silent)           */
#define BUZZER_VOLUME_MAX           (100U)   /**< Maximum volume                    */

/*==============================================================================
 *                          BEEP TIMING (ms)
 *============================================================================*/
#define BUZZER_BEEP_MS              (100U)   /**< Short beep duration               */
#define BUZZER_BEEP_GAP_MS          (100U)   /**< Gap between beeps in pattern      */
#define BUZZER_BEEP_LONG_MS         (500U)   /**< Long beep duration                */

/*==============================================================================
 *                          TONE PRESETS
 *  Reserved for future use -- frequency change per beep type requires
 *  calling ledcChangeFrequency() before each beep. Currently the buzzer
 *  runs at BUZZER_FREQ_HZ for all beep types.
 *============================================================================*/
#define BUZZER_ALARM_FREQ_HZ        (3500U)  /**< Alarm tone  (future use) */
#define BUZZER_NOTIFY_FREQ_HZ       (2000U)  /**< Notify tone (future use) */
#define BUZZER_ERROR_FREQ_HZ        (1000U)  /**< Error tone  (future use) */

/*==============================================================================
 *                          DEBUG
 *  @note Will be controlled by ProjectConfig.h once integrated.
 *        For now BUZZER_DEBUG_ENABLE is hardcoded.
 *        BUZZER_DEBUG_PRINTF routes through SERIAL_PRINTF (mutex-safe).
 *============================================================================*/
#define BUZZER_DEBUG_ENABLE         (0U)

#if (BUZZER_DEBUG_ENABLE == 1U)
    /**
     * @note MISRA C:2012 Rule 20.10 advisory deviation: variadic macro.
     *       Rationale: no compliant alternative for printf-style debug wrapper.
     */
    #define BUZZER_DEBUG_PRINTF(...)    SERIAL_PRINTF(__VA_ARGS__)
    #define BUZZER_DEBUG_PRINTLN(x)     SERIAL_PRINTF("%s\n", (x))
#else
    #define BUZZER_DEBUG_PRINTF(...)
    #define BUZZER_DEBUG_PRINTLN(x)
#endif

/*==============================================================================
 *                          VALIDATION
 *============================================================================*/
#if (BUZZER_DEFAULT_VOLUME > 100U)
#error "BUZZER_DEFAULT_VOLUME must be 0-100"
#endif

#if (BUZZER_VOLUME_MIN > BUZZER_VOLUME_MAX)
#error "BUZZER_VOLUME_MIN must be <= BUZZER_VOLUME_MAX"
#endif

#if (BUZZER_BEEP_MS == 0U)
#error "BUZZER_BEEP_MS must be > 0"
#endif

#if (BUZZER_BEEP_LONG_MS <= BUZZER_BEEP_MS)
#error "BUZZER_BEEP_LONG_MS must be greater than BUZZER_BEEP_MS"
#endif

#endif /* BUZZER_CFG_H */