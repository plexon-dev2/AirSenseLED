
/**
 * @file Modbus.cpp
 * @brief Modbus RTU Slave — Air Quality Monitor ESP32-S3
 *
 * FIX 1 — All registers uniform float32 (v2.0.0).
 *   Mixed float/uint16 layout caused ModScan32 misalignment and retries.
 *   All 9 parameters now float32, 2 registers each = 18 total.
 *
 * FIX 2 — Shadow buffer atomic register update (v2.0.0).
 *   memcpy to holding_registers[] is atomic — no torn floats mid-poll.
 *
 * FIX 3 — flush() restored for deterministic TX completion (v2.3.0).
 *   Previously replaced with delayMicroseconds(length * 1100) which was
 *   inaccurate when Core 0 was preempted by WiFi/BLE ISRs mid-delay.
 *   Result: DE/RE flipped LOW while bytes were still on the wire, truncating
 *   the response frame tail. Master retried immediately, sending the next
 *   request while our UART was still settling — corrupting the next RX frame.
 *   Fix: ModbusSerial.flush() blocks until TX FIFO is physically empty.
 *   This is the correct, deterministic way to gate DE/RE on RS485.
 *   flush() is safe in Arduino-ESP32 v2.x+ (no longer blocks indefinitely).
 *
 * FIX 4 — LEDHMI__ModbusPoll() moved OUT of Modbus_ProcessFrame() (v2.2.0).
 *   ROOT CAUSE OF CRC FAIL IN DUAL-CORE MODE:
 *   Previously LEDHMI__ModbusPoll() was called at the TOP of
 *   Modbus_ProcessFrame() — BEFORE CRC validation.
 *   This triggered a Serial.println() on Core 0 at the exact moment
 *   Core 1's LEDHMI__Handler also printed "Master detected" — both
 *   cores contended on Serial simultaneously. The Serial lock contention
 *   caused a brief CPU stall on Core 0 during UART RX byte reading,
 *   corrupting one byte of the incoming frame. CRC then always failed
 *   with the same value (0xC7C5 vs 0xDCC5) because the stall always
 *   happened at the same byte position.
 *   Proof: every CRC FAIL line in Serial log was merged with
 *   "[AQ_LEDHMI] MODBUS LED: Master detected" — 100% correlation.
 *   Fix: LEDHMI__ModbusPoll() moved to AFTER Modbus_SendResponse() in
 *   Modbus_HandleReadHoldingRegisters(). LED behaviour is identical —
 *   still blinks on every valid poll — but now fires after the response
 *   is fully transmitted, not during frame reception and CRC validation.
 *
 * FIX 5 — CRC byte order corrected (v2.2.0).
 *   Received CRC was being read big-endian but Modbus uses little-endian.
 *   frame[length-2] is CRC low byte, frame[length-1] is CRC high byte.
 *
 * FIX 6 — Modbus_HandleReadHoldingRegisters() restored (v2.2.0).
 *   Function was accidentally deleted during editing.
 *
 * @version 2.3.0
 */

#include "Modbus.h"
#include "Modbus_cfg.h"
#include <string.h>
#include "AQ_LEDHMI.h"

/*==============================================================================
 *                          DEBUG PRINT CONTROL
 *============================================================================*/
/**
 * @brief Set to 0 for production, 1 for debug
 * @details Serial prints during Modbus RX cause byte corruption due to
 *          CPU stalls from Serial lock contention. Disable for production.
 */
#define MODBUS_DEBUG_PRINT_ENABLE  (0U)

#if (MODBUS_DEBUG_PRINT_ENABLE == 1U)
  #define MODBUS_DBG_PRINTF(...)   Serial.printf(__VA_ARGS__)
  #define MODBUS_DBG_PRINTLN(...)  Serial.println(__VA_ARGS__)
#else
  #define MODBUS_DBG_PRINTF(...)   /* Disabled */
  #define MODBUS_DBG_PRINTLN(...)  /* Disabled */
#endif

/* ============================================================================
 * PRIVATE VARIABLES
 * ============================================================================ */

