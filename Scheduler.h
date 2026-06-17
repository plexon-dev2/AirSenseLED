/**
 * @file Scheduler.h
 * @brief FreeRTOS Scheduler Interface — LED Project
 * @details Declares the four lifecycle functions called from setup()
 *          and the task functions arrays used internally.
 *
 *          Core 0: CmdParser, Modbus, App, BLEComm, Fan, Buzzer
 *          Core 1: LEDHMI, RTC tick handler
 *
 * CHANGES FROM TFT BUILD:
 * =======================
 *   - Removed: DisplayProcess references
 *   - Removed: SCREENS_ID, BLEScreen navigation types
 *   - Removed: Scheduler_HandleScreenChange()
 *   - Removed: Scheduler_HandleBLEDirtyFlag()   (TFT-only cross-core screen update)
 *   - Removed: Scheduler_HandleBitmapReady()    (TFT-only bitmap pre-render logic)
 *
 * @version 2.0.0 — LED project
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

/*==============================================================================
 *                          LIFECYCLE FUNCTIONS
 *  Called from setup() in AirSense_LEDBased_1V0.ino in this exact order:
 *    1. Scheduler_ExecuteInitCore1()   — Core 1 peripherals (LEDHMI, BLE)
 *    2. RTC_HAL_I2C_Begin() + RTC__Init() + attachInterrupt()  ← in .ino directly
 *    3. Scheduler_ExecuteInitCore0()   — Core 0 peripherals (CmdParser, Modbus, Fan, App)
 *    4. Scheduler_Init()               — creates FreeRTOS tasks
 *    5. Scheduler_Start()              — releases g_setup_complete gate
 *============================================================================*/

/**
 * @brief Run Core 1 module init functions (LEDHMI, BLEComm)
 * @details Must be called before Scheduler_ExecuteInitCore0().
 */
void Scheduler_ExecuteInitCore1(void);

/**
 * @brief Run Core 0 module init functions (CmdParser, Modbus, Fan, Buzzer, App)
 */
void Scheduler_ExecuteInitCore0(void);

/**
 * @brief Create all FreeRTOS tasks
 * @return true on success, false if any task creation fails
 */
bool Scheduler_Init(void);

/**
 * @brief Release g_setup_complete gate and start scheduler operation
 */
void Scheduler_Start(void);

#endif /* SCHEDULER_H */
