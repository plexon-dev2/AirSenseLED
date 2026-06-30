/**
 * @file BLESwitch.h
 * @brief BLE Hardware Switch Control -- Interface
 *
 * STATE MACHINE:
 * ==============
 *
 *   COLD_IDLE
 *     BLE OFF, LED OFF -- cold boot, waiting for short switch press (rising edge).
 *     Short press (rising edge) -> ADVERTISING
 *
 *   ADVERTISING
 *     BLE ON advertising, LED OFF, no timer.
 *     (Entered from cold boot press or after client disconnects.)
 *     Client connects -> CONNECTED
 *
 *   ADVERTISING_TIMED
 *     BLE ON advertising, LED OFF, 2-min countdown active.
 *     (Entered from warm reset or 5-sec hold after timeout.)
 *     Client connects           -> CONNECTED (timer cancelled by state change)
 *     2-min expires, no connect -> BLE_OFF
 *
 *   CONNECTED
 *     BLE ON, client connected, LED blinks (1Hz).
 *     Client disconnects -> ADVERTISING (LED off, BLE stays ON, no timer)
 *
 *   BLE_OFF
 *     BLE OFF, LED OFF -- 2-min timer expired with nobody connecting.
 *     Hold switch 5 seconds -> ADVERTISING_TIMED (new 2-min timer)
 *
 * INTEGRATION:
 * ============
 *   Scheduler init  : BLESwitch__Init()    AFTER BLEComm__Init()
 *   Scheduler task  : BLESwitch__Handler() AFTER BLEComm__Handler()
 *   Both on Core 0, 50ms task.
 *
 * @version 2.1.0
 *
 * v2.1.0 changes (review fixes):
 *   - BLESwitch__ForceOff() added -- allows external callers (low-battery,
 *     OTA, factory reset) to stop BLE cleanly through the state machine
 *   - Header comments: non-ASCII arrow characters replaced with ASCII
 */

#ifndef BLE_SWITCH_H
#define BLE_SWITCH_H

#include <stdbool.h>
#include <stdint.h>

/*==============================================================================
 *                          PUBLIC API
 *============================================================================*/

/**
 * @brief Initialise BLE switch module.
 * @details Detects cold vs warm boot via esp_reset_reason().
 *          Cold boot : BLE stays OFF, waits for switch short press.
 *          Warm reset: starts BLE advertising + 2-min auto-off timer.
 *          Must be called AFTER BLEComm__Init().
 */
void BLESwitch__Init(void);

/**
 * @brief Periodic BLE switch handler -- call every 50ms from Core 0 task.
 * @details Runs the full state machine:
 *          switch debounce / edge detection / hold detection,
 *          2-min timer, connect/disconnect polling.
 */
void BLESwitch__Handler(void);

/**
 * @brief Query whether BLE is currently enabled (advertising or connected).
 * @return true  BLE is active (advertising or client connected).
 * @return false BLE is off.
 * @note   s_bleEnabled is volatile -- safe to read from Core 1 (LEDHMI task).
 */
bool BLESwitch__IsBLEEnabled(void);

/**
 * @brief Force BLE off from an external caller.
 * @details Stops advertising, disconnects any active client, and transitions
 *          the state machine to BLE_OFF.  Use for low-battery conditions,
 *          OTA updates, or factory reset sequences.
 *          Safe to call from Core 0 scheduler task context only.
 */
void BLESwitch__ForceOff(void);

#endif /* BLE_SWITCH_H */