static HardwareSerial      ModbusSerial(MODBUS_UART_NUM);
static bool                modbus_initialized = false;
static uint16_t            holding_registers[MODBUS_HOLDING_REG_COUNT] = {0};
static uint8_t             rx_buffer[MODBUS_BUFFER_SIZE];
static uint16_t            rx_index     = 0;
static uint32_t            last_rx_time = 0;
static Modbus_Statistics_t statistics   = {0};

/* ============================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ============================================================================ */

static uint16_t Modbus_CalculateCRC(uint8_t *data, uint16_t length);
static void     Modbus_SetRS485Transmit(void);
static void     Modbus_SetRS485Receive(void);
static void     Modbus_ProcessFrame(uint8_t *frame, uint16_t length);
static void     Modbus_SendResponse(uint8_t *response, uint16_t length);
static void     Modbus_SendException(uint8_t function_code, uint8_t exception_code);
static void     Modbus_HandleReadHoldingRegisters(uint8_t *frame, uint16_t length);
static void     Modbus_FloatToRegisters_LE(float value, uint16_t *low_reg, uint16_t *high_reg);

/* ============================================================================
 * PUBLIC FUNCTIONS
 * ============================================================================ */

void Modbus_Init(void)
{
    MODBUS_DBG_PRINTLN("[MODBUS] v2.3.0 — flush() TX fix active");
    /* Larger RX buffer reduces risk of byte loss during brief CPU stalls
     * caused by WiFi/BLE interrupts on Core 0. */
     //ModbusSerial.setRxBufferSize(512);
     ModbusSerial.begin(MODBUS_BAUD_RATE, SERIAL_8N1,
                       MODBUS_RX_PIN, MODBUS_TX_PIN,
                       false, 256U, 128U);

    MODBUS_DBG_PRINTF("[MODBUS] UART init: uart=%d baud=%lu rx=%d tx=%d de_re=%d\n",
                  (int)MODBUS_UART_NUM, (unsigned long)MODBUS_BAUD_RATE,
                  (int)MODBUS_RX_PIN, (int)MODBUS_TX_PIN, (int)MODBUS_DE_RE_PIN);

    pinMode(MODBUS_DE_RE_PIN, OUTPUT);
    Modbus_SetRS485Receive();

    memset(holding_registers, 0, sizeof(holding_registers));
    Modbus_ResetStatistics();

    /* Flush any stale bytes in the UART RX FIFO */
    while (ModbusSerial.available()) { ModbusSerial.read(); }

    if (MODBUS_RX_PIN < 0 || MODBUS_TX_PIN < 0)
    {
        modbus_initialized = false;
        return;
    }

    rx_index     = 0;
    last_rx_time = 0;

    modbus_initialized = true;

    MODBUS_DBG_PRINTLN("[MODBUS] Ready — FC03 slave");
    MODBUS_DBG_PRINTF ("[MODBUS] Registers: %d (9 params x 2 regs, all float32)\n",
                   MODBUS_HOLDING_REG_COUNT);
    MODBUS_DBG_PRINTLN("[MODBUS] Word order: Little Endian (LOW word first = CDAB)");
    MODBUS_DBG_PRINTLN("[MODBUS] ModScan32: Length=18, Float CDAB or Modicon byte order");
}

/* ----------------------------------------------------------------------------
 * Modbus_Handler
 * Accumulates incoming bytes into rx_buffer.
 * Triggers Modbus_ProcessFrame() after 3.5-character silence (MODBUS_FRAME_DELAY_US).
 * Called from Task_50ms_Core0 — last function in the cycle so all Serial
 * prints from other handlers complete before frame RX begins.
 * -------------------------------------------------------------------------- */
void Modbus_Handler(void)
{
    if (!modbus_initialized) { return; }

    while (ModbusSerial.available())
    {
        if (rx_index < MODBUS_BUFFER_SIZE)
        {
            rx_buffer[rx_index++] = ModbusSerial.read();
            last_rx_time = micros();
        }
        else
        {
            /* Buffer overflow — discard entire frame and reset */
            rx_index = 0;
            while (ModbusSerial.available()) { ModbusSerial.read(); }
            break;
        }
    }

    if (rx_index > 0)
    {
        uint32_t gap = micros() - last_rx_time;
        if (gap >= MODBUS_FRAME_DELAY_US)
        {
            MODBUS_DEBUG_PRINT("[MODBUS] Frame: %d bytes, gap=%luus\n",
                               rx_index, gap);
            Modbus_ProcessFrame(rx_buffer, rx_index);
            rx_index = 0;
        }
    }
}

