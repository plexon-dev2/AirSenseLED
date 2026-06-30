/**
 * @file CmdParser_cfg.h
 * @brief Configuration header for ZPHS01B/ZPHS01C Air Quality Sensor Command Parser
 *        LED Project version
 *
 * @version 2.2.0
 * @date 2025
 *
 * v2.2.0 changes (review fixes):
 *   - CMDPARSER_UART_RX_PIN / TX_PIN: now reference HW_PIN_SENSOR_UARTRX /
 *     HW_PIN_SENSOR_UARTTX from HardwareConfig.h instead of hardcoded integers.
 *     Previous version had comment saying GPIO8/GPIO9 but actual values were 5/4
 *     (old TFT build values) -- pin mismatch corrected.
 *   - CMDPARSER_DEBUG_PRINTF macro added: routes through SERIAL_PRINTF
 *     (mutex-safe). Raw Serial.printf at 1Hz on Core 0 causes Modbus RX corruption.
 *   - CMDPARSER_ENABLE_STATISTICS and CMDPARSER_DEBUG_LEVEL removed -- were
 *     defined but never used in CmdParser.cpp (dead defines).
 *   - Version bumped from 2.1.0 to 2.2.0 to match CmdParser.cpp.
 *
 * IMPORTANT -- DO NOT add ZPHS01B_BYTE_* macros here.
 *   All byte-position identifiers are declared as an enum inside CmdParser.h.
 *   Redefining them as macros here causes preprocessor substitution inside the
 *   enum body, breaking the enum with "expected identifier before '(' token".
 */

#ifndef CMDPARSER_CFG_H
#define CMDPARSER_CFG_H

/*==============================================================================
 *                              INCLUDES
 *============================================================================*/
#include <stdint.h>
#include <stdbool.h>
#include "HardwareConfig.h"
#include "AppMutex.h"       /* SERIAL_PRINTF -- mutex-safe debug output */

/*==============================================================================
 *                           UART CONFIGURATION
 *============================================================================*/

/**
 * @brief UART baud rate - ZPHS01B/ZPHS01C fixed at 9600
 */
#define CMDPARSER_UART_BAUDRATE         9600U

/** @brief UART data bits */
#define CMDPARSER_UART_DATABITS         8U

/** @brief UART stop bits */
#define CMDPARSER_UART_STOPBITS         1U

/** @brief UART parity - none */
#define CMDPARSER_UART_PARITY           0U

/** @brief UART hardware flow control */
#define CMDPARSER_UART_FLOW_CONTROL_ENABLE false

/**
 * @brief UART channel on ESP32-S3
 * @note  1 = Serial1 (HardwareSerial -- sensor UART)
 *        2 = Serial2 (HardwareSerial -- Modbus RS485)
 * @warning Do NOT use channel 0 (USB CDC). It does not support the 4-argument
 *          begin() form and will cause a compile error.
 */
#define CMDPARSER_UART_CHANNEL          1U

/**
 * @brief UART RX pin -- ESP32-S3 GPIO connected to ZPHS01B/C TX line.
 * @note  Sourced from HardwareConfig.h (HW_PIN_SENSOR_UARTRX).
 *        Previous version had hardcoded value 5 (old TFT build) while the
 *        comment stated GPIO8 -- mismatch fixed by referencing HardwareConfig.
 */
#define CMDPARSER_UART_RX_PIN           HW_PIN_SENSOR_UARTRX

/**
 * @brief UART TX pin -- ESP32-S3 GPIO connected to ZPHS01B/C RX line.
 * @note  Sourced from HardwareConfig.h (HW_PIN_SENSOR_UARTTX).
 *        Set to -1 if TX not needed (auto-stream mode, no query commands).
 */
#define CMDPARSER_UART_TX_PIN           HW_PIN_SENSOR_UARTTX

/*==============================================================================
 *                           BUFFER CONFIGURATION
 *============================================================================*/

/**
 * @brief Receive buffer size - must be >= ZPHS01B_FRAME_LENGTH (26)
 */
#define CMDPARSER_RX_BUFFER_SIZE        512U

/*==============================================================================
 *                           TIMING CONFIGURATION
 *============================================================================*/

/**
 * @brief Inter-character timeout in milliseconds
 * @details At 9600 baud each byte takes ~1.04ms. 5ms gives safe margin.
 */
#define CMDPARSER_CHAR_TIMEOUT_MS       5U

/*==============================================================================
 *                      ZPHS01B SENSOR FRAME CONFIGURATION
 *
 * NOTE: ZPHS01B_BYTE_* byte-position identifiers are declared as an enum
 *       inside CmdParser.h. Do NOT redefine them as macros here — doing so
 *       causes preprocessor substitution inside the enum body, breaking the
 *       enum declaration with "expected identifier before '(' token".
 *============================================================================*/

