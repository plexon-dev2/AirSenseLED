/**
 * @file CurrentSense.h
 * @brief Current Sense Module Interface -- AirSense ESP32-S3
 *
 * Measures current drawn by Fan and Sensor board via low-side shunt resistors.
 *
 * Hardware:
 *   GPIO1 (ADC1_CH0) -- Fan current,    0.1 ohm shunt, RC filter (0.2uF to GND)
 *   GPIO2 (ADC1_CH1) -- Sensor current, 0.2 ohm shunt, RC filter (0.2uF to GND)
 *
 * Method:
 *   64-sample oversampling + esp_adc_cal calibration + 8-point rolling average.
 *   ADC attenuation = 0dB (max ~950mV full-scale -- safe for <100mV shunt voltage).
 *
 * Usage:
 *   CurrentSense__Init();          // call once in setup / init sequence
 *   CurrentSense__Handler();       // call every 50ms task cycle
 *   CurrentSense__GetFanMA();      // returns fan current in mA
 *   CurrentSense__GetSensorMA();   // returns sensor board current in mA
 *   CurrentSense__IsFanOC();       // true if fan over-current
 *   CurrentSense__IsSensorOC();    // true if sensor over-current
 *   CurrentSense__IsInitialized(); // true after Init() completes successfully
 *
 * @version 1.1.0
 *
 * v1.1.0 changes (review fixes):
 *   - CurrentSense__IsInitialized() added to public API
 *   - Header: non-ASCII characters replaced with ASCII equivalents
 */

#ifndef CURRENT_SENSE_H
#define CURRENT_SENSE_H

#include <stdint.h>
#include <stdbool.h>

/*==============================================================================
 *                          PUBLIC API
 *============================================================================*/

/**
 * @brief Initialise ADC channels and calibration.
 *        Call once before CurrentSense__Handler().
 *        Safe to call before FreeRTOS scheduler starts.
 */
void CurrentSense__Init(void);

/**
 * @brief Periodic handler -- call every 50ms scheduler cycle.
 *        Samples ADC at CFG_CURRENT_SAMPLE_INTERVAL rate,
 *        updates rolling averages, checks alarm thresholds,
 *        publishes to MQTT and BLE at configured intervals.
 */
void CurrentSense__Handler(void);

/**
 * @brief Get latest fan current reading.
 * @return uint16_t Current in milliamps (mA), 0 if not yet sampled.
 */
uint16_t CurrentSense__GetFanMA(void);

/**
 * @brief Get latest sensor board current reading.
 * @return uint16_t Current in milliamps (mA), 0 if not yet sampled.
 */
uint16_t CurrentSense__GetSensorMA(void);

/**
 * @brief Check fan over-current condition.
 * @return true if fan current >= CFG_CURRENT_FAN_ALARM_MA.
 */
bool CurrentSense__IsFanOC(void);

/**
 * @brief Check sensor board over-current condition.
 * @return true if sensor current >= CFG_CURRENT_SENSOR_ALARM_MA.
 */
bool CurrentSense__IsSensorOC(void);

/**
 * @brief Get last JSON string built for MQTT/BLE publish.
 * @param[out] buf   Output buffer (must not be NULL).
 * @param[in]  size  Buffer size in bytes (must be > 0).
 */
void CurrentSense__GetJSON(char *buf, uint16_t size);

/**
 * @brief Check whether the module has been successfully initialised.
 * @return true  Init() completed successfully.
 * @return false Init() has not been called.
 */
bool CurrentSense__IsInitialized(void);

#endif /* CURRENT_SENSE_H */