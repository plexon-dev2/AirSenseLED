/**
 * @file Modbus_Cfg.h
 * @brief Modbus RTU Slave Configuration -- LED Project
 *
 * Pins sourced from HardwareConfig.h.
 *   RS485_RX  -> HW_PIN_RS485_UART_RX = GPIO17
 *   RS485_TX  -> HW_PIN_RS485_UART_TX = GPIO18
 *   RS485_DIR -> HW_PIN_RS485_DIR     = GPIO14
 *
 * @version 2.4.0 -- all pins now reference HW_PIN_* macros
 */

#ifndef MODBUS_CFG_H
#define MODBUS_CFG_H

#include <stdint.h>
#include "HardwareConfig.h"

/*==============================================================================
 *                          UART CONFIGURATION
 *============================================================================*/
#define MODBUS_UART_PORT            (2U)
#define MODBUS_UART_NUM             MODBUS_UART_PORT

#define MODBUS_TX_PIN               HW_PIN_RS485_UART_TX   /**< GPIO18 */
#define MODBUS_RX_PIN               HW_PIN_RS485_UART_RX   /**< GPIO17 */
#define MODBUS_DE_RE_PIN            HW_PIN_RS485_DIR        /**< GPIO14 */

#define MODBUS_BAUD_RATE            (9600U)

/*==============================================================================
 *                          BUFFER / TIMING
 *============================================================================*/
#define MODBUS_BUFFER_SIZE              (300U)
#define MODBUS_FRAME_DELAY_US           (4000U)
#define MODBUS_INTER_FRAME_DELAY_MS     (4U)
#define MODBUS_RS485_SWITCH_DELAY_US    (100U)
#define MODBUS_TX_RX_DELAY_US           MODBUS_RS485_SWITCH_DELAY_US

/*==============================================================================
 *                          SLAVE IDENTITY
 *============================================================================*/
#define MODBUS_SLAVE_ID             (1U)
#define MODBUS_SLAVE_ADDRESS        MODBUS_SLAVE_ID

/*==============================================================================
 *                          FUNCTION / EXCEPTION CODES
 *============================================================================*/
#define MODBUS_FC_READ_HOLDING_REGS             (0x03U)
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION       (0x01U)
#define MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS   (0x02U)
#define MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE     (0x03U)

/*==============================================================================
 *                          HOLDING REGISTER MAP
 *============================================================================*/
#define MODBUS_HOLDING_REG_COUNT    (20U)
#define REG_TEMPERATURE_HIGH        (0U)
#define REG_TEMPERATURE_LOW         (1U)
#define REG_HUMIDITY_HIGH           (2U)
#define REG_HUMIDITY_LOW            (3U)
#define REG_O3_HIGH                 (4U)
#define REG_O3_LOW                  (5U)
#define REG_CO_HIGH                 (6U)
#define REG_CO_LOW                  (7U)
#define REG_CO2_HIGH                (8U)
#define REG_CO2_LOW                 (9U)
#define REG_TVOC_HIGH               (10U)
#define REG_TVOC_LOW                (11U)
#define REG_NO2_HIGH                (12U)
#define REG_NO2_LOW                 (13U)
#define REG_CH2O_HIGH               (14U)
#define REG_CH2O_LOW                (15U)
#define REG_PM25_HIGH               (16U)
#define REG_PM25_LOW                (17U)
#define REG_RESERVED_18             (18U)
#define REG_RESERVED_19             (19U)

/*==============================================================================
 *                          STATISTICS / DEBUG
 *============================================================================*/
#define MODBUS_STATS_ENABLE         (1U)
#define MODBUS_DEBUG_ENABLE         (0U)

#if (MODBUS_DEBUG_ENABLE == 1U)
    #define MODBUS_DEBUG_PRINTF(...)  Serial.printf(__VA_ARGS__)
    #define MODBUS_DEBUG_PRINT        MODBUS_DEBUG_PRINTF
    #define MODBUS_DEBUG_PRINTLN(x)   Serial.println(x)
#else
    #define MODBUS_DEBUG_PRINTF(...)
    #define MODBUS_DEBUG_PRINT(...)
    #define MODBUS_DEBUG_PRINTLN(x)
#endif

/*==============================================================================
 *                          VALIDATION
 *============================================================================*/
#if (MODBUS_SLAVE_ID < 1U) || (MODBUS_SLAVE_ID > 247U)
#error "MODBUS_SLAVE_ID must be 1-247"
#endif
#if (MODBUS_HOLDING_REG_COUNT < 1U) || (MODBUS_HOLDING_REG_COUNT > 125U)
#error "MODBUS_HOLDING_REG_COUNT must be 1-125"
#endif

#endif /* MODBUS_CFG_H */