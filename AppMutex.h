/**
 * @file AppMutex.h
 * @brief Setup completion flag and debug print control
 * @details Core 0 and Core 1 tasks wait on g_setup_complete before running
 *          their handlers. Defined in AirSense_DualCore.ino.
 */

#ifndef APP_MUTEX_H
#define APP_MUTEX_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

extern SemaphoreHandle_t g_serial_mutex;
extern volatile bool g_setup_complete;

/*==============================================================================
 *                          DEBUG PRINT CONTROL
 *============================================================================*/
/**
 * @brief Set to 0 for production, 1 for debug
 * @details When disabled, eliminates Serial contention between Core 0 (Modbus)
 *          and Core 1 (LEDHMI) that causes Modbus RX byte corruption.
 */
#define APP_DEBUG_PRINT_ENABLE  (0U)

#if (APP_DEBUG_PRINT_ENABLE == 1U)
#define SERIAL_PRINTF(...) \
    do { \
        if (g_serial_mutex && xSemaphoreTake(g_serial_mutex, pdMS_TO_TICKS(10)) == pdTRUE) { \
            Serial.printf(__VA_ARGS__); \
            xSemaphoreGive(g_serial_mutex); \
        } \
    } while(0)
#else
#define SERIAL_PRINTF(...)  /* Disabled for production - prevents Modbus RX corruption */
#endif

#endif /* APP_MUTEX_H */