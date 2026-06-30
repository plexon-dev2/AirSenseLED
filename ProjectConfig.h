/**
 * @file ProjectConfig.h
 * @brief AirSense ESP32-S3 — Master Project Configuration
 *
 * SINGLE FILE FOR ALL TUNABLE PARAMETERS.
 * Hardware pin assignments remain in HardwareConfig.h (GPIO source of truth).
 * This file owns every value you might want to change between hardware revisions.
 *
 * HOW TO USE:
 *   - Each module _Cfg.h now #includes this file instead of defining values.
 *   - To retune: edit only this file. All modules pick up changes on next build.
 *   - Sections are ordered by module, matching your _Cfg.h files.
 *
 * @version 1.0.0 — consolidated from 10 individual _Cfg.h files
 */

#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "HardwareConfig.h"

/*==============================================================================
 *  SECTION 1 — WIFI & MQTT
 *  Origin: WiFiComm_Cfg.h
 *============================================================================*/

/* --- Credentials (change per deployment site) ---------------------------- */
#define CFG_WIFI_SSID                   "1st floor"
#define CFG_WIFI_PASSWORD               "Plexon5555"

/* --- Static IP (set CFG_WIFI_STATIC_IP_ENABLED 0 for DHCP) --------------- */
#define CFG_WIFI_STATIC_IP_ENABLED      (0U)
#define CFG_WIFI_STATIC_IP              "192.168.7.2"
#define CFG_WIFI_STATIC_GATEWAY         "192.168.7.1"
#define CFG_WIFI_STATIC_SUBNET          "255.255.255.0"
#define CFG_WIFI_STATIC_DNS1            "8.8.8.8"
#define CFG_WIFI_STATIC_DNS2            "8.8.4.4"

/* --- Mode flags ----------------------------------------------------------- */
#define CFG_WIFI_TCP_CLIENT_ENABLED     (0U)
#define CFG_WIFI_TCP_SERVER_ENABLED     (0U)
#define CFG_WIFI_UDP_ENABLED            (0U)
#define CFG_WIFI_MQTT_ENABLED           (1U)

/* --- TCP Client ----------------------------------------------------------- */
#define CFG_WIFI_TCP_CLIENT_SERVER_IP   "192.168.7.107"
#define CFG_WIFI_TCP_CLIENT_PORT        (8081U)
#define CFG_WIFI_TCP_CLIENT_RECONNECT_MS (5000U)
#define CFG_WIFI_TCP_CLIENT_TIMEOUT_MS  (3000U)

/* --- TCP Server ----------------------------------------------------------- */
#define CFG_WIFI_TCP_SERVER_PORT        (8080U)
#define CFG_WIFI_TCP_SERVER_MAX_CLIENTS (1U)

/* --- UDP ------------------------------------------------------------------ */
#define CFG_WIFI_UDP_LOCAL_PORT         (9000U)
#define CFG_WIFI_UDP_REMOTE_PORT        (9001U)
#define CFG_WIFI_UDP_REMOTE_IP          "192.168.7.102"

/* --- MQTT ----------------------------------------------------------------- */
#define CFG_WIFI_MQTT_BROKER_IP         "broker.hivemq.com"
#define CFG_WIFI_MQTT_BROKER_PORT       (1883U)
#define CFG_WIFI_MQTT_CLIENT_ID         "ESP32_AQI_001"
#define CFG_WIFI_MQTT_KEEPALIVE_SEC     (60U)
#define CFG_WIFI_MQTT_RECONNECT_MS      (5000U)
#define CFG_WIFI_MQTT_QOS               (1U)
#define CFG_WIFI_MQTT_RETAIN            (0U)

/* --- MQTT Topics ---------------------------------------------------------- */
#define CFG_MQTT_TOPIC_SENSOR_DATA      "aqi/sensor/data"
#define CFG_MQTT_TOPIC_COMMAND          "aqi/command"
#define CFG_MQTT_TOPIC_STATUS           "aqi/status"
#define CFG_MQTT_TOPIC_ALARM            "aqi/alarm"
#define CFG_MQTT_TOPIC_CONFIG           "aqi/config"
#define CFG_MQTT_TOPIC_CURRENT          "aqi/sensor/current"    /**< New: current sense data */
#define CFG_MQTT_TOPIC_BOOTTIME         "aqi/sensor/boottime"   /**< New: sensor boot time   */

