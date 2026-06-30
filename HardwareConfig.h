/**
 * @file HardwareConfig.h
 * @brief Master Hardware GPIO Configuration -- AirSense ESP32-S3 LED Build
 *
 * @details SINGLE SOURCE OF TRUTH for all GPIO pin assignments and safe-state
 *          default levels.  All module config files (RTC_Cfg.h, AQ_LEDHMI_Cfg.h,
 *          App_Cfg.h, Modbus_Cfg.h, Fan_Cfg.h, Buzzer_Cfg.h, CmdParser_cfg.h,
 *          etc.) must include this file and reference these macros.
 *          NEVER hardcode pin numbers in module files.
 *
 *          To change a pin: edit ONLY this file.
 *          All modules pick up the change automatically on next build.
 *
 * VERIFIED GPIO MAP (ESP32-S3-WROOM-1-N8R8, schematic rev 1.0):
 * ==============================================================
 *
 *  GPIO  | Signal Name        | Direction  | Module
 * -------+--------------------+------------+--------------------
 *  IO3   | RTC_INT            | IN (PU)    | RTC          (*strapping pin)
 *  IO4   | SENSOR_UART_TX     | OUT (UART) | CmdParser
 *  IO5   | SENSOR_UART_RX     | IN  (UART) | CmdParser
 *  IO6   | FAN_PWM            | OUT (PWM)  | Fan
 *  IO8   | SW_BLE_ON          | IN  (PU)   | BLESwitch    (physical button, active LOW)
 *  IO14  | RS485_DIRN         | OUT        | Modbus
 *  IO15  | RTC_SDA            | I2C        | RTC
 *  IO16  | RTC_SCL            | I2C        | RTC
 *  IO17  | RS485_UART_RX      | IN  (UART) | Modbus
 *  IO18  | RS485_UART_TX      | OUT (UART) | Modbus
 *  IO19  | DMS                | IN         | App
 *  IO20  | DPS                | IN         | App
 *  IO21  | LED_BLE            | OUT        | AQ_LEDHMI
 *  IO38  | ISO_V_STATUS       | IN         | App          (RS485 PSU monitor)
 *  IO39  | LED_CPU            | OUT        | AQ_LEDHMI
 *  IO40  | LED_MODBUS         | OUT        | AQ_LEDHMI
 *  IO42  | BUZZER             | OUT (PWM)  | Buzzer
 *  IO45  | LED_ERROR          | OUT        | AQ_LEDHMI
 *  IO47  | LED_ALARM          | OUT        | AQ_LEDHMI
 *  IO48  | SENSOR_POWER       | OUT        | App / SensorBoot
 *  TBD   | LED_POWER          | OUT        | AQ_LEDHMI    (*UNRESOLVED -- see below)
 *  TBD   | FAN_TACH           | IN  (PU)   | Fan          (*reserved for 3-wire fan)
 *
 * UNRESOLVED HARDWARE CONFLICTS:
 * ================================
 *  1. LED_POWER vs ISO_V_STATUS:
 *     Previous revision assigned both to IO38. This is impossible -- one pin
 *     cannot simultaneously drive a LED output and monitor an RS485 PSU fault
 *     input. ISO_V_STATUS is assigned IO38 (verified from schematic).
 *     LED_POWER needs a different GPIO -- update HW_PIN_LED_POWER below and
 *     the schematic together.  Placeholder: IO41 (verify no conflict first).
 *
 *  2. SW_BLE_ON direction:
 *     Pin table previously labelled IO8 as "OUT" -- corrected to "IN (PU)".
 *     HardwareInit.h was incorrectly configuring it as OUTPUT -- fixed.
 *
 * @version 1.2.0
 *
 * v1.2.0 changes (review fixes):
 *   - HW_DEFAULT_* safe-state macros added -- were referenced in HardwareInit.h
 *     but never defined anywhere, causing compile failure.
 *   - HW_PIN_SENSOR_UARTRX / HW_PIN_SENSOR_UARTTX added (used by CmdParser_cfg.h).
 *   - HW_PIN_FAN_TACH added as reserved placeholder (used by Fan_Cfg.h when tach enabled).
 *   - HW_PIN_LED_POWER: conflict with IO38 documented and reassigned to placeholder
 *     IO41 -- verify against schematic before use.
 *   - SW_BLE_ON direction corrected from OUT to IN in pin table comment.
 *   - Static assert added: HW_PIN_LED_POWER != HW_PIN_ISO_V_STATUS.
 *   - Static assert expanded: additional conflict pairs added.
 *   - Non-ASCII characters in comments replaced with ASCII equivalents.
 */

#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

/*==============================================================================
 *                       STATUS LEDs  (Active High -- NPN driver)
 *============================================================================*/

/** @brief BLE connection status LED -- IO21 */
#define HW_PIN_LED_BLE              (21U)

/** @brief Air quality alarm LED -- IO47 */
#define HW_PIN_LED_ALARM            (47U)

/** @brief System error LED -- IO45 */
#define HW_PIN_LED_ERROR            (45U)

