/**
 * @file App.h
 * @brief Application Module Interface
 * @details Public interface for main application logic.
 *
 *          Sensor interface : TTL direct -> CmdParser -> currentData
 *          Communication    : RS485 (Modbus) monitoring only
 *
 * @version 3.17.0
 */

#ifndef APP_H
#define APP_H

/*==============================================================================
 *                              INCLUDES
 *============================================================================*/
#include <stdint.h>
#include <stdbool.h>
#include "App_Cfg.h"

/*==============================================================================
 *                          FUNCTION PROTOTYPES
 *============================================================================*/

/*──────────────────────────────────────────────────────────────────────────────
 *  Scheduler Interface APIs
 *────────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Initialize Application Module.
 * @details Must be called once during system initialization before
 *          Scheduler_Init().  Resets all error flags, counters, and fan state.
 *          Registers MQTT command callback and subscriptions.
 */
void App__Init(void);

/**
 * @brief Main Application Handler.
 * @details Call periodically from scheduler task.
 *          Dispatches: communication monitor, fan speed update,
 *          BLE switch state machine, sensor data acquisition and publish.
 * @note    Sensor update period controlled by APP_DISPLAY_UPDATE_INTERVAL
 *          in App_Cfg.h.
 */
void App__Handler(void);

/**
 * @brief RTC periodic handler.
 * @details Reserved for scheduler Task_100ms.  Currently a no-op;
 *          RTC timestamp is published inside App__Handler() each sensor cycle.
 *          Implement here if a decoupled 1Hz RTC read is required.
 */
void App__RTCHandler(void);

/*──────────────────────────────────────────────────────────────────────────────
 *  Status Query APIs
 *────────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Get RS485 communication error status.
 * @return true  Modbus not initialized or communication error detected.
 * @return false RS485 communication normal.
 */
bool App__IsRS485CommError(void);

/**
 * @brief Query whether BLE advertising is currently permitted.
 * @return true  BLE is advertising, connected, or in disconnected-but-active state.
 * @return false BLE is fully stopped (BLE_SW_OFF).
 */
bool App__IsBLEAdvertisingAllowed(void);

/*──────────────────────────────────────────────────────────────────────────────
 *  Fan Control APIs
 *────────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Set fan to manual voltage control.
 * @details Overrides automatic AQI-based speed selection.
 *          Sets both the manual mode flag and immediately applies @p voltage.
 * @param voltage  Target fan voltage in volts.
 */
void App__SetFanManual(float voltage);

/**
 * @brief Return fan to automatic AQI-based speed control.
 * @details Clears both manual mode flag and BLE-direct flag.
 */
void App__SetFanAuto(void);

/**
 * @brief Signal that BLE has taken direct duty-cycle control of the fan.
 * @details When set, App__Handler() skips the automatic fan update entirely.
 *          Call App__SetFanAuto() to release BLE control.
 */
void App__SetFanManualFlag(void);

/**
 * @brief Query whether the fan is under any form of manual control.
 * @return true  Fan is in voltage-manual mode or BLE-direct mode.
 * @return false Fan is under automatic AQI-based control.
 */
bool App__IsFanManual(void);

/*──────────────────────────────────────────────────────────────────────────────
 *  Utility APIs
 *────────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Clear all latched error flags.
 * @details Resets RS485 error flag.
 */
void App__ClearErrors(void);


#endif /* APP_H */