/* ----------------------------------------------------------------------------
 * Modbus_UpdateSensorData
 * FIX 1 + FIX 2: All float32 + shadow buffer atomic update.
 * Builds a complete shadow copy first, then memcpy in one shot so
 * Modbus_Handler never sees a partially-updated register set.
 * -------------------------------------------------------------------------- */
Modbus_StatusType_t Modbus_UpdateSensorData(const CmdParser_ZPHS01B_Data_t *aq_data)
{
    if (!modbus_initialized || aq_data == NULL) { return MODBUS_STATUS_INIT_FAILED; }

    uint16_t shadow[MODBUS_HOLDING_REG_COUNT];
    memset(shadow, 0, sizeof(shadow));

    uint16_t low, high;

    /* Reg 0-1: Temperature (°C) */
    Modbus_FloatToRegisters_LE(aq_data->temperature, &low, &high);
    shadow[REG_TEMPERATURE_LOW]  = low;
    shadow[REG_TEMPERATURE_HIGH] = high;

    /* Reg 2-3: Humidity (%RH) */
    Modbus_FloatToRegisters_LE((float)aq_data->humidity, &low, &high);
    shadow[REG_HUMIDITY_LOW]  = low;
    shadow[REG_HUMIDITY_HIGH] = high;

    /* Reg 4-5: O3 (ppb) */
    Modbus_FloatToRegisters_LE(aq_data->o3 * 1000.0f, &low, &high);
    shadow[REG_O3_LOW]  = low;
    shadow[REG_O3_HIGH] = high;

    /* Reg 6-7: CO (ppm) */
    Modbus_FloatToRegisters_LE(aq_data->co, &low, &high);
    shadow[REG_CO_LOW]  = low;
    shadow[REG_CO_HIGH] = high;

    /* Reg 8-9: CO2 (ppm) */
    Modbus_FloatToRegisters_LE((float)aq_data->co2, &low, &high);
    shadow[REG_CO2_LOW]  = low;
    shadow[REG_CO2_HIGH] = high;

    /* Reg 10-11: TVOC (grade 0-3) */
    float tvoc = (float)((aq_data->voc <= 3U) ? aq_data->voc : 3U);
    Modbus_FloatToRegisters_LE(tvoc, &low, &high);
    shadow[REG_TVOC_LOW]  = low;
    shadow[REG_TVOC_HIGH] = high;

    /* Reg 12-13: NO2 (ppm) */
    Modbus_FloatToRegisters_LE(aq_data->no2, &low, &high);
    shadow[REG_NO2_LOW]  = low;
    shadow[REG_NO2_HIGH] = high;

    /* Reg 14-15: CH2O (mg/m³) */
    Modbus_FloatToRegisters_LE(aq_data->ch2o, &low, &high);
    shadow[REG_CH2O_LOW]  = low;
    shadow[REG_CH2O_HIGH] = high;

    /* Reg 16-17: PM2.5 (µg/m³) */
    Modbus_FloatToRegisters_LE((float)aq_data->pm2_5, &low, &high);
    shadow[REG_PM25_LOW]  = low;
    shadow[REG_PM25_HIGH] = high;

    /* Atomic copy — Modbus_Handler sees all-old or all-new, never partial */
    memcpy(holding_registers, shadow, sizeof(holding_registers));

    MODBUS_DEBUG_PRINT("[MODBUS] Regs updated — T=%.1f H=%.0f CO2=%.0f\n",
                       aq_data->temperature, (float)aq_data->humidity,
                       (float)aq_data->co2);

    return MODBUS_STATUS_OK;
}

Modbus_StatusType_t Modbus_SetDataValid(uint16_t valid)
{
    if (!modbus_initialized) { return MODBUS_STATUS_INIT_FAILED; }
    (void)valid;
    return MODBUS_STATUS_OK;
}

Modbus_StatusType_t Modbus_GetDataValid(uint16_t *valid)
{
    if (!modbus_initialized || valid == NULL) { return MODBUS_STATUS_INIT_FAILED; }
    *valid = (holding_registers[REG_TEMPERATURE_HIGH] != 0U) ? 1U : 0U;
    return MODBUS_STATUS_OK;
}

