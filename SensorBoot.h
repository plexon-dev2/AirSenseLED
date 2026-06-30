/**
 * @file SensorBoot.h
 * @brief Sensor Bootup Time Measurement — AirSense ESP32-S3
 *
 * Replaces the fixed delay(APP_SENSOR_WARMUP_MS) in App__Init() with an
 * active poll that exits as soon as the sensor responds, and records the
 * actual time taken.
 *
 * Bootup time is:
 *   - Printed to Serial
 *   - Published via MQTT to CFG_MQTT_TOPIC_BOOTTIME
 *   - Available via BLE JSON on first sensor data packet (field "boot_ms")
 *
 * Usage (in App__Init or Scheduler init, BEFORE CmdParser__Init):
 *   SensorBoot__MeasureAndWait();   // blocks until sensor ready or timeout
 *   uint32_t ms = SensorBoot__GetBootTimeMS();  // read result anytime after
 *
 * @version 1.0.0
 */

#ifndef SENSOR_BOOT_H
#define SENSOR_BOOT_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Power-on the sensor and block until first valid response or timeout.
 * @details Powers sensor via APP_SENSOR_POWER_PIN, then polls Modbus/CmdParser
 *          every CFG_BOOT_POLL_INTERVAL_MS until a valid frame is received
 *          or CFG_BOOT_SENSOR_TIMEOUT_MS elapses.
 *          Records actual boot time. Replaces fixed delay(3000).
 * @note Call BEFORE CmdParser__Init() and Modbus_Init() — this function
 *       initialises them internally for the boot probe, then leaves them ready.
 */
void SensorBoot__MeasureAndWait(void);

/**
 * @brief Get measured sensor bootup time.
 * @return uint32_t Boot time in milliseconds, 0 if not yet measured,
 *         CFG_BOOT_SENSOR_TIMEOUT_MS if sensor timed out.
 */
uint32_t SensorBoot__GetBootTimeMS(void);

/**
 * @brief Check if sensor responded within timeout.
 * @return true if sensor responded, false if timed out.
 */
bool SensorBoot__SensorReady(void);

/**
 * @brief Publish bootup time via MQTT and BLE (call after WiFi/BLE init).
 * @details Safe to call multiple times — publishes only once.
 */
void SensorBoot__PublishResult(void);

#endif /* SENSOR_BOOT_H */
