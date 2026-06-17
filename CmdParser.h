/**
 * @file CmdParser.h
 * @brief Air Quality Sensor Command Parser - Public Interface
 * @details Supports both Winsen ZPHS01B (9-parameter, 26-byte frame) and
 *          ZPHS01C (5-parameter, 14-byte frame) sensors on the same UART.
 *          Sensor type is auto-detected from the start byte of the first
 *          received frame and persisted to NVS so the correct frame length
 *          is used immediately on every subsequent boot.
 *
 * @version 2.1.0 - Added ZPHS01C dual-sensor support + NVS persistence
 * @date 2025
 *
 * ZPHS01B frame (26 bytes, start=0xFF):
 *   PM1.0, PM2.5, PM10, CO2, VOC, Temp, Humidity, CH2O, CO, O3, NO2
 *
 * ZPHS01C frame (14 bytes, start=0x16):
 *   CO2, VOC/CH2O, Humidity, Temp, PM2.5
 *   (PM1.0, PM10, CO, O3, NO2 are zeroed in the data struct)
 */

#ifndef CMDPARSER_H
#define CMDPARSER_H

/*==============================================================================
 *                              INCLUDES
 *============================================================================*/
#include <stdint.h>
#include <stdbool.h>
#include "CmdParser_cfg.h"
#include "SensorConfig.h"

/*==============================================================================
 *                              DEFINES
 *============================================================================*/
#define CMDPARSER_MODULE_VERSION_MAJOR  2U
#define CMDPARSER_MODULE_VERSION_MINOR  1U
#define CMDPARSER_MODULE_VERSION_PATCH  0U

/*==============================================================================
 *                              TYPEDEFS
 *============================================================================*/

/**
 * @brief ZPHS01B byte positions in the 26-byte response frame
 */
typedef enum
{
    ZPHS01B_BYTE_START       = 0U,   /**< 0xFF start byte */
    ZPHS01B_BYTE_CMD         = 1U,   /**< 0x86 command echo */
    ZPHS01B_BYTE_PM10_HIGH   = 2U,   /**< PM1.0 high byte */
    ZPHS01B_BYTE_PM10_LOW    = 3U,   /**< PM1.0 low byte */
    ZPHS01B_BYTE_PM25_HIGH   = 4U,   /**< PM2.5 high byte */
    ZPHS01B_BYTE_PM25_LOW    = 5U,   /**< PM2.5 low byte */
    ZPHS01B_BYTE_PM100_HIGH  = 6U,   /**< PM10 high byte */
    ZPHS01B_BYTE_PM100_LOW   = 7U,   /**< PM10 low byte */
    ZPHS01B_BYTE_CO2_HIGH    = 8U,   /**< CO2 high byte */
    ZPHS01B_BYTE_CO2_LOW     = 9U,   /**< CO2 low byte */
    ZPHS01B_BYTE_VOC         = 10U,  /**< VOC grade (0-3) */
    ZPHS01B_BYTE_TEMP_HIGH   = 11U,  /**< Temperature high byte */
    ZPHS01B_BYTE_TEMP_LOW    = 12U,  /**< Temperature low byte */
    ZPHS01B_BYTE_HUM_HIGH    = 13U,  /**< Humidity high byte */
    ZPHS01B_BYTE_HUM_LOW     = 14U,  /**< Humidity low byte */
    ZPHS01B_BYTE_CH2O_HIGH   = 15U,  /**< CH2O high byte */
    ZPHS01B_BYTE_CH2O_LOW    = 16U,  /**< CH2O low byte */
    ZPHS01B_BYTE_CO_HIGH     = 17U,  /**< CO high byte */
    ZPHS01B_BYTE_CO_LOW      = 18U,  /**< CO low byte */
    ZPHS01B_BYTE_O3_HIGH     = 19U,  /**< O3 high byte */
    ZPHS01B_BYTE_O3_LOW      = 20U,  /**< O3 low byte */
    ZPHS01B_BYTE_NO2_HIGH    = 21U,  /**< NO2 high byte */
    ZPHS01B_BYTE_NO2_LOW     = 22U,  /**< NO2 low byte */
    ZPHS01B_BYTE_RSVD1       = 23U,  /**< Reserved */
    ZPHS01B_BYTE_RSVD2       = 24U,  /**< Reserved */
    ZPHS01B_BYTE_CHECKSUM    = 25U   /**< Checksum byte */
} ZPHS01B_BytePosition_t;

