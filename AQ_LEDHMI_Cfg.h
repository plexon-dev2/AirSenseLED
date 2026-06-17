/**
 * @file AQ_LEDHMI_Cfg.h
 * @brief Air Quality LED HMI Configuration -- LED Project
 *
 * All pin assignments sourced from HardwareConfig.h.
 * Do NOT hardcode GPIO numbers here.
 *
 * Verified GPIO values (from HardwareConfig.h v1.0.0):
 *   LED_BLE    -> HW_PIN_LED_BLE    = GPIO21
 *   LED_ALARM  -> HW_PIN_LED_ALARM  = GPIO47
 *   LED_ERROR  -> HW_PIN_LED_ERROR  = GPIO45
 *   LED_POWER  -> HW_PIN_LED_POWER  = GPIO38
 *   LED_CPU    -> HW_PIN_LED_CPU    = GPIO39
 *   LED_MODBUS -> HW_PIN_LED_MODBUS = GPIO40
 *
 * @version 1.4.0 -- all pins now reference HW_PIN_* macros
 */

#ifndef AQ_LEDHMI_CFG_H
#define AQ_LEDHMI_CFG_H

#include "AQ_LEDHMI.h"
#include "HardwareConfig.h"

/*==============================================================================
 *                          PIN DEFINITIONS  (from HardwareConfig.h)
 *============================================================================*/
#define AQ_LED_BLE_PIN      HW_PIN_LED_BLE      /**< GPIO21  -- Blue LED    */
#define AQ_LED_ALARM_PIN    HW_PIN_LED_ALARM     /**< GPIO47 -- Red LED     */
#define AQ_LED_ERROR_PIN    HW_PIN_LED_ERROR     /**< GPIO45 -- Red LED     */
#define AQ_LED_POWER_PIN    HW_PIN_LED_POWER     /**< GPIO38 -- Green LED   */
#define AQ_LED_CPU_PIN      HW_PIN_LED_CPU       /**< GPIO39 -- Green LED   */
#define AQ_LED_MODBUS_PIN   HW_PIN_LED_MODBUS    /**< GPIO40 -- Yellow LED  */

/*==============================================================================
 *                          ACTIVE LEVEL
 *  All LEDs are NPN-driven: GPIO HIGH = LED ON (Active High)
 *============================================================================*/
#define AQ_LED_ACTIVE_LOW   (0U)

/*==============================================================================
 *                          TIMING (ms)
 *============================================================================*/
#define AQ_LED_CPU_ON_MS            (500U)
#define AQ_LED_CPU_OFF_MS           (500U)
#define AQ_LED_ERROR_BLINK_ON_MS    (500U)
#define AQ_LED_ERROR_BLINK_OFF_MS   (500U)
#define AQ_LED_ALARM_BLINK_ON_MS    (500U)
#define AQ_LED_ALARM_BLINK_OFF_MS   (500U)
#define AQ_LED_MODBUS_BLINK_ON_MS   (500U)
#define AQ_LED_MODBUS_BLINK_OFF_MS  (500U)
#define AQ_LED_BLE_BLINK_ON_MS      (500U)
#define AQ_LED_BLE_BLINK_OFF_MS     (500U)

/*==============================================================================
 *                          LED CONFIGURATION STRUCTURES
 *============================================================================*/
static const AQ_LED_Config AQ_LED_Power_Config =
{
    .pin          = AQ_LED_POWER_PIN,
    .blink_on_ms  = 0U,
    .blink_off_ms = 0U,
    .active_low   = AQ_LED_ACTIVE_LOW
};

static const AQ_LED_Config AQ_LED_Cpu_Config =
{
    .pin          = AQ_LED_CPU_PIN,
    .blink_on_ms  = AQ_LED_CPU_ON_MS,
    .blink_off_ms = AQ_LED_CPU_OFF_MS,
    .active_low   = AQ_LED_ACTIVE_LOW
};

static const AQ_LED_Config AQ_LED_Error_Config =
{
    .pin          = AQ_LED_ERROR_PIN,
    .blink_on_ms  = AQ_LED_ERROR_BLINK_ON_MS,
    .blink_off_ms = AQ_LED_ERROR_BLINK_OFF_MS,
    .active_low   = AQ_LED_ACTIVE_LOW
};

static const AQ_LED_Config AQ_LED_Alarm_Config =
{
    .pin          = AQ_LED_ALARM_PIN,
    .blink_on_ms  = AQ_LED_ALARM_BLINK_ON_MS,
    .blink_off_ms = AQ_LED_ALARM_BLINK_OFF_MS,
    .active_low   = AQ_LED_ACTIVE_LOW
};

static const AQ_LED_Config AQ_LED_Modbus_Config =
{
    .pin          = AQ_LED_MODBUS_PIN,
    .blink_on_ms  = AQ_LED_MODBUS_BLINK_ON_MS,
    .blink_off_ms = AQ_LED_MODBUS_BLINK_OFF_MS,
    .active_low   = AQ_LED_ACTIVE_LOW
};

static const AQ_LED_Config AQ_LED_Ble_Config =
{
    .pin          = AQ_LED_BLE_PIN,
    .blink_on_ms  = AQ_LED_BLE_BLINK_ON_MS,
    .blink_off_ms = AQ_LED_BLE_BLINK_OFF_MS,
    .active_low   = AQ_LED_ACTIVE_LOW
};

/*==============================================================================
 *                          VALIDATION
 *  ESP32-S3 GPIO0 is a strapping pin. Ensure no external pull-down on PCB
 *  that could hold it LOW at boot (LED off = safe, NPN driver starts LOW).
 *============================================================================*/
#if (AQ_LED_CPU_ON_MS < 100U) || (AQ_LED_CPU_OFF_MS < 100U)
#error "CPU LED timing must be at least 100ms"
#endif

#endif /* AQ_LEDHMI_CFG_H */