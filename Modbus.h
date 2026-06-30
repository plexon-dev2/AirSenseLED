/**
 * @file Modbus.h
 * @brief Modbus RTU Slave Interface — Air Quality Monitor ESP32-S3
 * @details Adapted from WeighLink project. Frame engine, CRC, RS485 logic unchanged.
 *          Sensor values sourced from CmdParser_ZPHS01B_Data_t.
 *          All values stored as float32 Little Endian in holding registers.
 *
 * ZPHS01B actual types → stored as float32 in Modbus:
 *   temperature  float   → direct
 *   humidity     uint16  → cast to float
 *   o3           float   → * 1000.0f (ppm → ppb, same as currentData)
 *   co           float   → direct
 *   co2          uint16  → cast to float
 *   tvoc(voc)    uint8   → cast to float (grade 0-3)
 *   no2          float   → direct
 *   ch2o         float   → direct
 *   pm2_5        uint16  → cast to float
 */

#ifndef MODBUS_H
#define MODBUS_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include "Modbus_cfg.h"
#include "CmdParser.h"   // for CmdParser_ZPHS01B_Data_t

/* ============================================================================
 * DATA TYPES
 * ============================================================================ */

typedef enum
{
    MODBUS_STATUS_OK = 0,
    MODBUS_STATUS_INIT_FAILED,
    MODBUS_STATUS_INVALID_FRAME,
    MODBUS_STATUS_CRC_ERROR,
    MODBUS_STATUS_INVALID_SLAVE_ID,
    MODBUS_STATUS_INVALID_FUNCTION_CODE,
    MODBUS_STATUS_INVALID_ADDRESS,
    MODBUS_STATUS_TIMEOUT,
    MODBUS_STATUS_ERROR
} Modbus_StatusType_t;

typedef struct
{
    uint32_t frames_received;
    uint32_t frames_transmitted;
    uint32_t crc_errors;
    uint32_t timeout_errors;
    uint32_t invalid_requests;
    uint32_t successful_reads;
    uint32_t successful_writes;
} Modbus_Statistics_t;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/**
 * @brief Initialize Modbus RTU Slave — call once in setup()
 */
void Modbus_Init(void);

/**
 * @brief Main Modbus handler — call every loop() cycle, non-blocking
 */
void Modbus_Handler(void);

/**
 * @brief Update all 9 sensor registers from parsed ZPHS01B data
 * @details Called inside CmdParser_ZPHS01B_Data block after currentData is set.
 *          Applies same conversions as currentData:
 *          - o3 * 1000.0f (ppm → ppb)
 *          - voc capped at 3
 *          - uint16/uint8 fields cast to float
 * @param aq_data Pointer to parsed sensor frame from CmdParser__GetAirQualityData()
 * @return MODBUS_STATUS_OK on success
 */
Modbus_StatusType_t Modbus_UpdateSensorData(const CmdParser_ZPHS01B_Data_t *aq_data);

/**
 * @brief Set data valid flag (Reg 18) — 1=valid, 0=invalid
 */
Modbus_StatusType_t Modbus_SetDataValid(uint16_t valid);

/**
 * @brief Get data valid flag (Reg 18)
 * @param valid Pointer to store the flag value (0=invalid, 1=valid)
 * @return MODBUS_STATUS_OK on success
 */
Modbus_StatusType_t Modbus_GetDataValid(uint16_t *valid);

/**
 * @brief Set individual 16-bit holding register
 */
Modbus_StatusType_t Modbus_SetHoldingRegister(uint16_t reg_addr, uint16_t value);

/**
 * @brief Get individual 16-bit holding register
 */
Modbus_StatusType_t Modbus_GetHoldingRegister(uint16_t reg_addr, uint16_t *value);

/**
 * @brief Check if initialized
 */
bool Modbus_IsInitialized(void);

/**
 * @brief Get communication statistics
 */
void Modbus_GetStatistics(Modbus_Statistics_t *stats);

/**
 * @brief Reset statistics counters
 */
void Modbus_ResetStatistics(void);

/**
 * @brief Print statistics to Serial
 */
void Modbus_PrintStatistics(void);

#endif /* MODBUS_H */