/**
 * @brief Command Parser return status codes
 */
typedef enum
{
    CMDPARSER_STATUS_OK              = 0U, /**< Operation successful */
    CMDPARSER_STATUS_ERROR           = 1U, /**< General error */
    CMDPARSER_STATUS_NOT_INITIALIZED = 2U, /**< Module not initialized */
    CMDPARSER_STATUS_INVALID_PARAM   = 3U, /**< Invalid parameter */
    CMDPARSER_STATUS_NO_DATA         = 4U, /**< No data available */
    CMDPARSER_STATUS_DATA_STALE      = 5U, /**< Data is outdated */
    CMDPARSER_STATUS_BUSY            = 6U, /**< Operation in progress */
    CMDPARSER_STATUS_TIMEOUT         = 7U  /**< Operation timed out */
} CmdParser_StatusType_t;

/**
 * @brief Error types for frame validation
 */
typedef enum
{
    CMDPARSER_ERROR_NONE               = 0U,  /**< No error */
    CMDPARSER_ERROR_INSUFFICIENT_BYTES = 1U,  /**< Frame too short */
    CMDPARSER_ERROR_INVALID_DATA       = 2U,  /**< Invalid start/cmd bytes */
    CMDPARSER_ERROR_CHECKSUM_FAIL      = 11U, /**< Checksum mismatch */
    CMDPARSER_ERROR_BUFFER_OVERFLOW    = 12U, /**< RX buffer overflow */
    CMDPARSER_ERROR_UART_FRAME         = 13U, /**< UART framing error */
    CMDPARSER_ERROR_UART_OVERRUN       = 14U  /**< UART overrun error */
} CmdParser_ErrorType_t;

/**
 * @brief Air quality data structure (shared by both ZPHS01B and ZPHS01C)
 * @details All 11 parameters. Fields not available on ZPHS01C are set to 0:
 *          pm1_0, pm10, co, o3, no2.
 */
typedef struct
{
    uint16_t pm1_0;        /**< PM1.0  ug/m3    — ZPHS01B only (0 on ZPHS01C) */
    uint16_t pm2_5;        /**< PM2.5  ug/m3    — both sensors               */
    uint16_t pm10;         /**< PM10   ug/m3    — ZPHS01B only (0 on ZPHS01C) */
    uint16_t co2;          /**< CO2    ppm      — both sensors               */
    uint8_t  voc;          /**< VOC    grade 0-3 — both sensors              */
    float    temperature;  /**< Temp   degC     — both sensors               */
    uint16_t humidity;     /**< Humidity %RH    — both sensors               */
    float    ch2o;         /**< CH2O   mg/m3    — ZPHS01B only (0 on ZPHS01C) */
    float    co;           /**< CO     ppm      — ZPHS01B only (0 on ZPHS01C) */
    float    o3;           /**< O3     ppm      — ZPHS01B only (0 on ZPHS01C) */
    float    no2;          /**< NO2    ppm      — ZPHS01B only (0 on ZPHS01C) */
    bool     valid;        /**< true when data is valid and fresh */
    uint32_t timestamp_ms; /**< millis() when frame was received */
} CmdParser_ZPHS01B_Data_t;

/*==============================================================================
 *                      SCHEDULER INTERFACE APIs
 *============================================================================*/

/**
 * @brief Initialize the Command Parser module
 * @details Configures UART, creates FreeRTOS mutex, resets all state.
 *          Loads previously detected sensor type from NVS so the correct
 *          frame length is used immediately — no re-detection needed on reboot.
 *          Must be called once at system startup before CmdParser__Handler().
 */
void CmdParser__Init(void);

/**
 * @brief Command Parser main handler — call periodically from scheduler
 * @details Reads UART bytes, auto-detects sensor type from start byte,
 *          collects the correct number of bytes (14 or 26), validates
 *          checksum, extracts parameters into internal data store.
 *          Sensor type is saved to NVS whenever it changes.
 */