Modbus_StatusType_t Modbus_SetHoldingRegister(uint16_t reg_addr, uint16_t value)
{
    if (!modbus_initialized)                  { return MODBUS_STATUS_INIT_FAILED;     }
    if (reg_addr >= MODBUS_HOLDING_REG_COUNT) { return MODBUS_STATUS_INVALID_ADDRESS; }
    holding_registers[reg_addr] = value;
    return MODBUS_STATUS_OK;
}

Modbus_StatusType_t Modbus_GetHoldingRegister(uint16_t reg_addr, uint16_t *value)
{
    if (!modbus_initialized)                  { return MODBUS_STATUS_INIT_FAILED;     }
    if (reg_addr >= MODBUS_HOLDING_REG_COUNT) { return MODBUS_STATUS_INVALID_ADDRESS; }
    if (value == NULL)                        { return MODBUS_STATUS_INVALID_ADDRESS; }
    *value = holding_registers[reg_addr];
    return MODBUS_STATUS_OK;
}

bool Modbus_IsInitialized(void) { return modbus_initialized; }

void Modbus_GetStatistics(Modbus_Statistics_t *stats)
{
    if (stats != NULL) { memcpy(stats, &statistics, sizeof(Modbus_Statistics_t)); }
}

void Modbus_ResetStatistics(void)
{
    memset(&statistics, 0, sizeof(Modbus_Statistics_t));
}

void Modbus_PrintStatistics(void)
{
    MODBUS_DBG_PRINTLN("[MODBUS] ===== STATISTICS =====");
    MODBUS_DBG_PRINTF ("[MODBUS] Frames Received   : %lu\n", statistics.frames_received);
    MODBUS_DBG_PRINTF ("[MODBUS] Frames Transmitted: %lu\n", statistics.frames_transmitted);
    MODBUS_DBG_PRINTF ("[MODBUS] Successful Reads  : %lu\n", statistics.successful_reads);
    MODBUS_DBG_PRINTF ("[MODBUS] CRC Errors        : %lu\n", statistics.crc_errors);
    MODBUS_DBG_PRINTF ("[MODBUS] Invalid Requests  : %lu\n", statistics.invalid_requests);
    MODBUS_DBG_PRINTLN("[MODBUS] ====================");
}

/* ============================================================================
 * PRIVATE FUNCTIONS
 * ============================================================================ */

/* ----------------------------------------------------------------------------
 * Modbus_SetRS485Transmit
 * Assert DE/RE HIGH to enable driver (transmit mode).
 * Guard delay gives the RS485 transceiver time to switch before first bit.
 * -------------------------------------------------------------------------- */
static void Modbus_SetRS485Transmit(void)
{
    digitalWrite(MODBUS_DE_RE_PIN, HIGH);
    delayMicroseconds(MODBUS_RS485_SWITCH_DELAY_US);
}

/* ----------------------------------------------------------------------------
 * Modbus_SetRS485Receive
 * De-assert DE/RE LOW to enable receiver (receive mode).
 * No delay needed here — flush() in Modbus_SendResponse() already guarantees
 * the TX FIFO is physically empty before this is called.
 * -------------------------------------------------------------------------- */
static void Modbus_SetRS485Receive(void)
{
    digitalWrite(MODBUS_DE_RE_PIN, LOW);
}

static uint16_t Modbus_CalculateCRC(uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++)
    {
        crc ^= (uint16_t)data[i];
        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x0001) { crc >>= 1; crc ^= 0xA001; }
            else              { crc >>= 1; }
        }
    }
    return crc;
}

static void Modbus_FloatToRegisters_LE(float value, uint16_t *low_reg, uint16_t *high_reg)
{
    union { float f; uint32_t u32; } c;
    c.f       = value;
    *low_reg  = (uint16_t)(c.u32 & 0xFFFF);
    *high_reg = (uint16_t)((c.u32 >> 16) & 0xFFFF);
}

