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
 * @version 1.5.0
 *
 * v1.5.0 changes (review fixes):
 *   - AQ_LED_*_Config structs removed from header (were static const -- each
 *     including TU got its own copy, MISRA Rule 2.4 / ODR risk).
 *     Definitions now live in AQ_LEDHMI.cpp; only extern declarations here.
 */

#ifndef AQ_LEDHMI_CFG_H
#define AQ_LEDHMI_CFG_H

#include "AQ_LEDHMI.h"
#include "HardwareConfig.h"

/*==============================================================================
 *                          PIN DEFINITIONS  (from HardwareConfig.h)
 *============================================================================*/
#define AQ_LED_BLE_PIN      HW_PIN_LED_BLE      /**< GPIO21 -- Blue LED   */
#define AQ_LED_ALARM_PIN    HW_PIN_LED_ALARM     /**< GPIO47 -- Red LED    */
#define AQ_LED_ERROR_PIN    HW_PIN_LED_ERROR     /**< GPIO45 -- Red LED    */
#define AQ_LED_POWER_PIN    HW_PIN_LED_POWER     /**< GPIO38 -- Green LED  */
#define AQ_LED_CPU_PIN      HW_PIN_LED_CPU       /**< GPIO39 -- Green LED  */
#define AQ_LED_MODBUS_PIN   HW_PIN_LED_MODBUS    /**< GPIO40 -- Yellow LED */

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
 *  Defined in AQ_LEDHMI.cpp -- declared extern here so other TUs do not
 *  get duplicate copies (was static const in header -- ODR / MISRA Rule 2.4).
 *============================================================================*/
extern const AQ_LED_Config AQ_LED_Power_Config;
extern const AQ_LED_Config AQ_LED_Cpu_Config;
extern const AQ_LED_Config AQ_LED_Error_Config;
extern const AQ_LED_Config AQ_LED_Alarm_Config;
extern const AQ_LED_Config AQ_LED_Modbus_Config;
extern const AQ_LED_Config AQ_LED_Ble_Config;

/*==============================================================================
 *                          VALIDATION
 *============================================================================*/
#if (AQ_LED_CPU_ON_MS < 100U) || (AQ_LED_CPU_OFF_MS < 100U)
#error "CPU LED timing must be at least 100ms"
#endif

#endif /* AQ_LEDHMI_CFG_H */