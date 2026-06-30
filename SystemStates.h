/**
 * @file SystemStates.h
 * @brief System State Machine Interface
 * @details Public interface for system state management
 *
 * @author Generated Module
 * @date 2025-12-02
 * @version 2.0.0
 */

#ifndef SYSTEMSTATES_H
#define SYSTEMSTATES_H

/*==============================================================================
 *                              INCLUDES
 *============================================================================*/
#include <stdint.h>
#include <stdbool.h>
#include "SystemStates_Cfg.h"

/*==============================================================================
 *                              TYPEDEFS
 *============================================================================*/

/**
 * @brief System state enumeration
 * @details Defines all possible system states
 */
typedef enum
{
    SYSTEMSTATES_INIT    = 0U,    /**< Initialization state */
    SYSTEMSTATES_NORMAL  = 1U,    /**< Normal operation state */
    SYSTEMSTATES_FAILURE = 2U,    /**< Failure/error state */
    SYSTEMSTATES_INVALID = 3U     /**< Invalid state (should never occur) */
} SystemStates_State_t;

/**
 * @brief System error code enumeration
 * @details Defines all possible error conditions
 */
typedef enum
{
    SYSTEMSTATES_ERROR_NONE                 = 0U,          /**< No error */
    SYSTEMSTATES_ERROR_COMMUNICATION        = 1U,		   /**< Communication error */
    SYSTEMSTATES_ERROR_TIMEOUT              = 2U,          /**< Communication timeout */
    SYSTEMSTATES_ERROR_INVALID_DATA         = 3U,          /**< Invalid data received */
    SYSTEMSTATES_ERROR_MODBUS               = 4U,          /**< Modbus error */
    SYSTEMSTATES_ERROR_PARSER 				= 5U,          /**< Parser error */
    SYSTEMSTATES_ERROR_POWER_SUPPLY 		= 6U,          /**< Power supply error */
    SYSTEMSTATES_ERROR_HARDWARE 			= 7U           /**< Hardware error */
} SystemStates_ErrorCode_t;

/*==============================================================================
 *                          FUNCTION PROTOTYPES
 *============================================================================*/

/**
 * @defgroup SystemStates_SchedulerAPIs Scheduler Interface APIs
 * @brief APIs called by the scheduler for module operation
 * @{
 */

/**
 * @brief Initialize System States Module
 * @details Must be called once during system initialization
 *
 * @note This API is called by the scheduler during system initialization
 */
void SystemStates__Init(void);

/**
 * @brief Main System States Handler
 * @details Should be called periodically by the scheduler
 *
 * @note This API is called by the scheduler in the main execution loop
 */
void SystemStates__Handler(void);

/** @} */

/**
 * @defgroup SystemStates_StateAPIs State Management APIs
 * @brief APIs for managing system state
 * @{
 */

/**
 * @brief Get current system state
 *
 * @return Current system state
 */
SystemStates_State_t SystemStates__GetState(void);

/**
 * @brief Set system state
 * @details Forces a state transition
 *
 * @param new_state State to transition to
 *
 * @note Use with caution - state machine normally manages transitions
 */
void SystemStates__SetState(SystemStates_State_t new_state);

/** @} */

/**
 * @defgroup SystemStates_ErrorAPIs Error Management APIs
 * @brief APIs for error handling and status
 * @{
 */

/**
 * @brief Get last error code
 *
 * @return Last detected error code
 */
SystemStates_ErrorCode_t SystemStates__GetLastError(void);

/**
 * @brief Get total error count
 *
 * @return Total number of errors detected since initialization
 */
uint32_t SystemStates__GetErrorCount(void);

/**
 * @brief Clear all errors and reset counters
 * @details Resets error counters and clears error flags in all modules
 */
void SystemStates__ClearErrors(void);

/** @} */

/**
 * @defgroup SystemStates_DiagnosticAPIs Diagnostic APIs
 * @brief APIs for system health monitoring
 * @{
 */

/**
 * @brief Perform diagnostic status check
 * @details Checks all system components for errors
 *
 * @return true if all diagnostics pass
 * @return false if any diagnostic fails
 *
 * @note Checks communication, power supply, and module initialization
 */
bool SystemStates__DiagnosticStatus(void);

/**
 * @brief Force system reset from failure state (manual intervention)
 */
void SystemStates__ForceReset(void);

/** @} */

#endif // SYSTEMSTATES_H