void CmdParser__Handler(void);

/*==============================================================================
 *                      DATA ACCESS APIs
 *============================================================================*/

/**
 * @brief Get all air quality parameters in one call
 * @details Primary data access API. Copies latest parsed data into the
 *          caller's struct. Fields unavailable on ZPHS01C are zeroed.
 *
 * @param[out] data  Pointer to caller-allocated data struct
 *
 * @return CMDPARSER_STATUS_OK           Fresh valid data copied
 * @return CMDPARSER_STATUS_NOT_INITIALIZED  Module not initialized
 * @return CMDPARSER_STATUS_INVALID_PARAM    NULL pointer passed
 * @return CMDPARSER_STATUS_NO_DATA          No valid frame received yet
 * @return CMDPARSER_STATUS_DATA_STALE       Last frame older than 5 seconds
 * @return CMDPARSER_STATUS_BUSY             Mutex timeout
 */
CmdParser_StatusType_t CmdParser__GetAirQualityData(CmdParser_ZPHS01B_Data_t *data);

/**
 * @brief Get raw received frame buffer for debugging
 *
 * @param[out] buffer        Output buffer (must be >= 26 bytes)
 * @param[out] buffer_length Actual bytes copied
 * @param[in]  max_length    Size of output buffer
 *
 * @return CmdParser_StatusType_t
 */
CmdParser_StatusType_t CmdParser__GetRawBuffer(uint8_t  *buffer,
                                                uint16_t *buffer_length,
                                                uint16_t  max_length);

/*==============================================================================
 *                      ERROR AND STATUS APIs
 *============================================================================*/

/**
 * @brief Get current error status
 * @param[out] error_type  Pointer to store current error type
 * @return CmdParser_StatusType_t
 */
CmdParser_StatusType_t CmdParser__GetErrorStatus(CmdParser_ErrorType_t *error_type);

/**
 * @brief Clear error status and reset error counters
 * @return CmdParser_StatusType_t
 */
CmdParser_StatusType_t CmdParser__ClearErrorStatus(void);

/**
 * @brief Check if module is initialized
 * @return true if initialized, false otherwise
 */
bool CmdParser__IsInitialized(void);

/**
 * @brief Check if latest data is fresh (within 5 second timeout)
 * @return true if fresh valid data available
 */
bool CmdParser__IsDataFresh(void);

/*==============================================================================
 *                      UTILITY APIs
 *============================================================================*/

/**
 * @brief Reset module to initial state (keeps UART initialized)
 * @return CmdParser_StatusType_t
 */
CmdParser_StatusType_t CmdParser__Reset(void);

/**
 * @brief Get total number of successfully parsed frames
 * @param[out] count  Pointer to store frame count
 * @return CmdParser_StatusType_t
 */
CmdParser_StatusType_t CmdParser__GetTotalMessages(uint32_t *count);

/*==============================================================================
 *                      SENSOR TYPE APIs
 *============================================================================*/

/**
 * @brief Get the currently detected sensor type
 * @return SENSOR_9PARAM  (ZPHS01B, 26-byte frame)
 * @return SENSOR_5PARAM  (ZPHS01C, 14-byte frame)
 * @return SENSOR_TYPE_DEFAULT  if no frame received yet
 */
uint32_t CmdParser__GetSensorType(void);

/**
 * @brief Check if sensor type has been detected from a live frame
 * @return true if at least one valid start byte has been received
 */
bool CmdParser__IsSensorDetected(void);

/**
 * @brief Force sensor type re-detection on the next received frame
 * @details Clears the detected type from both RAM and NVS.
 *          Call this after physically swapping the sensor module so the
 *          parser immediately picks up the new type without a full reboot.
 *          The next incoming start byte (0xFF or 0x16) will re-identify
 *          the sensor and persist the new type to NVS automatically.
 *
 * @note Typical usage in loop() after a sensor-swap event:
 * @code
 *   if (sensor_swap_detected)
 *   {
 *       CmdParser__ResetSensorType();
 *       CmdParser__Reset();   // also flush UART buffer
 *   }
 * @endcode
 */
void CmdParser__ResetSensorType(void);

#endif /* CMDPARSER_H */