/* --- Timing --------------------------------------------------------------- */
#define CFG_WIFI_CONNECT_TIMEOUT_MS     (20000U)
#define CFG_WIFI_RECONNECT_CHECK_MS     (3000U)
#define CFG_WIFI_HANDLER_INTERVAL_MS    (100U)
#define CFG_WIFI_TX_BUFFER_SIZE         (512U)
#define CFG_WIFI_RX_BUFFER_SIZE         (512U)
#define CFG_WIFI_MQTT_TOPIC_MAX_LEN     (128U)
#define CFG_WIFI_PAYLOAD_MAX_LEN        (512U)

/*==============================================================================
 *  SECTION 2 — BLE
 *  Origin: BLEComm_Cfg.h
 *============================================================================*/

#define CFG_BLE_DEVICE_NAME             "ESP32_AQI"
#define CFG_BLE_SERVICE_UUID            "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CFG_BLE_SENSOR_CHAR_UUID        "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CFG_BLE_IDENTIFY_CHAR_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define CFG_BLE_FAN_DUTY_CHAR_UUID      "6E400004-B5A3-F393-E0A9-E50E24DCCA9E"
#define CFG_BLE_FAN_FREQ_CHAR_UUID      "6E400005-B5A3-F393-E0A9-E50E24DCCA9E"
#define CFG_BLE_IDENTIFY_TOKEN          "IDENTIFY:PyScript_AQI"
#define CFG_BLE_PYSCRIPT_DEVICE_NAME    "PyScript_AQI"
#define CFG_BLE_PYSCRIPT_SERVICE_UUID   "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CFG_BLE_PYSCRIPT_CHAR_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

/* --- Scan ----------------------------------------------------------------- */
#define CFG_BLE_SCAN_DURATION_SEC       (5U)
#define CFG_BLE_MAX_DEVICES             (5U)
#define CFG_BLE_SCAN_INTERVAL           (100U)
#define CFG_BLE_SCAN_WINDOW             (99U)

/* --- Task ----------------------------------------------------------------- */
#define CFG_BLE_CONNECT_TASK_STACK      (16384U)
#define CFG_BLE_CONNECT_TASK_PRIORITY   (1U)
#define CFG_BLE_HANDLER_INTERVAL_MS     (100U)

/*==============================================================================
 *  SECTION 3 — FAN
 *  Origin: Fan_Cfg.h
 *============================================================================*/

/* --- LEDC ----------------------------------------------------------------- */
#define CFG_FAN_LEDC_CHANNEL            (0U)
#define CFG_FAN_LEDC_TIMER              (0U)
#define CFG_FAN_PWM_FREQ_HZ             (25000U)
#define CFG_FAN_PWM_RESOLUTION_BITS     (8U)
#define CFG_FAN_FREQ_MIN_HZ             (1000U)
#define CFG_FAN_FREQ_MAX_HZ             (25000U)

/* --- Kickstart ------------------------------------------------------------ */
/** @brief Full-duty burst duration when starting fan from rest (ms).
 *  Increased from 300ms to 1200ms -- exhaust fan motor was not reliably
 *  overcoming starting torque within the original burst window. */
#define CFG_FAN_KICKSTART_MS            (1200U)

/* --- Voltage curve -------------------------------------------------------- */
/** @brief Voltage below which fan is commanded OFF */
#define CFG_FAN_VOLT_OFF                (0.5f)
/** @brief Voltage at which PWM ramp starts (fan minimum running speed) */
#define CFG_FAN_VOLT_START              (2.0f)
/** @brief Voltage at which PWM reaches 100% */
#define CFG_FAN_VOLT_FULL               (5.0f)
#define CFG_FAN_SUPPLY_VOLTAGE_V        (5.0f)
#define CFG_FAN_DUTY_MAX                (255U)

/* --- Tachometer ----------------------------------------------------------- */
#define CFG_FAN_TACH_ENABLE             (0U)      /**< 0=disabled (2-wire), 1=enabled (3-wire) */
#define CFG_FAN_FAIL_RPM_THRESHOLD      (200U)
#define CFG_FAN_TACH_PULSES_PER_REV     (2U)
#define CFG_FAN_TACH_MEASURE_MS         (1000U)
#define CFG_FAN_STALL_DETECT_MS         (3000U)

/*==============================================================================
 *  SECTION 4 — BUZZER
 *  Origin: Buzzer_Cfg.h
 *============================================================================*/