/* ----------------------------------------------------------------------------
 * Modbus_ProcessFrame
 * FIX 4: LEDHMI__ModbusPoll() NOT called here — moved to after response TX.
 * FIX 5: CRC read as little-endian (low byte = frame[length-2]).
 * -------------------------------------------------------------------------- */
static void Modbus_ProcessFrame(uint8_t *frame, uint16_t length)
{
    statistics.frames_received++;

    MODBUS_DEBUG_PRINT("[MODBUS] Frame #%lu: ", statistics.frames_received);
    for (uint16_t i = 0; i < length; i++) { MODBUS_DEBUG_PRINT("%02X ", frame[i]); }
    MODBUS_DEBUG_PRINT("\n");

    if (length < 5)
    {
        statistics.invalid_requests++;
        return;
    }

    /* FIX 5: Modbus CRC is little-endian — low byte first, high byte second */
    uint16_t received_crc   = ((uint16_t)frame[length - 2])
                            | ((uint16_t)frame[length - 1] << 8);
    uint16_t calculated_crc = Modbus_CalculateCRC(frame, length - 2);

    if (received_crc != calculated_crc)
    {
        statistics.crc_errors++;
        MODBUS_DBG_PRINTF("[MODBUS] CRC FAIL — got 0x%04X expected 0x%04X\n",
                      received_crc, calculated_crc);
        while (ModbusSerial.available()) { ModbusSerial.read(); }
        rx_index = 0;
        return;
    }

    MODBUS_DEBUG_PRINT("[MODBUS] Frame OK — addr=%u FC=%u len=%u\n",
                       frame[0], frame[1], length);

    if (frame[0] != MODBUS_SLAVE_ID)
    {
        statistics.invalid_requests++;
        return;
    }

    switch (frame[1])
    {
        case MODBUS_FC_READ_HOLDING_REGS:
            Modbus_HandleReadHoldingRegisters(frame, length);
            break;
        default:
            Modbus_SendException(frame[1], MODBUS_EXCEPTION_ILLEGAL_FUNCTION);
            statistics.invalid_requests++;
            break;
    }
}

/* ----------------------------------------------------------------------------
 * Modbus_HandleReadHoldingRegisters (FC03)
 *
 * Request frame (8 bytes):
 *   [0] Slave ID
 *   [1] FC = 0x03
 *   [2] Start addr high
 *   [3] Start addr low
 *   [4] Reg count high
 *   [5] Reg count low
 *   [6] CRC low
 *   [7] CRC high
 *
 * Response frame:
 *   [0]   Slave ID
 *   [1]   FC = 0x03
 *   [2]   Byte count = reg_count * 2
 *   [3+]  Register values (high byte first per register)
 *   [n-2] CRC low
 *   [n-1] CRC high
 *
 * FIX 4: LEDHMI__ModbusPoll() called AFTER Modbus_SendResponse() —
 *        never during frame reception or CRC validation.
 * -------------------------------------------------------------------------- */
