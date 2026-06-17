/**
 * @file CmdParser_cfg.h
 * @brief Configuration header for ZPHS01B Air Quality Sensor Command Parser
 *        LED Project version
 *
 * @version 2.1.0 — LED project: updated UART pins to GPIO8 (RX) / GPIO9 (TX)
 * @date 2025
 *
 * PIN CHANGES from TFT build:
 *   CMDPARSER_UART_RX_PIN : 4  → 8   (GPIO8 — SENSOR_UARTRX, moved from SPI-flash pin)
 *   CMDPARSER_UART_TX_PIN : 5  → 9   (GPIO9 — SENSOR_UARTTX, moved from SPI-flash pin)
 *   CMDPARSER_UART_CHANNEL: 1  (unchanged — Serial1)
 *
 * IMPORTANT — DO NOT add ZPHS01B_BYTE_* macros here.
 *   All byte-position identifiers (ZPHS01B_BYTE_PM10_HIGH, ZPHS01B_BYTE_CO2_HIGH,
 *   ZPHS01B_BYTE_PM100_HIGH, etc.) are declared as an enum inside CmdParser.h.
 *   Defining them ALSO as #define macros in this cfg file causes the preprocessor
 *   to replace the enum member names with their numeric values BEFORE the compiler
 *   sees the enum — resulting in "expected identifier before '(' token" errors.
 *   Keep byte-position identifiers in the enum in CmdParser.h ONLY.
 */

#ifndef CMDPARSER_CFG_H
#define CMDPARSER_CFG_H

/*==============================================================================
 *                              INCLUDES
 *============================================================================*/
#include <stdint.h>
#include <stdbool.h>

/*==============================================================================
 *                           UART CONFIGURATION
 *============================================================================*/

/**
 * @brief UART baud rate - ZPHS01B fixed at 9600
 */
#define CMDPARSER_UART_BAUDRATE         9600U

/**
 * @brief UART data bits
 */
#define CMDPARSER_UART_DATABITS         8U

/**
 * @brief UART stop bits
 */
#define CMDPARSER_UART_STOPBITS         1U

/**
 * @brief UART parity - none for ZPHS01B
 */
#define CMDPARSER_UART_PARITY           0U

/**
 * @brief UART hardware flow control
 */
#define CMDPARSER_UART_FLOW_CONTROL_ENABLE false

/**
 * @brief UART channel on ESP32-S3
 * @note  0 = Serial  (USB CDC / HWCDC — do NOT use for sensor; only accepts 1 arg in begin())
 *        1 = Serial1 (HardwareSerial — sensor UART)
 *        2 = Serial2 (HardwareSerial — Modbus RS485)
 * @warning Keep this as 1 for the sensor port. If set to 0 the compiler will
 *          report "no matching function for HWCDC::begin(4 args)" because the
 *          USB CDC Serial does not support the 4-argument begin() form.
 */
#define CMDPARSER_UART_CHANNEL          1U

/**
 * @brief UART RX pin — ESP32-S3 GPIO for ZPHS01B TX line
 * @note  LED build: GPIO8 (SENSOR_UARTRX) — moved from GPIO4 (SPI flash conflict)
 */
#define CMDPARSER_UART_RX_PIN           5

/**
 * @brief UART TX pin — ESP32-S3 GPIO for ZPHS01B RX line
 * @note  LED build: GPIO9 (SENSOR_UARTTX) — moved from GPIO5 (SPI flash conflict)
 *        Set to -1 if TX not needed (query not used, auto-stream mode)
 */
#define CMDPARSER_UART_TX_PIN           4

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
 *============================================================================*/

/**
 * @brief Debug level
 * @note  0=None, 1=Error only, 2=Error+Warn, 3=Info, 4=Verbose
 */
#define CMDPARSER_DEBUG_LEVEL               1U

/** @brief Enable statistics collection */
#define CMDPARSER_ENABLE_STATISTICS         true

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
