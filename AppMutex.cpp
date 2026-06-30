/**
 * @file AppMutex.cpp
 * @brief Defines shared synchronisation objects declared in AppMutex.h.
 * @details Provides the single definition point for:
 *            - g_serial_mutex   : FreeRTOS mutex for Serial output protection
 *            - g_setup_complete : Setup gate flag consumed by all FreeRTOS tasks
 *
 *          Previously g_setup_complete was defined in the .ino translation unit.
 *          Moved here (v1.2.0) so that the extern declaration in AppMutex.h has
 *          a proper corresponding definition in a standard .cpp translation unit,
 *          consistent with MISRA C:2012 Rule 8.6.
 *
 * @version 1.2.0
 */

#include "AppMutex.h"   /* single include -- duplicate removed (was included twice) */

SemaphoreHandle_t g_serial_mutex   = NULL;
volatile bool     g_setup_complete = false;