#define CFG_BUZZER_LEDC_CHANNEL         (1U)
#define CFG_BUZZER_LEDC_TIMER           (1U)
/** @brief Set to buzzer's mechanical resonant frequency for loudest output */
#define CFG_BUZZER_FREQ_HZ              (3500U)
#define CFG_BUZZER_RESOLUTION           (8U)
#define CFG_BUZZER_DEFAULT_VOLUME       (50U)     /**< 0-100 */
#define CFG_BUZZER_VOLUME_MIN           (0U)
#define CFG_BUZZER_VOLUME_MAX           (100U)

/* --- Beep timing (ms) ----------------------------------------------------- */
#define CFG_BUZZER_BEEP_MS              (100U)
#define CFG_BUZZER_BEEP_GAP_MS          (100U)
#define CFG_BUZZER_BEEP_LONG_MS         (500U)

/* --- Tone presets --------------------------------------------------------- */
#define CFG_BUZZER_ALARM_FREQ_HZ        (3500U)
#define CFG_BUZZER_NOTIFY_FREQ_HZ       (2000U)
#define CFG_BUZZER_ERROR_FREQ_HZ        (1000U)

/*==============================================================================
 *  SECTION 5 — APP / APPLICATION LOGIC
 *  Origin: App_Cfg.h
 *============================================================================*/

/* --- BLE switch ----------------------------------------------------------- */
#define CFG_APP_BLE_HOLD_MS             (5000U)   /**< Hold duration to re-enable BLE */
#define CFG_APP_BLE_AUTO_OFF_MS         (120000UL) /**< 2-minute advertising timeout  */

/* --- Sensor power --------------------------------------------------------- */
/** @brief Fixed warm-up wait after sensor power ON (ms).
 *  Actual measured bootup time is logged via CurrentSense module. */
#define CFG_APP_SENSOR_WARMUP_MS        (3000U)
#define CFG_APP_SENSOR_POWER_ON_LEVEL   (HIGH)

/* --- Fan auto-control thresholds ----------------------------------------- */
/** @brief Fan never fully stops in auto mode -- always-on floor voltage.
 *  Raised from 2.0V to 3.0V -- 2.0V settled duty (FAN_DUTY_MIN, ~40%) was
 *  not enough to keep this exhaust fan motor reliably spinning after the
 *  kickstart burst ended. Tune further if fan still stalls at floor speed. */
#define CFG_APP_FAN_MIN_VOLTAGE         (3.0f)

/* Voltage steps by CO2 (ppm) */
#define CFG_APP_FAN_CO2_THRESH_HIGH     (1500.0f)  /**< 5.0V */
#define CFG_APP_FAN_CO2_THRESH_MED      (1000.0f)  /**< 3.5V */
#define CFG_APP_FAN_CO2_THRESH_LOW      (800.0f)   /**< 2.0V */

/* Voltage steps by PM2.5 (ug/m3) */
#define CFG_APP_FAN_PM25_THRESH_HIGH    (75.0f)    /**< 5.0V */
#define CFG_APP_FAN_PM25_THRESH_MED     (35.0f)    /**< 3.0V */

/* --- Display / comms ------------------------------------------------------ */
#define CFG_APP_DISPLAY_UPDATE_INTERVAL (10U)      /**< Handler calls between updates */
#define CFG_APP_ISO_POWER_DEBOUNCE      (10U)
#define CFG_APP_HEAP_LOG_INTERVAL       (100U)

/* --- Feature flags -------------------------------------------------------- */
#define CFG_APP_DEBUG_ENABLE            (1U)
#define CFG_APP_COMM_MONITORING_ENABLE  (1U)
#define CFG_APP_HEAP_MONITORING_ENABLE  (1U)

/*==============================================================================
 *  SECTION 6 — LED HMI
 *  Origin: AQ_LEDHMI_Cfg.h
 *============================================================================*/

#define CFG_LED_ACTIVE_LOW              (0U)  /**< NPN driver: GPIO HIGH = LED ON */

/* --- Blink timing (ms) ---------------------------------------------------- */
#define CFG_LED_CPU_ON_MS               (500U)
#define CFG_LED_CPU_OFF_MS              (500U)
#define CFG_LED_ERROR_BLINK_ON_MS       (500U)
#define CFG_LED_ERROR_BLINK_OFF_MS      (500U)
#define CFG_LED_ALARM_BLINK_ON_MS       (500U)
#define CFG_LED_ALARM_BLINK_OFF_MS      (500U)
#define CFG_LED_MODBUS_BLINK_ON_MS      (500U)
#define CFG_LED_MODBUS_BLINK_OFF_MS     (500U)
#define CFG_LED_BLE_BLINK_ON_MS         (500U)
#define CFG_LED_BLE_BLINK_OFF_MS        (500U)

