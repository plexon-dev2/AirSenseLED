/**
 * @file AppMutex.h
 * @brief Shared synchronisation objects and debug print control.
 * @details Provides:
 *            - g_serial_mutex   : FreeRTOS mutex guarding Serial output across cores
 *            - g_setup_complete : Flag released by setup() once all modules are ready;
 *                                 tasks block on this before entering their run loops
 *            - SERIAL_PRINTF    : Mutex-guarded Serial.printf (no-op in production)
 *
 *          g_serial_mutex and g_setup_complete are DEFINED in AppMutex.cpp.
 *
 * @version 1.2.0
 *
 * v1.2.0 changes:
 *   - g_setup_complete definition moved from .ino to AppMutex.cpp (proper TU)
 *   - SERIAL_PRINTF null check changed from pointer-as-bool to != NULL (MISRA R2)
 *   - APP_SERIAL_BAUD_RATE defined here for single-point maintenance
 */

#ifndef APP_MUTEX_H
#define APP_MUTEX_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

/*==============================================================================
 *                          SHARED OBJECTS
 *============================================================================*/

/** @brief FreeRTOS mutex protecting Serial output shared between Core 0 and Core 1. */
extern SemaphoreHandle_t g_serial_mutex;

/**
 * @brief Setup completion gate.
 * @details Set to true by setup() after Scheduler_Start().
 *          All FreeRTOS tasks must poll or wait on this flag before executing
 *          their run-loop body.
 * @note    For stricter dual-core safety, replace with an EventGroup --
 *          see recommendation in code review. volatile + portMEMORY_BARRIER()
 *          is used at read sites until that migration is done.
 */
extern volatile bool g_setup_complete;

/*==============================================================================
 *                          HARDWARE CONFIGURATION
 *============================================================================*/

/** @brief UART baud rate for Serial debug output. */
#define APP_SERIAL_BAUD_RATE    (115200U)

/*==============================================================================
 *                          DEBUG PRINT CONTROL
 *============================================================================*/

/**
 * @brief Set to 1 for debug builds, 0 for production.
 * @details When 0, SERIAL_PRINTF expands to nothing -- eliminates Serial
 *          contention between Core 0 (Modbus) and Core 1 (LEDHMI) that
 *          causes Modbus RX byte corruption.
 */
#define APP_DEBUG_PRINT_ENABLE  (0U)

#if (APP_DEBUG_PRINT_ENABLE == 1U)
/**
 * @brief Mutex-guarded Serial.printf for safe use from any FreeRTOS task.
 * @details Acquires g_serial_mutex with a 10 ms timeout.  If the mutex is
 *          unavailable within the timeout the message is silently dropped --
 *          this is acceptable for debug output; do not rely on it for
 *          safety-critical logging.
 * @note    Variadic macro -- MISRA C:2012 Rule 20.10 advisory deviation.
 *          Rationale: no compliant alternative exists for a printf-style
 *          debug wrapper; deviation is documented and isolated to this file.
 */
#define SERIAL_PRINTF(...) \
    do { \
        if ((g_serial_mutex != NULL) && \
            (xSemaphoreTake(g_serial_mutex, pdMS_TO_TICKS(10)) == pdTRUE)) \
        { \
            Serial.printf(__VA_ARGS__); \
            xSemaphoreGive(g_serial_mutex); \
        } \
    } while(0)
#else
#define SERIAL_PRINTF(...)  /* Disabled for production -- prevents Modbus RX corruption */
#endif

#endif /* APP_MUTEX_H */