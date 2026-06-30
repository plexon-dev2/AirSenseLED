/**
 * @file SystemStates_Cfg.h
 * @brief System States Module Configuration
 * @details Configuration parameters for system state machine
 *
 * @author Generated Module
 * @date 2025-12-02
 * @version 2.0.0
 */

#ifndef SYSTEMSTATES_CFG_H
#define SYSTEMSTATES_CFG_H

/*==============================================================================
 *                              INCLUDES
 *============================================================================*/
#include <stdint.h>
#include <stdbool.h>

/*==============================================================================
 *                              DEFINES
 *============================================================================*/

/**
 * @defgroup SystemStates_Timing Timing Configuration
 * @brief Timing parameters for state machine
 * @{
 */

/**
 * @brief Communication timeout in milliseconds
 * @details Maximum time allowed without valid data before flagging timeout
 * @note If no data received within this time, system transitions to failure state
 */
#define SYSTEMSTATES_COMM_TIMEOUT_MS (5000U)

/**
 * @brief Health check interval
 * @details Number of handler calls between health checks
 * @note At 50ms task rate: 100 counts = 5 seconds
 */
#define SYSTEMSTATES_HEALTH_CHECK_INTERVAL (100U)

/**
 * @brief Failure recovery delay in milliseconds
 * @details Time to wait before attempting recovery from failure state
 * @note Allows system to stabilize before retrying
 */
#define SYSTEMSTATES_FAILURE_RECOVERY_DELAY_MS (2000U)

/** @} */

/**
 * @defgroup SystemStates_ErrorHandling Error Handling Configuration
 * @brief Parameters for error detection and handling
 * @{
 */

/**
 * @brief Maximum consecutive errors before failure
 * @details Number of consecutive errors before transitioning to failure state
 * @note Prevents nuisance trips from transient errors
 */
#define SYSTEMSTATES_MAX_CONSECUTIVE_ERRORS (5U)

/**
 * @brief Maximum total error count
 * @details Total error count threshold requiring manual intervention
 * @note System requires manual reset if this count is exceeded
 */
#define SYSTEMSTATES_MAX_ERROR_COUNT (50U)

/** @} */

/**
 * @defgroup SystemStates_Features Feature Configuration
 * @brief Enable/disable system state features
 * @{
 */

/**
 * @brief Enable debug output
 * @details Set to 1 to enable debug messages, 0 to disable
 */
#define SYSTEMSTATES_DEBUG_ENABLE (1)

/**
 * @brief Enable automatic recovery
 * @details Set to 1 to enable automatic recovery from errors
 * @note If disabled, manual intervention required after errors
 */
#define SYSTEMSTATES_AUTO_RECOVERY_ENABLE (1)

/**
 * @brief Enable statistics tracking
 * @details Set to 1 to track state machine statistics
 */
#define SYSTEMSTATES_STATISTICS_ENABLE (1)

/**
 * @brief Initialization delay in milliseconds
 * @details Time to wait in INIT state before transitioning to NORMAL
 */
#define SYSTEMSTATES_INIT_DELAY_MS (1000U)

/**
 * @brief Maximum recovery attempts before entering extended failure mode
 */
#define SYSTEMSTATES_MAX_RECOVERY_ATTEMPTS (10U)

/** @} */

/*==============================================================================
 *                          VALIDATION MACROS
 *============================================================================*/

#if (SYSTEMSTATES_COMM_TIMEOUT_MS < 1000U) || (SYSTEMSTATES_COMM_TIMEOUT_MS > 60000U)
#error "SYSTEMSTATES_COMM_TIMEOUT_MS must be between 1000 and 60000 milliseconds"
#endif

#if (SYSTEMSTATES_HEALTH_CHECK_INTERVAL < 10U) || (SYSTEMSTATES_HEALTH_CHECK_INTERVAL > 1000U)
#error "SYSTEMSTATES_HEALTH_CHECK_INTERVAL must be between 10 and 1000"
#endif

#if (SYSTEMSTATES_MAX_CONSECUTIVE_ERRORS < 1U) || (SYSTEMSTATES_MAX_CONSECUTIVE_ERRORS > 100U)
#error "SYSTEMSTATES_MAX_CONSECUTIVE_ERRORS must be between 1 and 100"
#endif

#if (SYSTEMSTATES_MAX_ERROR_COUNT < 10U) || (SYSTEMSTATES_MAX_ERROR_COUNT > 1000U)
#error "SYSTEMSTATES_MAX_ERROR_COUNT must be between 10 and 1000"
#endif

#endif /* SYSTEMSTATES_CFG_H */