/*==============================================================================
 *  SECTION 7 — MODBUS
 *  Origin: Modbus_cfg.h
 *============================================================================*/

#define CFG_MODBUS_UART_PORT            (2U)
#define CFG_MODBUS_BAUD_RATE            (9600U)
#define CFG_MODBUS_BUFFER_SIZE          (300U)
#define CFG_MODBUS_FRAME_DELAY_US       (4000U)
#define CFG_MODBUS_INTER_FRAME_DELAY_MS (4U)
#define CFG_MODBUS_RS485_SWITCH_DELAY_US (100U)
#define CFG_MODBUS_SLAVE_ID             (1U)      /**< Valid range: 1-247 */
#define CFG_MODBUS_HOLDING_REG_COUNT    (20U)
#define CFG_MODBUS_STATS_ENABLE         (1U)

/*==============================================================================
 *  SECTION 8 — RTC (PCF8563T)
 *  Origin: RTC_Cfg.h
 *============================================================================*/

#define CFG_RTC_I2C_FREQ_HZ             (100000UL)
#define CFG_RTC_I2C_ADDR                (0x51U)   /**< PCF8563T fixed address */
#define CFG_RTC_YEAR_OFFSET             (2000U)
#define CFG_RTC_YEAR_MIN                (2000U)
#define CFG_RTC_YEAR_MAX                (2099U)
#define CFG_RTC_UNIX_EPOCH_2000         (946684800UL)
#define CFG_RTC_AUTO_START_OSCILLATOR   (1U)
#define CFG_RTC_FORCE_24HR_MODE         (1U)

/*==============================================================================
 *  SECTION 9 — SCHEDULER
 *  Origin: Scheduler_Cfg.h
 *============================================================================*/

#define CFG_SCHED_STACK_CORE0           (16384U)
#define CFG_SCHED_STACK_WIFI            (16384U)
#define CFG_SCHED_STACK_CORE1           (16384U)
#define CFG_SCHED_PRIORITY_HIGH         (2U)
#define CFG_SCHED_PRIORITY_WIFI         (1U)
#define CFG_SCHED_PERIOD_CORE0_MS       (50U)
#define CFG_SCHED_PERIOD_WIFI_MS        (100U)
#define CFG_SCHED_PERIOD_CORE1_MS       (50U)

/*==============================================================================
 *  SECTION 10 — SYSTEM STATES
 *  Origin: SystemStates_Cfg.h
 *============================================================================*/

#define CFG_SYS_COMM_TIMEOUT_MS         (5000U)
#define CFG_SYS_HEALTH_CHECK_INTERVAL   (100U)
#define CFG_SYS_FAILURE_RECOVERY_MS     (2000U)
#define CFG_SYS_MAX_CONSECUTIVE_ERRORS  (5U)
#define CFG_SYS_MAX_ERROR_COUNT         (50U)
#define CFG_SYS_AUTO_RECOVERY_ENABLE    (1U)
#define CFG_SYS_STATISTICS_ENABLE       (1U)
#define CFG_SYS_INIT_DELAY_MS           (1000U)
#define CFG_SYS_MAX_RECOVERY_ATTEMPTS   (10U)

/*==============================================================================
 *  SECTION 11 — CURRENT SENSING (NEW)
 *  GPIO1 = FAN_I  (0.1Ω shunt, low-side)
 *  GPIO2 = SENSOR_I (0.2Ω shunt, low-side)
 *============================================================================*/

/** @brief ADC channel for fan current sense (GPIO1 = ADC1_CH0) */
#define CFG_CURRENT_FAN_ADC_CHANNEL     ADC1_CHANNEL_0   /**< GPIO1 */

/** @brief ADC channel for sensor current sense (GPIO2 = ADC1_CH1) */
#define CFG_CURRENT_SENSOR_ADC_CHANNEL  ADC1_CHANNEL_1   /**< GPIO2 */

/** @brief Shunt resistor value — Fan (Ohms) */
#define CFG_CURRENT_FAN_SHUNT_OHM       (0.1f)

/** @brief Shunt resistor value — Sensor board (Ohms) */
#define CFG_CURRENT_SENSOR_SHUNT_OHM    (0.2f)

/** @brief ADC resolution bits (ESP32-S3 = 12) */
#define CFG_CURRENT_ADC_BITS            (12U)

/** @brief ADC reference voltage in mV (ESP32-S3 attenuation 0dB = 950mV full-scale) */
#define CFG_CURRENT_ADC_VREF_MV         (950U)     /**< Use 0dB atten — max ~950mV */

