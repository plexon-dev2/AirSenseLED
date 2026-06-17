/**
 * @file AppMutex.cpp
 * @brief Setup completion flag translation unit
 * @details g_setup_complete is defined in AirSense_DualCore.ino.
 *          This file exists only to satisfy the build system — it provides
 *          no additional definitions.
 */

#include "AppMutex.h"
#include "AppMutex.h"

SemaphoreHandle_t g_serial_mutex = NULL;