/**
 * @file App.h
 * @brief Application Module Interface
 * @details Public interface for main application logic.
 *
 *          Sensor interface : TTL direct → CmdParser → currentData
 *          Communication    : RS485 (Modbus) monitoring only
 *
 * @version 3.0.0
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
 * @brief Initialize Application Module
 * @details Must be called once during system initialization before
 *          Scheduler_Init(). Resets all error flags and counters.
 */
void App__Init(void);

/**
 * @brief Main Application Handler
 * @details Call periodically from scheduler task.
 *          Runs communication monitoring and sensor data acquisition.
 * @note    Period controlled by APP_DISPLAY_UPDATE_INTERVAL in App_Cfg.h
 */
void App__Handler(void);

/**
 * @brief RTC periodic handler
 * @details Call from scheduler Task_100ms.
 *          Reads RTC every 1 second and updates currentData time fields.
 */
void App__RTCHandler(void);

/*──────────────────────────────────────────────────────────────────────────────
 *  Status Query APIs
 *────────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Get RS485 communication error status
 * @return true if Modbus RS485 not initialized or communication error
 * @return false if RS485 communication normal
 */
bool App__IsRS485CommError(void);

/*──────────────────────────────────────────────────────────────────────────────
 *  Utility APIs
 *────────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Clear all error flags
 * @details Resets RS485 error flag and display counter
 */
void App__ClearErrors(void);
void App__SetFanManual(float voltage);
void App__SetFanAuto(void);
bool App__IsFanManual(void);


#endif /* APP_H */