/**
 * @brief ZPHS01B fixed response frame length in bytes
 * @details 26 bytes: Byte0=0xFF start, Byte1=0x86 cmd, Bytes2-24=data, Byte25=checksum
 */
#define ZPHS01B_FRAME_LENGTH            26U

/**
 * @brief ZPHS01B frame start byte
 */
#define ZPHS01B_START_BYTE              0xFFU

/**
 * @brief ZPHS01B frame command byte (echo of query command)
 */
#define ZPHS01B_CMD_BYTE                0x86U

/**
 * @brief Number of air quality parameters extracted from frame
 * @details PM1.0, PM2.5, PM10, CO2, VOC, Temperature, Humidity, CH2O, CO, O3, NO2
 */
#define ZPHS01B_PARAM_COUNT             11U

/**
 * @brief ZPHS01B query command bytes (host sends to request data)
 * @details 9-byte query: FF 01 86 00 00 00 00 00 79
 */
#define ZPHS01B_QUERY_CMD               {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79}
#define ZPHS01B_QUERY_LENGTH            9U

/**
 * @brief Temperature encoding offset
 * @details raw = (temp_C * 10) + 500  =>  temp_C = (raw - 500) * 0.1
 */
#define ZPHS01B_TEMP_OFFSET             500U

/*==============================================================================
 *                      DATA PROCESSING CONFIGURATION
 *============================================================================*/

/** @brief Enable checksum verification */
#define CMDPARSER_ENABLE_CHECKSUM_VERIFY    true

/** @brief Enable error recovery */
#define CMDPARSER_ENABLE_ERROR_RECOVERY     true

/** @brief Maximum consecutive errors before reset */
#define CMDPARSER_MAX_CONSECUTIVE_ERRORS    5U

/** @brief Enable error statistics collection */
#define CMDPARSER_ENABLE_ERROR_STATISTICS   true

/*==============================================================================
 *                          DEBUG CONFIGURATION
 *  @note Will be controlled by ProjectConfig.h once integrated.
 *        CMDPARSER_DEBUG_PRINTF routes through SERIAL_PRINTF (mutex-safe).
 *        Raw Serial.printf at 1Hz on Core 0 causes Modbus RX byte corruption.
 *============================================================================*/

/**
 * @brief Debug level: 0=off, 1=errors, 2=warnings, 3=info, 4=verbose.
 * @note  Keep 0 in production -- Serial output at sensor frame rate (~1Hz)
 *        on Core 0 causes Serial contention with Core 1 (Modbus/LEDHMI).
 */
#define CMDPARSER_DEBUG_LEVEL               0U

#if (CMDPARSER_DEBUG_LEVEL > 0U)
    /**
     * @note MISRA C:2012 Rule 20.10 advisory deviation: variadic macro.
     *       Rationale: no compliant alternative for printf-style debug.
     */
    #define CMDPARSER_DEBUG_PRINTF(...)     SERIAL_PRINTF(__VA_ARGS__)
    #define CMDPARSER_DEBUG_PRINTLN(x)      SERIAL_PRINTF("%s\n", (x))
#else
    #define CMDPARSER_DEBUG_PRINTF(...)
    #define CMDPARSER_DEBUG_PRINTLN(x)
#endif

/*==============================================================================
 *                        COMPILE-TIME VALIDATION
 *============================================================================*/

#if (CMDPARSER_RX_BUFFER_SIZE < ZPHS01B_FRAME_LENGTH)
#error "CMDPARSER_RX_BUFFER_SIZE must be >= ZPHS01B_FRAME_LENGTH (26)"
#endif

#if (CMDPARSER_UART_DATABITS != 7U) && (CMDPARSER_UART_DATABITS != 8U)
#error "CMDPARSER_UART_DATABITS must be 7 or 8"
#endif

#if (CMDPARSER_UART_STOPBITS != 1U) && (CMDPARSER_UART_STOPBITS != 2U)
#error "CMDPARSER_UART_STOPBITS must be 1 or 2"
#endif

#if (CMDPARSER_UART_PARITY > 2U)
#error "CMDPARSER_UART_PARITY must be 0 (None), 1 (Even), or 2 (Odd)"
#endif

#if (CMDPARSER_CHAR_TIMEOUT_MS < 3U) || (CMDPARSER_CHAR_TIMEOUT_MS > 20U)
#error "CMDPARSER_CHAR_TIMEOUT_MS must be between 3 and 20"
#endif

#if (ZPHS01B_PARAM_COUNT != 11U)
#error "ZPHS01B_PARAM_COUNT must be 11"
#endif

#if (CMDPARSER_UART_CHANNEL == 0U)
#error "CMDPARSER_UART_CHANNEL 0 = USB CDC (HWCDC). Use channel 1 (Serial1) for sensor UART."
#endif

#endif /* CMDPARSER_CFG_H */