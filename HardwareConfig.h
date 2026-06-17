/**
 * @file HardwareConfig.h
 * @brief Master Hardware GPIO Configuration — AirSense ESP32-S3 LED Build
 *
 * @details SINGLE SOURCE OF TRUTH for all GPIO pin assignments.
 *          All module config files (RTC_Cfg.h, AQ_LEDHMI_Cfg.h, App_Cfg.h,
 *          Modbus_Cfg.h, Fan_Cfg.h, Buzzer_Cfg.h, etc.) must include this
 *          file and reference these macros — never hardcode pin numbers.
 *
 *          To change a pin: edit ONLY this file.
 *          All modules pick up the change automatically on next build.
 *
 * VERIFIED PIN MAP (hardware schematic rev 1.0):
 * ===============================================
 *
 *  GPIO  │ Signal Name        │ Direction │ Module
 * ───────┼────────────────────┼───────────┼─────────────────
 *  GPIO0  │ LED_BLE            │ OUT       │ AQ_LEDHMI
 *  GPIO3  │ RTC_INT            │ IN (PU)   │ RTC
 *  GPIO4  │ SENSOR_UART_TX     │ OUT (UART)│ CmdParser
 *  GPIO5  │ SENSOR_UART_RX     │ IN  (UART)│ CmdParser
 *  GPIO6  │ FAN_PWM            │ OUT (PWM) │ Fan
 *  GPIO8  │ SW_BLE             │ OUT       │ BLEComm
 *  GPIO15 │ RTC_SDA            │ I2C       │ RTC
 *  GPIO16 │ RTC_SCL            │ I2C       │ RTC
 *  GPIO17 │ RS485_UART_RX      │ IN  (UART)│ Modbus
 *  GPIO18 │ RS485_UART_TX      │ OUT (UART)│ Modbus
 *  GPIO21 │ RS485_DIR          │ OUT       │ Modbus
 *  GPIO35 │ LED_ALARM          │ OUT       │ AQ_LEDHMI
 *  GPIO36 │ SENSOR_POWER       │ OUT       │ App
 *  GPIO37 │ LED_ERROR          │ OUT       │ AQ_LEDHMI
 *  GPIO38 │ LED_POWER          │ OUT       │ AQ_LEDHMI
 *  GPIO39 │ LED_CPU            │ OUT       │ AQ_LEDHMI
 *  GPIO40 │ LED_MODBUS         │ OUT       │ AQ_LEDHMI
 *  GPIO42 │ BUZZER             │ OUT (PWM) │ Buzzer
 *
 * @version 1.0.0
 */

#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

/*==============================================================================
 *                       STATUS LEDs  (Active High — NPN driver)
 *============================================================================*/

/** @brief BLE connection status LED (Blue) */
#define HW_PIN_LED_BLE              (21U)

/** @brief Air quality alarm LED (Red) */
#define HW_PIN_LED_ALARM            (47U)

/** @brief System error LED (Red) */
#define HW_PIN_LED_ERROR            (45U)

/** @brief Power indicator LED (Green) */
#define HW_PIN_LED_POWER            (38U)

/** @brief CPU heartbeat LED (Green) */
#define HW_PIN_LED_CPU              (39U)

/** @brief Modbus activity LED (Yellow) */
#define HW_PIN_LED_MODBUS           (40U)

/*==============================================================================
 *                       AUDIO
 *============================================================================*/

/** @brief Buzzer PWM output (Active High) */
#define HW_PIN_BUZZER               (42U)

/*==============================================================================
 *                       FAN
 *============================================================================*/

/** @brief Fan speed PWM output */
#define HW_PIN_FAN_PWM              (6U)

/*==============================================================================
 *                       SENSOR UART  (ZPHS01C / ZPHS01B)
 *============================================================================*/

/** @brief Sensor UART TX — ESP32 transmits to sensor */
#define HW_PIN_SENSOR_UART_TX       (4U)

/** @brief Sensor UART RX — ESP32 receives from sensor */
#define HW_PIN_SENSOR_UART_RX       (5U)

/*==============================================================================
 *                       RS485 / MODBUS
 *============================================================================*/

/** @brief RS485 UART RX */
#define HW_PIN_RS485_UART_RX        (17U)

/** @brief RS485 UART TX */
#define HW_PIN_RS485_UART_TX        (18U)

/** @brief RS485 direction control (LOW=RX, HIGH=TX) */
#define HW_PIN_RS485_DIR            (14U)

/*==============================================================================
 *                       RTC (DS1307)
 *============================================================================*/

/** @brief RTC I2C SDA */
#define HW_PIN_RTC_SDA              (15U)

/** @brief RTC I2C SCL */
#define HW_PIN_RTC_SCL              (16U)

/** @brief RTC interrupt input (active low, needs pullup) */
#define HW_PIN_RTC_INT              (3U)

/*==============================================================================
 *                       POWER CONTROL
 *============================================================================*/

/** @brief Sensor power supply enable (HIGH=ON) */
#define HW_PIN_SENSOR_POWER         (48U)

/*==============================================================================
 *                       BLE
 *============================================================================*/

/** @brief Software BLE enable/disable control */
#define HW_PIN_SW_BLE               (8U)

/*==============================================================================
 *                       SAFE BOOT DEFAULTS
 *          Expected GPIO state immediately after reset.
 *          Outputs must match these to prevent hardware damage.
 *============================================================================*/

// #define HW_DEFAULT_LED_BLE          (LOW)   /* LED off                       */
// #define HW_DEFAULT_LED_ALARM        (LOW)   /* LED off                       */
// #define HW_DEFAULT_LED_ERROR        (LOW)   /* LED off                       */
// #define HW_DEFAULT_LED_POWER        (LOW)   /* LED off                       */
// #define HW_DEFAULT_LED_CPU          (LOW)   /* LED off                       */
// #define HW_DEFAULT_LED_MODBUS       (LOW)   /* LED off                       */
// #define HW_DEFAULT_BUZZER           (LOW)   /* Silent                        */
// #define HW_DEFAULT_FAN_PWM          (LOW)   /* Fan stopped                   */
// #define HW_DEFAULT_RS485_DIR        (LOW)   /* Receive mode                  */
// #define HW_DEFAULT_SENSOR_POWER     (LOW)   /* Sensor powered off            */
// #define HW_DEFAULT_SW_BLE           (LOW)   /* BLE disabled                  */

#endif /* HARDWARE_CONFIG_H */
