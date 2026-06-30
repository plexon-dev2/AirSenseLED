/**
 * @file BLESwitch_Cfg.h
 * @brief BLE Switch Control -- Configuration
 *
 * PIN ASSIGNMENT:
 *   GPIO8 (HW_PIN_SW_BLE) -- INPUT_PULLUP, active LOW
 *
 * BEHAVIOUR SUMMARY:
 * ==================
 *   Cold boot  : BLE OFF -> short press -> BLE ON (no timer)
 *   Warm reset : BLE ON automatically -> 2-min timer ->
 *                  Nobody connects -> BLE OFF
 *                  Client connects -> LED blinks, timer cancelled
 *                  Client disconnects -> LED off, BLE stays ON
 *   After timeout : 5-sec hold -> BLE ON + new 2-min timer
 *
 * @version 2.1.0
 *
 * v2.1.0 changes (review fixes):
 *   - BLESWITCH_DBG_PRINTF: now routes through SERIAL_PRINTF from AppMutex.h
 *     (was Serial.printf -- bypassed serial mutex, caused Modbus RX corruption)
 *   - Non-ASCII characters in comments replaced with ASCII equivalents
 *   - BLESWITCH_DEBOUNCE_MS upper validation bound tightened from 500 to 200ms
 *   - Changelog added
 */

#ifndef BLE_SWITCH_CFG_H
#define BLE_SWITCH_CFG_H

#include "HardwareConfig.h"
#include "AppMutex.h"       /* SERIAL_PRINTF -- mutex-safe debug output */

/*==============================================================================
 *                          PIN
 *============================================================================*/

/** @brief BLE switch GPIO pin (from HardwareConfig.h) */
#define BLESWITCH_PIN               (HW_PIN_SW_BLE)

/** @brief Active logic level -- LOW = switch pressed (INPUT_PULLUP) */
#define BLESWITCH_ACTIVE_LEVEL      (LOW)

/*==============================================================================
 *                          TIMING
 *============================================================================*/

/** @brief Switch debounce window (ms) -- applied on both press and release edges */
#define BLESWITCH_DEBOUNCE_MS           (50U)

/** @brief Hold duration to re-enable BLE after timeout (ms) -- 5 seconds */
#define BLESWITCH_HOLD_MS               (5000U)

/** @brief Warm-reset auto-off timer (ms) -- 2 minutes */
#define BLESWITCH_WARM_TIMEOUT_MS       (120000UL)

/*==============================================================================
 *                          DEBUG
 *  Uses SERIAL_PRINTF from AppMutex.h -- mutex-safe across dual-core tasks.
 *  Raw Serial.printf causes Modbus RX byte corruption when called from Core 0.
 *============================================================================*/

#define BLESWITCH_DEBUG_ENABLE      (1U)

#if (BLESWITCH_DEBUG_ENABLE == 1U)
    /**
     * @brief Mutex-safe debug printf via SERIAL_PRINTF.
     * @note  MISRA C:2012 Rule 20.10 advisory deviation: variadic macro.
     *        Rationale: no compliant alternative for printf-style debug wrapper;
     *        deviation isolated to this file and documented here.
     */
    #define BLESWITCH_DBG_PRINTF(...)   SERIAL_PRINTF(__VA_ARGS__)
#else
    #define BLESWITCH_DBG_PRINTF(...)
#endif

/*==============================================================================
 *                          VALIDATION
 *============================================================================*/

#if (BLESWITCH_DEBOUNCE_MS < 10U) || (BLESWITCH_DEBOUNCE_MS > 200U)
#error "BLESWITCH_DEBOUNCE_MS must be 10 - 200 ms"
#endif

#if (BLESWITCH_HOLD_MS < 1000U) || (BLESWITCH_HOLD_MS > 10000U)
#error "BLESWITCH_HOLD_MS must be 1000 - 10000 ms"
#endif

#if (BLESWITCH_WARM_TIMEOUT_MS < 30000UL)
#error "BLESWITCH_WARM_TIMEOUT_MS must be at least 30 seconds"
#endif

#endif /* BLE_SWITCH_CFG_H */