static void Modbus_HandleReadHoldingRegisters(uint8_t *frame, uint16_t length)
{
    MODBUS_DEBUG_PRINT("[MODBUS] FC03 request — addr=%u len=%u\n", frame[0], length);

    if (length != 8)
    {
        Modbus_SendException(MODBUS_FC_READ_HOLDING_REGS,
                             MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE);
        return;
    }

    uint16_t start_addr = ((uint16_t)frame[2] << 8) | frame[3];
    uint16_t reg_count  = ((uint16_t)frame[4] << 8) | frame[5];

    MODBUS_DEBUG_PRINT("[MODBUS] Read regs: start=%u count=%u\n", start_addr, reg_count);

    if ((start_addr + reg_count) > MODBUS_HOLDING_REG_COUNT)
    {
        Modbus_SendException(MODBUS_FC_READ_HOLDING_REGS,
                             MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
        return;
    }

    /* Build response — max size: 3 header + 125*2 data + 2 CRC = 255 bytes */
    static uint8_t response[256];
    uint16_t idx = 0;

    response[idx++] = MODBUS_SLAVE_ID;
    response[idx++] = MODBUS_FC_READ_HOLDING_REGS;
    response[idx++] = (uint8_t)(reg_count * 2U);   /* byte count */

    /* Registers: high byte first (big-endian per register, per Modbus spec) */
    for (uint16_t i = 0; i < reg_count; i++)
    {
        uint16_t reg_val = holding_registers[start_addr + i];
        response[idx++] = (uint8_t)((reg_val >> 8) & 0xFFU);   /* high byte */
        response[idx++] = (uint8_t)(reg_val & 0xFFU);           /* low byte  */
    }

    /* Append CRC — little-endian (low byte first) */
    uint16_t crc    = Modbus_CalculateCRC(response, idx);
    response[idx++] = (uint8_t)(crc & 0xFFU);
    response[idx++] = (uint8_t)((crc >> 8) & 0xFFU);

    #if MODBUS_DEBUG_ENABLE
    MODBUS_DBG_PRINTF("[MODBUS] Sending %u bytes:", idx);
    for (uint16_t i = 0; i < idx; i++) { MODBUS_DBG_PRINTF(" %02X", response[i]); }
    MODBUS_DBG_PRINTLN();
    #endif

    Modbus_SendResponse(response, idx);

    statistics.successful_reads++;

    /* FIX 4: LEDHMI poll AFTER response is fully transmitted —
     * prevents Serial contention with Core 1 during frame reception */
    LEDHMI__ModbusPoll();
}

/* ----------------------------------------------------------------------------
 * Modbus_SendResponse
 *
 * FIX 3 (v2.3.0): delayMicroseconds() replaced with ModbusSerial.flush().
 *
 * WHY THE DELAY WAS WRONG:
 *   delayMicroseconds(length * 1100) is an estimate of TX time.
 *   If Core 0 is preempted mid-delay by a WiFi or BLE ISR, the actual
 *   elapsed time exceeds the estimate — but the delay has already expired.
 *   The function then falls through to Modbus_SetRS485Receive() and pulls
 *   DE/RE LOW while bytes are still physically on the wire. The RS485
 *   driver is disabled mid-frame, truncating the response. The master
 *   receives a bad frame, retries immediately, and the retry arrives
 *   while our UART RX is still recovering — corrupting the next request.
 *
 * WHY flush() IS CORRECT:
 *   ModbusSerial.flush() blocks until the UART TX FIFO and shift register
 *   are both physically empty — i.e. the last stop bit has left the wire.
 *   Only then does it return. DE/RE is then flipped LOW on an idle bus,
 *   which is exactly what RS485 half-duplex requires.
 *   flush() is safe in Arduino-ESP32 v2.x+ (uses a semaphore internally,
 *   not a spin-wait, so FreeRTOS can schedule other tasks while waiting).
 * -------------------------------------------------------------------------- */
static void Modbus_SendResponse(uint8_t *response, uint16_t length)
{
    if (!modbus_initialized || response == NULL || length == 0U) { return; }

    Modbus_SetRS485Transmit();
    ModbusSerial.write(response, length);

    /* Wait until every bit is physically transmitted before releasing the bus */
    ModbusSerial.flush();
    
    /* FIX: Allow last byte to fully settle on bus before switching to RX.
     * At 9600 baud, 1 char = 1.04ms. 500µs guard ensures clean turnaround. */
    delayMicroseconds(500);

    Modbus_SetRS485Receive();
    statistics.frames_transmitted++;
}

/* ----------------------------------------------------------------------------
 * Modbus_SendException
 * Same flush() fix applied for consistency.
 * -------------------------------------------------------------------------- */
static void Modbus_SendException(uint8_t function_code, uint8_t exception_code)
{
    if (!modbus_initialized) { return; }

    static uint8_t response[5];
    uint8_t idx = 0;

    response[idx++] = MODBUS_SLAVE_ID;
    response[idx++] = function_code | 0x80U;
    response[idx++] = exception_code;

    uint16_t crc    = Modbus_CalculateCRC(response, idx);
    response[idx++] = (uint8_t)(crc & 0xFFU);
    response[idx++] = (uint8_t)((crc >> 8) & 0xFFU);

    Modbus_SetRS485Transmit();
    ModbusSerial.write(response, 5);
    ModbusSerial.flush();           /* wait for last bit before releasing bus */
    delayMicroseconds(500);         /* FIX: Allow bus to settle */
    Modbus_SetRS485Receive();

    statistics.frames_transmitted++;
    statistics.invalid_requests++;
}