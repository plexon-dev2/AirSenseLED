/**
 * @file HardwareInit.h
 * @brief Hardware GPIO Safe-State Initialization
 * @details Call HardwareInit__Init() as the FIRST line in setup() before any
 *          module initialisation.  Drives all output pins to a defined safe
 *          state to prevent undefined hardware behaviour during boot.
 *
 *          Uses HardwareConfig.h for all pin numbers and safe-state levels.
 *          Do NOT hardcode pin numbers or levels here.
 *
 * @version 2.2.0
 *
 * v2.2.0 changes (review fixes):
 *   - HW_PIN_SW_BLE: corrected from OUTPUT to INPUT_PULLUP. Was incorrectly
 *     configured as OUTPUT, which fights the external push-button and risks
 *     GPIO damage when the switch is pressed. Physical switch = INPUT.
 *   - HW_DEFAULT_* macros: these are now defined in HardwareConfig.h.
 *     Previously were referenced here but never defined anywhere -- compile
 *     failure on every build.
 *   - Non-ASCII characters in comments replaced with ASCII equivalents.
 */

#ifndef HARDWARE_INIT_H
#define HARDWARE_INIT_H

#include <Arduino.h>
#include "HardwareConfig.h"

/**
 * @brief Drive all hardware peripherals to safe state.
 * @details Must be called as the first line of setup(), before Serial.begin()
 *          and before any module Init() function. Ensures no GPIO floats in an
 *          undefined state during the boot sequence.
 *
 *          Input pins (switches, status monitors, I2C, RTC INT) are configured
 *          with INPUT_PULLUP where appropriate -- no safe-state level to drive.
 *
 * @note    Inline implementation in header -- acceptable for C++ Arduino project.
 *          In a multi-TU C project this would require a .c definition file.
 */
static inline void HardwareInit__Init(void)
{
    /* ── Status LEDs -- all OFF at startup ─────────────────────────────── */
    pinMode(HW_PIN_LED_BLE,    OUTPUT); digitalWrite(HW_PIN_LED_BLE,    HW_DEFAULT_LED_BLE);
    pinMode(HW_PIN_LED_ALARM,  OUTPUT); digitalWrite(HW_PIN_LED_ALARM,  HW_DEFAULT_LED_ALARM);
    pinMode(HW_PIN_LED_ERROR,  OUTPUT); digitalWrite(HW_PIN_LED_ERROR,  HW_DEFAULT_LED_ERROR);
    pinMode(HW_PIN_LED_POWER,  OUTPUT); digitalWrite(HW_PIN_LED_POWER,  HW_DEFAULT_LED_POWER);
    pinMode(HW_PIN_LED_CPU,    OUTPUT); digitalWrite(HW_PIN_LED_CPU,    HW_DEFAULT_LED_CPU);
    pinMode(HW_PIN_LED_MODBUS, OUTPUT); digitalWrite(HW_PIN_LED_MODBUS, HW_DEFAULT_LED_MODBUS);

    /* ── Buzzer -- silent ──────────────────────────────────────────────── */
    pinMode(HW_PIN_BUZZER, OUTPUT);
    digitalWrite(HW_PIN_BUZZER, HW_DEFAULT_BUZZER);

    /* ── Fan -- stopped ────────────────────────────────────────────────── */
    pinMode(HW_PIN_FAN_PWM, OUTPUT);
    digitalWrite(HW_PIN_FAN_PWM, HW_DEFAULT_FAN_PWM);

    /* ── RS485 -- receive mode (LOW=RX for THVD1406 auto-direction IC) ─── */
    pinMode(HW_PIN_RS485_DIR, OUTPUT);
    digitalWrite(HW_PIN_RS485_DIR, HW_DEFAULT_RS485_DIR);

    /* ── Sensor power supply -- OFF until SensorBoot enables it ─────────── */
    pinMode(HW_PIN_SENSOR_POWER, OUTPUT);
    digitalWrite(HW_PIN_SENSOR_POWER, HW_DEFAULT_SENSOR_POWER);

    /* ── BLE switch -- INPUT_PULLUP (physical push-button, active LOW) ──── */
    /* NOTE: was incorrectly OUTPUT in v2.1.0 -- driving an output against  */
    /*       a pressed switch causes excessive current and potential damage.  */
    pinMode(HW_PIN_SW_BLE, INPUT_PULLUP);

    /* ── RTC INT -- input with pullup (open-drain, active low) ─────────── */
    /* GPIO3 is a strapping pin -- pullup ensures HIGH at reset (safe level) */
    pinMode(HW_PIN_RTC_INT, INPUT_PULLUP);

    /* ── I2C pins -- Wire.begin() will reconfigure; pullup as interim ───── */
    pinMode(HW_PIN_RTC_SDA, INPUT_PULLUP);
    pinMode(HW_PIN_RTC_SCL, INPUT_PULLUP);

    /* ── ISO V STATUS -- input (RS485 PSU fault monitor, HIGH=fault) ────── */
    /* No pullup -- externally driven by isolated power supply circuit       */
    pinMode(HW_PIN_ISO_V_STATUS, INPUT);

    /* ── DMS / DPS -- inputs ─────────────────────────────────────────────── */
    pinMode(HW_PIN_DMS, INPUT);
    pinMode(HW_PIN_DPS, INPUT);
}

#endif /* HARDWARE_INIT_H */