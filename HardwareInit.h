/**
 * @file HardwareInit.h
 * @brief Hardware GPIO Safe-State Initialization
 * @details Call HardwareInit__Init() as FIRST line in setup().
 *          Uses HardwareConfig.h for all pin numbers.
 * @version 2.1.0
 */

#ifndef HARDWARE_INIT_H
#define HARDWARE_INIT_H

#include <Arduino.h>
#include "HardwareConfig.h"

static inline void HardwareInit__Init(void)
{
    /* ── Status LEDs — all OFF ──────────────────────────────────────────── */
    pinMode(HW_PIN_LED_BLE,    OUTPUT); digitalWrite(HW_PIN_LED_BLE,    HW_DEFAULT_LED_BLE);
    pinMode(HW_PIN_LED_ALARM,  OUTPUT); digitalWrite(HW_PIN_LED_ALARM,  HW_DEFAULT_LED_ALARM);
    pinMode(HW_PIN_LED_ERROR,  OUTPUT); digitalWrite(HW_PIN_LED_ERROR,  HW_DEFAULT_LED_ERROR);
    pinMode(HW_PIN_LED_POWER,  OUTPUT); digitalWrite(HW_PIN_LED_POWER,  HW_DEFAULT_LED_POWER);
    pinMode(HW_PIN_LED_CPU,    OUTPUT); digitalWrite(HW_PIN_LED_CPU,    HW_DEFAULT_LED_CPU);
    pinMode(HW_PIN_LED_MODBUS, OUTPUT); digitalWrite(HW_PIN_LED_MODBUS, HW_DEFAULT_LED_MODBUS);

    /* ── Buzzer — silent ────────────────────────────────────────────────── */
    pinMode(HW_PIN_BUZZER, OUTPUT);
    digitalWrite(HW_PIN_BUZZER, HW_DEFAULT_BUZZER);

    /* ── Fan — stopped ──────────────────────────────────────────────────── */
    pinMode(HW_PIN_FAN_PWM, OUTPUT);
    digitalWrite(HW_PIN_FAN_PWM, HW_DEFAULT_FAN_PWM);

    /* ── RS485 — receive mode ───────────────────────────────────────────── */
    pinMode(HW_PIN_RS485_DIR, OUTPUT);
    digitalWrite(HW_PIN_RS485_DIR, HW_DEFAULT_RS485_DIR);

    /* ── Sensor power — OFF ─────────────────────────────────────────────── */
    pinMode(HW_PIN_SENSOR_POWER, OUTPUT);
    digitalWrite(HW_PIN_SENSOR_POWER, HW_DEFAULT_SENSOR_POWER);

    /* ── BLE SW — disabled ──────────────────────────────────────────────── */
    pinMode(HW_PIN_SW_BLE, OUTPUT);
    digitalWrite(HW_PIN_SW_BLE, HW_DEFAULT_SW_BLE);

    /* ── RTC INT — input with pullup ────────────────────────────────────── */
    pinMode(HW_PIN_RTC_INT, INPUT_PULLUP);

    /* ── I2C pins — inputs (Wire.begin() configures) ────────────────────── */
    pinMode(HW_PIN_RTC_SDA, INPUT_PULLUP);
    pinMode(HW_PIN_RTC_SCL, INPUT_PULLUP);
}

#endif /* HARDWARE_INIT_H */