/**
 * @brief Power indicator LED.
 * @warning CONFLICT RESOLUTION REQUIRED: was IO38, which conflicts with
 *          HW_PIN_ISO_V_STATUS (also IO38). Reassigned to IO41 as placeholder.
 *          Verify against schematic rev and update before manufacture.
 */
#define HW_PIN_LED_POWER            (41U)   /* TODO: verify against schematic */

/** @brief CPU heartbeat LED -- IO39 */
#define HW_PIN_LED_CPU              (39U)

/** @brief Modbus activity LED -- IO40 */
#define HW_PIN_LED_MODBUS           (40U)

/*==============================================================================
 *                       AUDIO
 *============================================================================*/

/** @brief Buzzer PWM output (Active High) -- IO42 */
#define HW_PIN_BUZZER               (42U)

/*==============================================================================
 *                       FAN
 *============================================================================*/

/** @brief Fan speed PWM output -- IO6 */
#define HW_PIN_FAN_PWM              (6U)

/**
 * @brief Fan tachometer pulse input -- reserved for future 3-wire fan.
 * @details Only used when FAN_TACH_ENABLED=1 in Fan_Cfg.h.
 *          Must be an interrupt-capable GPIO that does not conflict with ADC
 *          channels used by CurrentSense (GPIO1, GPIO2).
 * @warning Assign a real GPIO number here before enabling FAN_TACH_ENABLED.
 *          Placeholder value 7 -- verify no conflict with schematic.
 */
#define HW_PIN_FAN_TACH             (7U)    /* TODO: verify against schematic */

/*==============================================================================
 *                       SENSOR UART  (ZPHS01B / ZPHS01C)
 *============================================================================*/

/** @brief Sensor UART TX (ESP32 TX -> sensor RX) -- IO4 */
#define HW_PIN_SENSOR_UARTTX        (4U)

/** @brief Sensor UART RX (sensor TX -> ESP32 RX) -- IO5 */
#define HW_PIN_SENSOR_UARTRX        (5U)

/*==============================================================================
 *                       RS485 / MODBUS
 *============================================================================*/

/** @brief RS485 UART RX -- IO17 */
#define HW_PIN_RS485_UART_RX        (17U)

/** @brief RS485 UART TX -- IO18 */
#define HW_PIN_RS485_UART_TX        (18U)

/** @brief RS485 direction control (LOW=RX, HIGH=TX) -- IO14 */
#define HW_PIN_RS485_DIR            (14U)

/*==============================================================================
 *                       RTC (PCF8563T)
 *============================================================================*/

/** @brief RTC I2C SDA -- IO15 */
#define HW_PIN_RTC_SDA              (15U)

/** @brief RTC I2C SCL -- IO16 */
#define HW_PIN_RTC_SCL              (16U)

/**
 * @brief RTC interrupt input (active low, open-drain, needs pullup) -- IO3
 * @warning GPIO3 is an ESP32-S3 strapping pin. It is sampled at reset.
 *          Ensure the RTC INT line is HIGH (inactive) during power-on/reset,
 *          otherwise the chip may boot into download mode.
 *          The 10k pull-up resistor on the schematic handles this correctly.
 */
#define HW_PIN_RTC_INT              (3U)

/*==============================================================================
 *                       POWER CONTROL
 *============================================================================*/

/** @brief Sensor power supply enable (HIGH=ON) -- IO48 */
#define HW_PIN_SENSOR_POWER         (48U)

/*==============================================================================
 *                       ISO POWER STATUS
 *============================================================================*/

/**
 * @brief RS485 isolated power supply status monitor -- IO38.
 * @details HIGH = power supply fault, LOW = normal.
 *          This is an INPUT -- cannot share with HW_PIN_LED_POWER (output).
 *          Conflict with previous LED_POWER assignment to IO38 is now resolved
 *          by moving LED_POWER to IO41 (see above).
 */
#define HW_PIN_ISO_V_STATUS         (38U)

/*==============================================================================
 *                       BLE SWITCH
 *============================================================================*/

/**
 * @brief Physical BLE enable/disable push-button switch -- IO8.
 * @details INPUT, active LOW (INPUT_PULLUP in BLESwitch module).
 *          Do NOT configure as OUTPUT -- doing so fights the external switch
 *          and risks GPIO damage when the switch is pressed.
 */
#define HW_PIN_SW_BLE               (8U)

/*==============================================================================
 *                       DMS / DPS
 *============================================================================*/

/** @brief DMS input -- IO19 */
#define HW_PIN_DMS                  (19U)

/** @brief DPS input -- IO20 */
#define HW_PIN_DPS                  (20U)

/*==============================================================================
 *                       SAFE-STATE DEFAULT LEVELS
 *  Used by HardwareInit__Init() to drive all outputs to a known safe state
 *  before any module initialises.  All LEDs and buzzer are active-high
 *  (NPN driver); fan and RS485 DIR are active-high too.
 *============================================================================*/