/** @brief ADC attenuation — keep at 0dB since max shunt voltage is ~100mV */
#define CFG_CURRENT_ADC_ATTEN           ADC_ATTEN_DB_0

/** @brief Number of ADC samples to average per reading (oversampling for noise) */
#define CFG_CURRENT_OVERSAMPLE_COUNT    (64U)

/** @brief Rolling average window depth */
#define CFG_CURRENT_ROLLING_AVG_DEPTH   (8U)

/** @brief How often current is sampled — handler calls (at 50ms task = every N*50ms) */
#define CFG_CURRENT_SAMPLE_INTERVAL     (4U)       /**< Every 200ms */

/** @brief MQTT publish interval for current data (handler calls) */
#define CFG_CURRENT_MQTT_INTERVAL       (20U)      /**< Every 1 second */

/** @brief Current alarm threshold — Sensor over-current (mA) */
#define CFG_CURRENT_SENSOR_ALARM_MA     (600U)     /**< >600mA = fault */

/** @brief Current alarm threshold — Fan over-current (mA) */
#define CFG_CURRENT_FAN_ALARM_MA        (1500U)    /**< >1500mA = fault */

/*==============================================================================
 *  SECTION 12 — SENSOR BOOTUP TIME (NEW)
 *============================================================================*/

/** @brief Maximum time to wait for first valid sensor response after power ON (ms) */
#define CFG_BOOT_SENSOR_TIMEOUT_MS      (10000U)

/** @brief Modbus poll interval during bootup wait (ms) */
#define CFG_BOOT_POLL_INTERVAL_MS       (200U)

/** @brief Report bootup time via MQTT: 1=yes, 0=no */
#define CFG_BOOT_MQTT_REPORT_ENABLE     (1U)

/** @brief Report bootup time via BLE JSON: 1=yes, 0=no */
#define CFG_BOOT_BLE_REPORT_ENABLE      (1U)

/*==============================================================================
 *  GLOBAL DEBUG SWITCH
 *  Set 0 to silence all module debug output in one place
 *============================================================================*/
#define CFG_DEBUG_ENABLE                (1U)

#if (CFG_DEBUG_ENABLE == 1U)
    #define CFG_DBG_PRINTF(...)         Serial.printf(__VA_ARGS__)
    #define CFG_DBG_PRINTLN(x)          Serial.println(x)
#else
    #define CFG_DBG_PRINTF(...)
    #define CFG_DBG_PRINTLN(x)
#endif

/*==============================================================================
 *  COMPILE-TIME VALIDATION
 *============================================================================*/
#ifdef __cplusplus
    #define _CFG_ASSERT(expr, msg)  static_assert(expr, msg)
#else
    #define _CFG_ASSERT(expr, msg)  _Static_assert(expr, msg)
#endif

_CFG_ASSERT(CFG_MODBUS_SLAVE_ID >= 1U && CFG_MODBUS_SLAVE_ID <= 247U,
            "CFG_MODBUS_SLAVE_ID must be 1-247");
_CFG_ASSERT(CFG_MODBUS_HOLDING_REG_COUNT >= 1U && CFG_MODBUS_HOLDING_REG_COUNT <= 125U,
            "CFG_MODBUS_HOLDING_REG_COUNT must be 1-125");
_CFG_ASSERT(CFG_BLE_SCAN_WINDOW <= CFG_BLE_SCAN_INTERVAL,
            "CFG_BLE_SCAN_WINDOW must be <= CFG_BLE_SCAN_INTERVAL");
_CFG_ASSERT(CFG_BLE_MAX_DEVICES >= 1U && CFG_BLE_MAX_DEVICES <= 10U,
            "CFG_BLE_MAX_DEVICES must be 1-10");
_CFG_ASSERT(CFG_BLE_CONNECT_TASK_STACK >= 6144U,
            "CFG_BLE_CONNECT_TASK_STACK must be >= 6144 bytes");
_CFG_ASSERT(CFG_BUZZER_DEFAULT_VOLUME <= 100U,
            "CFG_BUZZER_DEFAULT_VOLUME must be 0-100");
_CFG_ASSERT(CFG_WIFI_MQTT_QOS <= 2U,
            "CFG_WIFI_MQTT_QOS must be 0, 1, or 2");
_CFG_ASSERT(CFG_CURRENT_OVERSAMPLE_COUNT >= 8U && CFG_CURRENT_OVERSAMPLE_COUNT <= 256U,
            "CFG_CURRENT_OVERSAMPLE_COUNT must be 8-256");

#endif /* PROJECT_CONFIG_H */