/** @brief LEDs: OFF at startup (active high -- LOW = off) */
#define HW_DEFAULT_LED_BLE          (LOW)
#define HW_DEFAULT_LED_ALARM        (LOW)
#define HW_DEFAULT_LED_ERROR        (LOW)
#define HW_DEFAULT_LED_POWER        (LOW)
#define HW_DEFAULT_LED_CPU          (LOW)
#define HW_DEFAULT_LED_MODBUS       (LOW)

/** @brief Buzzer: silent at startup */
#define HW_DEFAULT_BUZZER           (LOW)

/** @brief Fan: stopped at startup */
#define HW_DEFAULT_FAN_PWM          (LOW)

/** @brief RS485 direction: RX mode at startup (LOW=RX for most transceivers) */
#define HW_DEFAULT_RS485_DIR        (LOW)

/** @brief Sensor power: OFF at startup (HIGH=ON, so LOW=OFF) */
#define HW_DEFAULT_SENSOR_POWER     (LOW)

/*==============================================================================
 *                       COMPILE-TIME CONFLICT DETECTION
 *  Catches common pin assignment errors at build time.
 *  Add a new _HW_PIN_ASSERT for every new signal pair that could conflict.
 *============================================================================*/

#ifdef __cplusplus
  #define _HW_PIN_ASSERT(expr, msg) static_assert(expr, msg)
#else
  #define _HW_PIN_ASSERT(expr, msg) _Static_assert(expr, msg)
#endif

/* Previously undetected conflict -- now fixed and asserted */
_HW_PIN_ASSERT(HW_PIN_LED_POWER    != HW_PIN_ISO_V_STATUS,  "PIN CONFLICT: LED_POWER == ISO_V_STATUS");

/* RTC */
_HW_PIN_ASSERT(HW_PIN_RTC_INT      != HW_PIN_ISO_V_STATUS,  "PIN CONFLICT: RTC_INT == ISO_V_STATUS");
_HW_PIN_ASSERT(HW_PIN_RTC_SDA      != HW_PIN_RTC_SCL,       "PIN CONFLICT: RTC_SDA == RTC_SCL");
_HW_PIN_ASSERT(HW_PIN_RTC_INT      != HW_PIN_RTC_SDA,       "PIN CONFLICT: RTC_INT == RTC_SDA");

/* LEDs vs peripherals */
_HW_PIN_ASSERT(HW_PIN_LED_BLE      != HW_PIN_RS485_DIR,     "PIN CONFLICT: LED_BLE == RS485_DIR");
_HW_PIN_ASSERT(HW_PIN_LED_MODBUS   != HW_PIN_RS485_DIR,     "PIN CONFLICT: LED_MODBUS == RS485_DIR");
_HW_PIN_ASSERT(HW_PIN_LED_CPU      != HW_PIN_LED_MODBUS,    "PIN CONFLICT: LED_CPU == LED_MODBUS");
_HW_PIN_ASSERT(HW_PIN_LED_ALARM    != HW_PIN_LED_ERROR,     "PIN CONFLICT: LED_ALARM == LED_ERROR");

/* Buzzer */
_HW_PIN_ASSERT(HW_PIN_BUZZER       != HW_PIN_RTC_INT,       "PIN CONFLICT: BUZZER == RTC_INT");
_HW_PIN_ASSERT(HW_PIN_BUZZER       != HW_PIN_FAN_PWM,       "PIN CONFLICT: BUZZER == FAN_PWM");

/* Fan tach vs ADC (CurrentSense uses GPIO1/GPIO2) */
_HW_PIN_ASSERT(HW_PIN_FAN_TACH     != (1U),                 "PIN CONFLICT: FAN_TACH == GPIO1 (CurrentSense fan ADC)");
_HW_PIN_ASSERT(HW_PIN_FAN_TACH     != (2U),                 "PIN CONFLICT: FAN_TACH == GPIO2 (CurrentSense sensor ADC)");
_HW_PIN_ASSERT(HW_PIN_FAN_TACH     != HW_PIN_SW_BLE,        "PIN CONFLICT: FAN_TACH == SW_BLE");
_HW_PIN_ASSERT(HW_PIN_FAN_TACH     != HW_PIN_FAN_PWM,       "PIN CONFLICT: FAN_TACH == FAN_PWM");

/* UART channels */
_HW_PIN_ASSERT(HW_PIN_SENSOR_UARTRX != HW_PIN_RS485_UART_RX, "PIN CONFLICT: SENSOR_RX == RS485_RX");
_HW_PIN_ASSERT(HW_PIN_SENSOR_UARTTX != HW_PIN_RS485_UART_TX, "PIN CONFLICT: SENSOR_TX == RS485_TX");

/* BLE switch vs active outputs */
_HW_PIN_ASSERT(HW_PIN_SW_BLE       != HW_PIN_FAN_PWM,       "PIN CONFLICT: SW_BLE == FAN_PWM");
_HW_PIN_ASSERT(HW_PIN_SW_BLE       != HW_PIN_LED_BLE,       "PIN CONFLICT: SW_BLE == LED_BLE");

#endif /* HARDWARE_CONFIG_H */