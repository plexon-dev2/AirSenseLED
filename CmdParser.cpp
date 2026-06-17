/**
 * @file CmdParser.cpp
 * @brief Air Quality Sensor Command Parser Implementation
 * @details Supports both Winsen ZPHS01B (9-parameter, 26-byte frame) and
 *          ZPHS01C (5-parameter, 14-byte frame) sensors on the same UART.
 *          Sensor type is auto-detected from the start byte of the first
 *          received frame and persisted to NVS so the correct frame length
 *          is used immediately on every subsequent boot.
 *
 * @version 2.2.0 - Fixed ZPHS01C first-boot deadlock + UART settle delay
 * @date 2025
 *
 * ZPHS01B FRAME FORMAT (26 bytes):
 *   [0]     0xFF         Start byte
 *   [1]     0x86         Command echo
 *   [2-3]   PM1.0        ug/m3  big-endian uint16
 *   [4-5]   PM2.5        ug/m3  big-endian uint16
 *   [6-7]   PM10         ug/m3  big-endian uint16
 *   [8-9]   CO2          ppm    big-endian uint16
 *   [10]    VOC          grade 0-3
 *   [11-12] Temperature  raw = (C*10)+500, big-endian uint16
 *   [13-14] Humidity     %RH    big-endian uint16
 *   [15-16] CH2O         x0.001 mg/m3, big-endian uint16
 *   [17-18] CO           x0.1 ppm,     big-endian uint16
 *   [19-20] O3           x0.01 ppm,    big-endian uint16
 *   [21-22] NO2          x0.01 ppm,    big-endian uint16
 *   [23-24] 0x0000       Reserved
 *   [25]    Checksum     (~sum(byte1..byte24) + 1) & 0xFF
 *
 * ZPHS01C FRAME FORMAT (14 bytes):
 *   [0]     0x16         Start byte
 *   [1]     0x0B         Length byte
 *   [2]     0x01         Command byte
 *   [3-4]   CO2          ppm    big-endian uint16
 *   [5-6]   VOC/CH2O     grade  big-endian uint16 (bits[1:0] = grade 0-3)
 *   [7-8]   Humidity     raw/10 = %RH, big-endian uint16
 *   [9-10]  Temperature  raw = (C*10)+500, big-endian uint16
 *   [11-12] PM2.5        ug/m3  big-endian uint16
 *   [13]    Checksum     (~sum(byte0..byte12) + 1) & 0xFF
 *
 * FIX LOG (v2.2.0):
 *   FIX-1: ZPHS01C first-boot deadlock — startup command now sent speculatively
 *          when NVS is empty so the sensor begins streaming on the very first boot.
 *          ZPHS01B safely ignores the command (different protocol / start byte).
 *   FIX-2: Added delay(100) + UART flush before sending startup command so the
 *          UART peripheral has time to settle after begin().
 *   FIX-3: last_complete_buffer no longer cleared at start of new frame reception.
 *          It is updated only after a successful parse, preventing GetRawBuffer()
 *          from returning zeros during active streaming.
 */

/*==============================================================================
 *                              INCLUDES
 *============================================================================*/
#include "CmdParser.h"
#include <Arduino.h>
#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string.h>
#include <stdlib.h>
#include "SensorConfig.h"
#include <Preferences.h>

/*==============================================================================
 *                              DEFINES
 *============================================================================*/

/**
 * @brief Set to 1 to enable verbose sensor frame logging.
 * @warning KEEP 0 IN PRODUCTION — these Serial.printf calls run on Core 0
 *          every sensor frame (~1 Hz) and cause Serial contention with Core 1
 *          (LEDHMI, Modbus), corrupting Modbus frames → CRC FAIL.
 */
#define CMDPARSER_DEBUG_ENABLE          0

/** @brief ZPHS01C frame constants */
#define ZPHS01C_FRAME_LENGTH            14U
#define ZPHS01C_START_BYTE              0x16U
#define ZPHS01C_CMD_BYTE                0x01U
#define ZPHS01C_TEMP_OFFSET             500U

/**
 * @brief ZPHS01C active-upload enable command (host -> sensor).
 * @details Per datasheet: "11 02 01 00 EC"
 *          Must be sent before the sensor will stream any frames.
 *          After a valid response the module uploads data every second
 *          automatically until power-off — no need to repeat.
 *
 * @note ZPHS01B SAFETY: The ZPHS01B uses a completely different binary
 *       protocol (0xFF start byte, 0x86 cmd echo). It will not interpret
 *       this 5-byte sequence as a valid command and will continue to
 *       auto-stream its 26-byte frames normally. Sending this command
 *       speculatively when the sensor type is unknown is therefore harmless.
 */
static const uint8_t ZPHS01C_ACTIVE_UPLOAD_CMD[] = {0x11U, 0x02U, 0x01U, 0x00U, 0xECU};
#define ZPHS01C_ACTIVE_UPLOAD_CMD_LEN   5U

/**
 * @brief Milliseconds to wait after UART begin() before writing to the bus.
 * @details Matches the settle delay used in the standalone test sketch.
 *          Without this the startup command may be corrupted or missed.
 */
#define CMDPARSER_UART_SETTLE_MS        100U

/** @brief Max time to wait for a complete frame after the first byte */
#define MESSAGE_TIMEOUT_MS              250U

/** @brief Max age of data before it is considered stale (5 seconds) */
#define DATA_FRESHNESS_TIMEOUT_MS       5000U

/*==============================================================================
 *                              TYPEDEFS
 *============================================================================*/

typedef enum
{
    CMDPARSER_STATE_UNINITIALIZED = 0U,
    CMDPARSER_STATE_INITIALIZED   = 1U,
    CMDPARSER_STATE_RECEIVING     = 2U,
    CMDPARSER_STATE_PROCESSING    = 3U,
    CMDPARSER_STATE_DATA_READY    = 4U,
    CMDPARSER_STATE_ERROR         = 5U
} CmdParser_InternalState_t;

/*==============================================================================
 *                              UART PORT SELECTION
 *============================================================================*/

#if (CMDPARSER_UART_CHANNEL == 0U)
#define UART_PORT Serial
#elif (CMDPARSER_UART_CHANNEL == 1U)
#define UART_PORT Serial1
#elif (CMDPARSER_UART_CHANNEL == 2U)
#define UART_PORT Serial2
#else
#error "Invalid CMDPARSER_UART_CHANNEL. Must be 0, 1, or 2"
#endif

/*==============================================================================
 *                          STATIC VARIABLES
 *============================================================================*/

/* Module state */
static volatile CmdParser_InternalState_t module_state    = CMDPARSER_STATE_UNINITIALIZED;
static volatile bool                      module_initialized = false;
static volatile CmdParser_ErrorType_t     current_error    = CMDPARSER_ERROR_NONE;

/* Sensor type detection */
static uint32_t s_detected_sensor_type = SENSOR_TYPE_DEFAULT;
static bool     s_sensor_detected      = false;

/* UART reception */
static uint8_t           rx_buffer[ZPHS01B_FRAME_LENGTH];       /* sized for the larger frame */
static uint8_t           last_complete_buffer[ZPHS01B_FRAME_LENGTH];
static volatile uint16_t bytes_received      = 0U;
static uint16_t          last_complete_length = 0U;
static volatile uint32_t message_start_time  = 0U;
static volatile uint32_t last_char_time      = 0U;
static volatile bool     message_complete    = false;
static volatile bool     new_data_available  = false;
static volatile bool     raw_buffer_available = false;

/* Debug frame counters */
static uint32_t s_frame_counter_9param = 0U;
static uint32_t s_frame_counter_5param = 0U;

/* Parsed air quality data */
static CmdParser_ZPHS01B_Data_t current_aq_data = {0U, 0U, 0U, 0U, 0U,
                                                    0.0f, 0U, 0.0f, 0.0f,
                                                    0.0f, 0.0f, false, 0U};

/* Freshness tracking */
static volatile uint32_t last_valid_data_time = 0U;

/* Statistics */
static uint32_t total_messages_received = 0U;
static uint32_t total_errors_detected   = 0U;
static uint32_t consecutive_errors      = 0U;

/* NVS persistence for sensor type */
static Preferences s_prefs;
#define NVS_NAMESPACE  SENSOR_NVS_NAMESPACE
#define NVS_KEY_SENSOR SENSOR_NVS_KEY_TYPE

/* FreeRTOS synchronization */
static SemaphoreHandle_t data_mutex   = NULL;
static portMUX_TYPE      critical_mux = portMUX_INITIALIZER_UNLOCKED;

/*==============================================================================
 *                     STATIC FUNCTION PROTOTYPES
 *============================================================================*/
static bool                  IsValidPointer(const void *ptr);
static void                  ResetMessageState(void);
static void                  UpdateErrorStatus(CmdParser_ErrorType_t error_type);
static bool                  IsDataFresh(void);
static bool                  VerifyChecksum(const uint8_t *frame, uint16_t length);
static CmdParser_ErrorType_t ParseMessage(void);
static CmdParser_ErrorType_t ParseMessage_ZPHS01C(void);
static void                  CmdParser_DetectAndHandleSensorType(uint8_t start_byte);
static void                  CmdParser_ZPHS01C_SendStartupCommands(void);

/*==============================================================================
 * PRIVATE — ZPHS01C Startup Command Sequence
 *
 * Per datasheet the ZPHS01C does NOT auto-stream on power-up.
 * The active-upload enable command must be sent first: 11 02 01 00 EC
 * After a valid response the sensor streams frames every second automatically.
 *
 * Called from:
 *   1. CmdParser__Init()  — always (both when NVS has saved type AND when
 *                           type is unknown / NVS is empty).  See FIX-1.
 *   2. CmdParser_DetectAndHandleSensorType() — when ZPHS01C is detected
 *      for the first time from a live start byte (covers sensor-swap case).
 *============================================================================*/
static void CmdParser_ZPHS01C_SendStartupCommands(void)
{
    UART_PORT.write(ZPHS01C_ACTIVE_UPLOAD_CMD, ZPHS01C_ACTIVE_UPLOAD_CMD_LEN);

#if CMDPARSER_DEBUG_ENABLE
    Serial.println("[CMDPARSER] ZPHS01C: active-upload command sent (11 02 01 00 EC)");
    Serial.println("[CMDPARSER] ZPHS01C: sensor will stream in ~1 s");
#endif
}

/*==============================================================================
 * PRIVATE — Sensor type detection and NVS persistence
 * Called on every received start byte. NVS written only when type changes.
 * Accepts either known start byte so detection works on the very first frame.
 *============================================================================*/
static void CmdParser_DetectAndHandleSensorType(uint8_t start_byte)
{
    uint32_t new_type = SENSOR_TYPE_DEFAULT;

    if      (start_byte == SENSOR_9PARAM_START_BYTE) { new_type = SENSOR_9PARAM; }
    else if (start_byte == SENSOR_5PARAM_START_BYTE) { new_type = SENSOR_5PARAM; }
    else    { return; }   /* unknown start byte — ignore */

    /* Always mark as detected from a live frame */
    s_sensor_detected = true;

    /* Only update + persist when the type has actually changed */
    if (new_type != s_detected_sensor_type)
    {
        s_detected_sensor_type = new_type;

        s_prefs.begin(NVS_NAMESPACE, false);
        s_prefs.putUInt(NVS_KEY_SENSOR, new_type);
        s_prefs.end();

#if CMDPARSER_DEBUG_ENABLE
        Serial.printf("[CMDPARSER] Sensor type CHANGED -> %s — saved to NVS\n",
                      (new_type == SENSOR_5PARAM) ? "ZPHS01C (5-param)" : "ZPHS01B (9-param)");
#endif

        /*
         * If newly detected as ZPHS01C send startup commands now.
         * This path is reached when the ZPHS01C was already streaming
         * (e.g. because CmdParser__Init sent the command speculatively)
         * and we are confirming the type for the first time — OR after
         * a physical sensor swap (CmdParser__ResetSensorType was called).
         */
        if (new_type == SENSOR_5PARAM)
        {
            CmdParser_ZPHS01C_SendStartupCommands();
        }
    }
}

/*==============================================================================
 * PRIVATE — ZPHS01C Frame Parser  (5-parameter, 14 bytes)
 * Frame layout: 16 0B 01 CO2(2) VOC(2) Hum(2) Temp(2) PM2.5(2) CS
 *============================================================================*/
static CmdParser_ErrorType_t ParseMessage_ZPHS01C(void)
{
    uint16_t raw_u16;

    if (bytes_received != ZPHS01C_FRAME_LENGTH)
        return CMDPARSER_ERROR_INSUFFICIENT_BYTES;

    if ((rx_buffer[0] != ZPHS01C_START_BYTE) || (rx_buffer[2] != ZPHS01C_CMD_BYTE))
        return CMDPARSER_ERROR_INVALID_DATA;

    /* Checksum: ~(sum of byte0..byte12) + 1 */
    uint8_t sum = 0U;
    for (uint8_t i = 0U; i < (ZPHS01C_FRAME_LENGTH - 1U); i++)
        sum += rx_buffer[i];
    uint8_t expected_cs = (~sum) + 1U;
    if (expected_cs != rx_buffer[ZPHS01C_FRAME_LENGTH - 1U])
        return CMDPARSER_ERROR_CHECKSUM_FAIL;

    /* CO2 — Byte 3-4 */
    raw_u16 = ((uint16_t)rx_buffer[3] << 8U) | (uint16_t)rx_buffer[4];
    current_aq_data.co2 = raw_u16;

    /* VOC/CH2O — Byte 5-6 (bits[1:0] = grade 0-3) */
    raw_u16 = ((uint16_t)rx_buffer[5] << 8U) | (uint16_t)rx_buffer[6];
    current_aq_data.voc  = (uint8_t)(raw_u16 & 0x03U);
    current_aq_data.ch2o = 0.0f;

    /* Humidity — Byte 7-8  (raw / 10 = %RH) */
    raw_u16 = ((uint16_t)rx_buffer[7] << 8U) | (uint16_t)rx_buffer[8];
    current_aq_data.humidity = raw_u16 / 10U;

    /* Temperature — Byte 9-10  ((raw - 500) / 10 = degC) */
    raw_u16 = ((uint16_t)rx_buffer[9] << 8U) | (uint16_t)rx_buffer[10];
    current_aq_data.temperature = ((float)raw_u16 - (float)ZPHS01C_TEMP_OFFSET) * 0.1f;

    /* PM2.5 — Byte 11-12 */
    raw_u16 = ((uint16_t)rx_buffer[11] << 8U) | (uint16_t)rx_buffer[12];
    current_aq_data.pm2_5 = raw_u16;

    /* Parameters not available on ZPHS01C — zero them */
    current_aq_data.pm1_0 = 0U;
    current_aq_data.pm10  = 0U;
    current_aq_data.co    = 0.0f;
    current_aq_data.o3    = 0.0f;
    current_aq_data.no2   = 0.0f;

    current_aq_data.valid        = true;
    current_aq_data.timestamp_ms = millis();
    s_frame_counter_5param++;

    return CMDPARSER_ERROR_NONE;
}

/*==============================================================================
 * PRIVATE — ZPHS01B Frame Parser  (9-parameter, 26 bytes)
 *============================================================================*/
static CmdParser_ErrorType_t ParseMessage(void)
{
    uint16_t raw_u16;

    if (bytes_received != ZPHS01B_FRAME_LENGTH)
        return CMDPARSER_ERROR_INSUFFICIENT_BYTES;

    if ((rx_buffer[ZPHS01B_BYTE_START] != ZPHS01B_START_BYTE) ||
        (rx_buffer[ZPHS01B_BYTE_CMD]   != ZPHS01B_CMD_BYTE))
        return CMDPARSER_ERROR_INVALID_DATA;

#if (CMDPARSER_ENABLE_CHECKSUM_VERIFY == true)
    if (!VerifyChecksum(rx_buffer, ZPHS01B_FRAME_LENGTH))
        return CMDPARSER_ERROR_CHECKSUM_FAIL;
#endif

    raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_PM10_HIGH] << 8U) |
               (uint16_t)rx_buffer[ZPHS01B_BYTE_PM10_LOW];
    current_aq_data.pm1_0 = raw_u16;

    raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_PM25_HIGH] << 8U) |
               (uint16_t)rx_buffer[ZPHS01B_BYTE_PM25_LOW];
    current_aq_data.pm2_5 = raw_u16;

    raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_PM100_HIGH] << 8U) |
               (uint16_t)rx_buffer[ZPHS01B_BYTE_PM100_LOW];
    current_aq_data.pm10 = raw_u16;

    raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_CO2_HIGH] << 8U) |
               (uint16_t)rx_buffer[ZPHS01B_BYTE_CO2_LOW];
    current_aq_data.co2 = raw_u16;

    current_aq_data.voc = rx_buffer[ZPHS01B_BYTE_VOC];

    raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_TEMP_HIGH] << 8U) |
               (uint16_t)rx_buffer[ZPHS01B_BYTE_TEMP_LOW];
    current_aq_data.temperature = ((float)raw_u16 - (float)ZPHS01B_TEMP_OFFSET) * 0.1f;

    raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_HUM_HIGH] << 8U) |
               (uint16_t)rx_buffer[ZPHS01B_BYTE_HUM_LOW];
    current_aq_data.humidity = raw_u16 / 10U;

    raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_CH2O_HIGH] << 8U) |
               (uint16_t)rx_buffer[ZPHS01B_BYTE_CH2O_LOW];
    current_aq_data.ch2o = (float)raw_u16 * 0.001f;

    raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_CO_HIGH] << 8U) |
               (uint16_t)rx_buffer[ZPHS01B_BYTE_CO_LOW];
    current_aq_data.co = (float)raw_u16 * 0.1f;

    raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_O3_HIGH] << 8U) |
               (uint16_t)rx_buffer[ZPHS01B_BYTE_O3_LOW];
    current_aq_data.o3 = (float)raw_u16 * 0.01f;

    raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_NO2_HIGH] << 8U) |
               (uint16_t)rx_buffer[ZPHS01B_BYTE_NO2_LOW];
    current_aq_data.no2 = (float)raw_u16 * 0.01f;

    current_aq_data.valid        = true;
    current_aq_data.timestamp_ms = millis();
    s_frame_counter_9param++;

    return CMDPARSER_ERROR_NONE;
}

/*==============================================================================
 *                     PUBLIC FUNCTION IMPLEMENTATIONS
 *============================================================================*/

/**
 * @brief Initialize the Command Parser module.
 *
 * CHANGE v2.2.0 (FIX-1 + FIX-2):
 *   The ZPHS01C startup command is now ALWAYS sent during Init, regardless of
 *   whether the sensor type was saved in NVS.  This eliminates the first-boot
 *   deadlock that existed when NVS was empty:
 *
 *     Old behaviour (broken on empty NVS):
 *       No startup command sent → sensor silent → no start byte received →
 *       type never detected → startup command never sent → ∞ deadlock
 *
 *     New behaviour (fixed):
 *       Startup command sent speculatively → ZPHS01C starts streaming →
 *       0x16 start byte received → type confirmed and saved to NVS
 *       ZPHS01B: ignores the command safely, continues to auto-stream 0xFF frames
 *
 *   A 100 ms settle delay + UART flush are also added before writing the
 *   startup command so the UART peripheral is stable (mirrors the test sketch).
 */
void CmdParser__Init(void)
{
    /* ── 1. Create FreeRTOS mutex ─────────────────────────────────────────── */
    if (data_mutex == NULL)
    {
        data_mutex = xSemaphoreCreateMutex();
    }

    /* ── 2. Initialize UART ───────────────────────────────────────────────── */
    UART_PORT.setRxBufferSize(CMDPARSER_RX_BUFFER_SIZE);
    UART_PORT.begin(CMDPARSER_UART_BAUDRATE,
                    SERIAL_8N1,
                    CMDPARSER_UART_RX_PIN,
                    CMDPARSER_UART_TX_PIN);

    /* FIX-2: Wait for UART peripheral to fully settle before writing.
     *         Without this the startup command can be clocked out before
     *         the baud-rate generator is stable and arrives corrupted. */
    delay(CMDPARSER_UART_SETTLE_MS);

    /* Flush any garbage that arrived during power-up or UART init */
    while (UART_PORT.available() > 0) { (void)UART_PORT.read(); }

    /* ── 3. Reset all internal state under mutex ──────────────────────────── */
    if (xSemaphoreTake(data_mutex, portMAX_DELAY) == pdTRUE)
    {
        module_state       = CMDPARSER_STATE_INITIALIZED;
        module_initialized = true;
        current_error      = CMDPARSER_ERROR_NONE;

        ResetMessageState();

        memset(&current_aq_data,     0, sizeof(current_aq_data));
        memset(last_complete_buffer, 0, sizeof(last_complete_buffer));

        last_complete_length    = 0U;
        raw_buffer_available    = false;
        total_messages_received = 0U;
        total_errors_detected   = 0U;
        consecutive_errors      = 0U;
        last_valid_data_time    = 0U;
        new_data_available      = false;

        xSemaphoreGive(data_mutex);
    }

    /* ── 4. Load persisted sensor type from NVS ───────────────────────────── */
    s_prefs.begin(NVS_NAMESPACE, true);
    uint32_t saved = s_prefs.getUInt(NVS_KEY_SENSOR, 0xFFFFFFFFU);
    s_prefs.end();

    if ((saved == SENSOR_9PARAM) || (saved == SENSOR_5PARAM))
    {
        /* NVS has a valid saved type — use it immediately */
        s_detected_sensor_type = saved;
        s_sensor_detected      = true;

#if CMDPARSER_DEBUG_ENABLE
        Serial.printf("[CMDPARSER] NVS: sensor type loaded = %s\n",
                      (saved == SENSOR_5PARAM) ? "ZPHS01C (5-param)" : "ZPHS01B (9-param)");
#endif

        /*
         * ZPHS01C does not auto-stream after power-up — always send the
         * startup command on every boot even when NVS already knows the type.
         *
         * ZPHS01B: if NVS says SENSOR_9PARAM we do NOT send the ZPHS01C
         * command — the ZPHS01B auto-streams and doesn't need it.
         */
        if (saved == SENSOR_5PARAM)
        {
            CmdParser_ZPHS01C_SendStartupCommands();
        }
    }
    else
    {
        /*
         * FIX-1: NVS is empty (first ever boot, or after flash erase).
         * Sensor type is unknown — send the ZPHS01C startup command
         * speculatively so that a ZPHS01C will begin streaming immediately.
         *
         * Why this is safe for ZPHS01B:
         *   The ZPHS01B uses a completely different binary protocol:
         *     - Start byte 0xFF  (vs 0x16 for ZPHS01C)
         *     - Command echo 0x86 in its response frames
         *     - Actively streams 26-byte frames with no enable command required
         *   The ZPHS01C command bytes {11 02 01 00 EC} do not match any valid
         *   ZPHS01B host command, so the ZPHS01B silently ignores them and
         *   continues to auto-stream its 26-byte frames unchanged.
         *
         * Detection sequence after this call:
         *   ZPHS01C boots → receives startup command → starts streaming 0x16 frames
         *     → CmdParser__Handler picks up 0x16 → DetectAndHandleSensorType()
         *     → saves SENSOR_5PARAM to NVS → all subsequent boots use NVS path.
         *   ZPHS01B boots → ignores command → auto-streams 0xFF frames
         *     → CmdParser__Handler picks up 0xFF → DetectAndHandleSensorType()
         *     → saves SENSOR_9PARAM to NVS → all subsequent boots use NVS path.
         */
        s_detected_sensor_type = SENSOR_TYPE_DEFAULT;
        s_sensor_detected      = false;

        CmdParser_ZPHS01C_SendStartupCommands();   /* FIX-1: always send */

#if CMDPARSER_DEBUG_ENABLE
        Serial.println("[CMDPARSER] NVS: no saved type — startup cmd sent speculatively."
                       " Will auto-detect from first received frame.");
#endif
    }

#if CMDPARSER_DEBUG_ENABLE
    Serial.printf("[CMDPARSER] Parser ready — expecting %u-byte frames initially\n",
                  (s_detected_sensor_type == SENSOR_5PARAM) ?
                  ZPHS01C_FRAME_LENGTH : ZPHS01B_FRAME_LENGTH);
#endif
}

/*==============================================================================
 *                          MAIN HANDLER
 *============================================================================*/
void CmdParser__Handler(void)
{
    uint32_t current_time;
    int      available_bytes;
    bool     timeout_detected = false;
    bool     should_process   = false;

    if (!module_initialized)
    {
        return;
    }

    current_time    = millis();
    available_bytes = UART_PORT.available();

    /*
     * Compute active frame length and start byte from currently known sensor type.
     * These are recomputed immediately after byte-0 detection inside the read loop
     * so the correct frame length is used on the very first frame even before
     * the type has been confirmed.
     */
    uint8_t active_frame_len  = (s_detected_sensor_type == SENSOR_5PARAM) ?
                                 ZPHS01C_FRAME_LENGTH : ZPHS01B_FRAME_LENGTH;
    uint8_t active_start_byte = (s_detected_sensor_type == SENSOR_5PARAM) ?
                                 ZPHS01C_START_BYTE : ZPHS01B_START_BYTE;

    if (available_bytes > 0)
    {
        taskENTER_CRITICAL(&critical_mux);

        /*
         * FIX-3: Do NOT clear last_complete_buffer here.
         *
         * Old code wiped last_complete_buffer + set raw_buffer_available=false
         * at the start of every new frame reception.  This meant that between
         * the moment a new frame started arriving and the moment it was fully
         * parsed, GetRawBuffer() would return CMDPARSER_STATUS_NO_DATA and
         * zeros — losing the last valid raw frame unnecessarily.
         *
         * last_complete_buffer is now updated ONLY inside the successful-parse
         * branch below (via memcpy after parse_result == CMDPARSER_ERROR_NONE).
         * It remains valid and readable at all other times.
         */
        if (bytes_received == 0U)
        {
            module_state       = CMDPARSER_STATE_RECEIVING;
            message_start_time = current_time;
            /* last_complete_buffer intentionally NOT cleared here (FIX-3) */
        }

        /* Read bytes into buffer */
        while (UART_PORT.available() > 0)
        {
            int received_byte = UART_PORT.read();
            if (received_byte < 0) { continue; }

            /* ── Byte 0: synchronise to start byte + detect sensor type ──── */
            if (bytes_received == 0U)
            {
                uint8_t b = (uint8_t)received_byte;

                /*
                 * Accept either known start byte so detection works on the
                 * very first frame even before s_detected_sensor_type is set.
                 * Discard any other byte silently.
                 */
                if ((b != ZPHS01B_START_BYTE) && (b != ZPHS01C_START_BYTE))
                {
                    continue;   /* not a valid start byte — discard */
                }

                /* Identify the sensor from this start byte */
                CmdParser_DetectAndHandleSensorType(b);

                /*
                 * Recompute frame length RIGHT NOW after detection so the
                 * rest of this frame is collected to the correct size.
                 */
                active_frame_len  = (s_detected_sensor_type == SENSOR_5PARAM) ?
                                     ZPHS01C_FRAME_LENGTH : ZPHS01B_FRAME_LENGTH;
                active_start_byte = (s_detected_sensor_type == SENSOR_5PARAM) ?
                                     ZPHS01C_START_BYTE : ZPHS01B_START_BYTE;
            }

            /* Fill buffer up to the correct frame length */
            if (bytes_received < active_frame_len)
            {
                rx_buffer[bytes_received] = (uint8_t)received_byte;
                bytes_received++;
                last_char_time = current_time;
            }

            /* Stop as soon as the complete frame is in the buffer */
            if (bytes_received >= active_frame_len)
            {
                message_complete = true;
                break;
            }
        }

        taskEXIT_CRITICAL(&critical_mux);
    }

    /* Detect inter-character timeout */
    if ((bytes_received > 0U) && ((current_time - message_start_time) > MESSAGE_TIMEOUT_MS))
    {
        timeout_detected = true;
    }

    taskENTER_CRITICAL(&critical_mux);
    should_process = (message_complete || timeout_detected);
    taskEXIT_CRITICAL(&critical_mux);

    if (should_process)
    {
        if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            module_state       = CMDPARSER_STATE_PROCESSING;
            new_data_available = false;

            CmdParser_ErrorType_t parse_result = CMDPARSER_ERROR_NONE;

            if (message_complete && (bytes_received == active_frame_len))
            {
                /* Route to the correct parser based on detected sensor type */
                if (s_detected_sensor_type == SENSOR_5PARAM)
                    parse_result = ParseMessage_ZPHS01C();
                else
                    parse_result = ParseMessage();

                if (parse_result == CMDPARSER_ERROR_NONE)
                {
                    module_state         = CMDPARSER_STATE_DATA_READY;
                    new_data_available   = true;
                    last_valid_data_time = current_time;
                    total_messages_received++;
                    consecutive_errors   = 0U;
                    UpdateErrorStatus(CMDPARSER_ERROR_NONE);

                    /* Save raw frame for debug access (FIX-3: only here) */
                    memcpy(last_complete_buffer, rx_buffer, active_frame_len);
                    last_complete_length = active_frame_len;
                    raw_buffer_available = true;
                }
                else
                {
                    module_state = CMDPARSER_STATE_ERROR;
                    total_errors_detected++;
                    consecutive_errors++;
                    UpdateErrorStatus(parse_result);

#if CMDPARSER_DEBUG_ENABLE
                    Serial.printf("[CMDPARSER] Parse error: %d (consecutive: %lu)\n",
                                  (int)parse_result,
                                  (unsigned long)consecutive_errors);
#endif
                }
            }
            else
            {
                /* Timeout or wrong byte count */
                module_state = CMDPARSER_STATE_ERROR;
                total_errors_detected++;
                consecutive_errors++;

                if (bytes_received == 0U)
                {
                    UpdateErrorStatus(CMDPARSER_ERROR_INSUFFICIENT_BYTES);
                }
                else
                {
                    UpdateErrorStatus(CMDPARSER_ERROR_INVALID_DATA);
#if CMDPARSER_DEBUG_ENABLE
                    Serial.printf("[CMDPARSER] Incomplete frame: got %u bytes, expected %u (sensor=%s)\n",
                                  (unsigned)bytes_received,
                                  (unsigned)active_frame_len,
                                  (s_detected_sensor_type == SENSOR_5PARAM) ? "ZPHS01C(14)" : "ZPHS01B(26)");
#endif
                }
            }

            ResetMessageState();
            xSemaphoreGive(data_mutex);

            /* Serial logging OUTSIDE mutex — safe.
             * WARNING: Keep CMDPARSER_DEBUG_ENABLE 0 in production.
             *          These prints (~1 Hz on Core 0) cause Serial contention
             *          with Core 1 (LEDHMI, Modbus) → Modbus CRC FAIL. */
#if CMDPARSER_DEBUG_ENABLE
            if (new_data_available)
            {
                if (s_detected_sensor_type == SENSOR_5PARAM)
                {
                    Serial.printf("[CMDPARSER] ZPHS01C #%lu — CO2=%u VOC=%u T=%.1f H=%u PM25=%u\n",
                                  (unsigned long)s_frame_counter_5param,
                                  (unsigned)current_aq_data.co2,
                                  (unsigned)current_aq_data.voc,
                                  current_aq_data.temperature,
                                  (unsigned)current_aq_data.humidity,
                                  (unsigned)current_aq_data.pm2_5);
                }
                else
                {
                    Serial.printf("[CMDPARSER] ZPHS01B #%lu — PM25=%u CO2=%u T=%.1f H=%u\n",
                                  (unsigned long)s_frame_counter_9param,
                                  (unsigned)current_aq_data.pm2_5,
                                  (unsigned)current_aq_data.co2,
                                  current_aq_data.temperature,
                                  (unsigned)current_aq_data.humidity);
                }
            }
#endif
        }
    }
}

/*==============================================================================
 *                          DATA ACCESS APIs
 *============================================================================*/

CmdParser_StatusType_t CmdParser__GetAirQualityData(CmdParser_ZPHS01B_Data_t *data)
{
    CmdParser_StatusType_t status;

    if (!IsValidPointer(data))
        return CMDPARSER_STATUS_INVALID_PARAM;

    if (!module_initialized)
        return CMDPARSER_STATUS_NOT_INITIALIZED;

    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        if (!IsDataFresh())
            status = CMDPARSER_STATUS_DATA_STALE;
        else if (!current_aq_data.valid)
            status = CMDPARSER_STATUS_NO_DATA;
        else
        {
            *data  = current_aq_data;
            status = CMDPARSER_STATUS_OK;
        }
        xSemaphoreGive(data_mutex);
    }
    else
    {
        status = CMDPARSER_STATUS_BUSY;
    }

    return status;
}

CmdParser_StatusType_t CmdParser__GetRawBuffer(uint8_t  *buffer,
                                                uint16_t *buffer_length,
                                                uint16_t  max_length)
{
    if (!IsValidPointer(buffer) || !IsValidPointer(buffer_length))
        return CMDPARSER_STATUS_INVALID_PARAM;

    if (!module_initialized)
        return CMDPARSER_STATUS_NOT_INITIALIZED;

    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        if (!raw_buffer_available || (last_complete_length == 0U))
        {
            xSemaphoreGive(data_mutex);
            return CMDPARSER_STATUS_NO_DATA;
        }
        uint16_t copy_len = (last_complete_length < max_length)
                                ? last_complete_length : max_length;
        memcpy(buffer, last_complete_buffer, copy_len);
        *buffer_length = copy_len;
        xSemaphoreGive(data_mutex);
        return CMDPARSER_STATUS_OK;
    }

    return CMDPARSER_STATUS_BUSY;
}

/*==============================================================================
 *                          ERROR AND STATUS APIs
 *============================================================================*/

CmdParser_StatusType_t CmdParser__GetErrorStatus(CmdParser_ErrorType_t *error_type)
{
    if (!IsValidPointer(error_type))
        return CMDPARSER_STATUS_INVALID_PARAM;

    if (!module_initialized)
        return CMDPARSER_STATUS_NOT_INITIALIZED;

    *error_type = current_error;
    return CMDPARSER_STATUS_OK;
}

CmdParser_StatusType_t CmdParser__ClearErrorStatus(void)
{
    if (!module_initialized)
        return CMDPARSER_STATUS_NOT_INITIALIZED;

    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        current_error      = CMDPARSER_ERROR_NONE;
        consecutive_errors = 0U;
        xSemaphoreGive(data_mutex);
    }

    return CMDPARSER_STATUS_OK;
}

bool CmdParser__IsInitialized(void)
{
    return module_initialized;
}

bool CmdParser__IsDataFresh(void)
{
    bool fresh = false;

    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        fresh = IsDataFresh() && new_data_available;
        xSemaphoreGive(data_mutex);
    }

    return fresh;
}

/*==============================================================================
 *                          UTILITY APIs
 *============================================================================*/

CmdParser_StatusType_t CmdParser__Reset(void)
{
    if (!module_initialized)
        return CMDPARSER_STATUS_NOT_INITIALIZED;

    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        module_state  = CMDPARSER_STATE_INITIALIZED;
        current_error = CMDPARSER_ERROR_NONE;

        ResetMessageState();

        while (UART_PORT.available() > 0) { (void)UART_PORT.read(); }

        memset(&current_aq_data,     0, sizeof(current_aq_data));
        memset(last_complete_buffer, 0, sizeof(last_complete_buffer));

        last_complete_length = 0U;
        raw_buffer_available = false;
        consecutive_errors   = 0U;
        last_valid_data_time = 0U;
        new_data_available   = false;

        xSemaphoreGive(data_mutex);
    }

    return CMDPARSER_STATUS_OK;
}

CmdParser_StatusType_t CmdParser__GetTotalMessages(uint32_t *count)
{
    if (!IsValidPointer(count))
        return CMDPARSER_STATUS_INVALID_PARAM;

    if (!module_initialized)
        return CMDPARSER_STATUS_NOT_INITIALIZED;

    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        *count = total_messages_received;
        xSemaphoreGive(data_mutex);
        return CMDPARSER_STATUS_OK;
    }

    return CMDPARSER_STATUS_BUSY;
}

/*==============================================================================
 *                          SENSOR TYPE APIs
 *============================================================================*/

uint32_t CmdParser__GetSensorType(void)
{
    return s_detected_sensor_type;
}

bool CmdParser__IsSensorDetected(void)
{
    return s_sensor_detected;
}

void CmdParser__ResetSensorType(void)
{
    taskENTER_CRITICAL(&critical_mux);
    s_detected_sensor_type = SENSOR_TYPE_DEFAULT;
    s_sensor_detected      = false;
    taskEXIT_CRITICAL(&critical_mux);

    s_prefs.begin(NVS_NAMESPACE, false);
    s_prefs.remove(NVS_KEY_SENSOR);
    s_prefs.end();

#if CMDPARSER_DEBUG_ENABLE
    Serial.println("[CMDPARSER] Sensor type reset — will auto-detect from next frame");
#endif
}

/*==============================================================================
 *                     STATIC FUNCTION IMPLEMENTATIONS
 *============================================================================*/

static bool IsValidPointer(const void *ptr)
{
    return (ptr != NULL);
}

static void ResetMessageState(void)
{
    taskENTER_CRITICAL(&critical_mux);
    bytes_received     = 0U;
    message_complete   = false;
    message_start_time = 0U;
    last_char_time     = 0U;
    memset(rx_buffer, 0, sizeof(rx_buffer));
    taskEXIT_CRITICAL(&critical_mux);
}

static void UpdateErrorStatus(CmdParser_ErrorType_t error_type)
{
    current_error = error_type;
}

static bool IsDataFresh(void)
{
    uint32_t current_time = millis();
    uint32_t elapsed;

    if (last_valid_data_time == 0U)
        return false;

    if (current_time >= last_valid_data_time)
        elapsed = current_time - last_valid_data_time;
    else
        elapsed = (0xFFFFFFFFU - last_valid_data_time) + current_time + 1U;

    return (elapsed < DATA_FRESHNESS_TIMEOUT_MS);
}

static bool VerifyChecksum(const uint8_t *frame, uint16_t length)
{
    uint8_t  sum = 0U;
    uint16_t i;

    if (!IsValidPointer(frame) || (length != ZPHS01B_FRAME_LENGTH))
        return false;

    for (i = 1U; i <= 24U; i++)
        sum += frame[i];

    uint8_t expected_checksum = (~sum + 1U) & 0xFFU;

    return (expected_checksum == frame[ZPHS01B_FRAME_LENGTH - 1U]);
}

// /**
//  * @file CmdParser.cpp
//  * @brief ZPHS01B Air Quality Sensor Command Parser Implementation
//  * @details Parses binary UART frames from Winsen ZPHS01B multi-in-one air
//  *          quality sensor. Extracts 11 air quality parameters from the
//  *          26-byte fixed-length response frame.
//  *
//  * @version 2.0.0 - Adapted from Sansui weighing scale to ZPHS01B sensor
//  * @date 2025
//  *
//  * FRAME FORMAT (26 bytes):
//  *   [0]     0xFF         Start byte
//  *   [1]     0x86         Command echo
//  *   [2-3]   PM1.0        ug/m3  big-endian uint16
//  *   [4-5]   PM2.5        ug/m3  big-endian uint16
//  *   [6-7]   PM10         ug/m3  big-endian uint16
//  *   [8-9]   CO2          ppm    big-endian uint16
//  *   [10]    VOC          grade 0-3
//  *   [11-12] Temperature  raw = (C*10)+500, big-endian uint16
//  *   [13-14] Humidity     %RH    big-endian uint16
//  *   [15-16] CH2O         x0.001 mg/m3, big-endian uint16
//  *   [17-18] CO           x0.1 ppm,     big-endian uint16
//  *   [19-20] O3           x0.01 ppm,    big-endian uint16
//  *   [21-22] NO2          x0.01 ppm,    big-endian uint16
//  *   [23-24] 0x0000       Reserved
//  *   [25]    Checksum     (~sum(byte1..byte24) + 1) & 0xFF
//  *
//  * WHAT CHANGED FROM SANSUI VERSION:
//  *   - ParseMessage()          : Binary byte extraction replaces CSV field splitting
//  *   - Data struct             : CmdParser_ZPHS01B_Data_t replaces weight/timestamp structs
//  *   - Frame detection         : Fixed 26-byte length + 0xFF start byte alignment
//  *   - Checksum verification   : Binary checksum replaces no-checksum Sansui protocol
//  *   - Removed functions       : ParseTimestamp, ParseWeight, ValidateWeightCalculation,
//  *                               SplitFields, ConvertTimestampToEpoch
//  *
//  * WHAT IS UNCHANGED:
//  *   - CmdParser__Init()       : UART init, mutex, buffer setup
//  *   - CmdParser__Handler()    : UART read loop, state machine, timeout handling
//  *   - All utility APIs        : IsInitialized, IsDataFresh, Reset, GetRawBuffer
//  *   - FreeRTOS integration    : Mutex, critical sections
//  *   - Error handling          : UpdateErrorStatus, consecutive error tracking
//  *   - Statistics              : total_messages_received, total_errors_detected
//  */

// /**
//  * @file CmdParser.cpp
//  * @brief ZPHS01B Air Quality Sensor Command Parser Implementation
//  * @details Parses binary UART frames from Winsen ZPHS01B multi-in-one air
//  *          quality sensor. Extracts 11 air quality parameters from the
//  *          26-byte fixed-length response frame.
//  *
//  * @version 2.0.0 - Adapted from Sansui weighing scale to ZPHS01B sensor
//  * @date 2025
//  *
//  * FRAME FORMAT (26 bytes):
//  *   [0]     0xFF         Start byte
//  *   [1]     0x86         Command echo
//  *   [2-3]   PM1.0        ug/m3  big-endian uint16
//  *   [4-5]   PM2.5        ug/m3  big-endian uint16
//  *   [6-7]   PM10         ug/m3  big-endian uint16
//  *   [8-9]   CO2          ppm    big-endian uint16
//  *   [10]    VOC          grade 0-3
//  *   [11-12] Temperature  raw = (C*10)+500, big-endian uint16
//  *   [13-14] Humidity     %RH    big-endian uint16
//  *   [15-16] CH2O         x0.001 mg/m3, big-endian uint16
//  *   [17-18] CO           x0.1 ppm,     big-endian uint16
//  *   [19-20] O3           x0.01 ppm,    big-endian uint16
//  *   [21-22] NO2          x0.01 ppm,    big-endian uint16
//  *   [23-24] 0x0000       Reserved
//  *   [25]    Checksum     (~sum(byte1..byte24) + 1) & 0xFF
//  *
//  * WHAT CHANGED FROM SANSUI VERSION:
//  *   - ParseMessage()          : Binary byte extraction replaces CSV field splitting
//  *   - Data struct             : CmdParser_ZPHS01B_Data_t replaces weight/timestamp structs
//  *   - Frame detection         : Fixed 26-byte length + 0xFF start byte alignment
//  *   - Checksum verification   : Binary checksum replaces no-checksum Sansui protocol
//  *   - Removed functions       : ParseTimestamp, ParseWeight, ValidateWeightCalculation,
//  *                               SplitFields, ConvertTimestampToEpoch
//  *
//  * WHAT IS UNCHANGED:
//  *   - CmdParser__Init()       : UART init, mutex, buffer setup
//  *   - CmdParser__Handler()    : UART read loop, state machine, timeout handling
//  *   - All utility APIs        : IsInitialized, IsDataFresh, Reset, GetRawBuffer
//  *   - FreeRTOS integration    : Mutex, critical sections
//  *   - Error handling          : UpdateErrorStatus, consecutive error tracking
//  *   - Statistics              : total_messages_received, total_errors_detected
//  */

// /**
//  * @file CmdParser.cpp
//  * @brief Air Quality Sensor Command Parser Implementation
//  * @details Supports both Winsen ZPHS01B (9-parameter, 26-byte frame) and
//  *          ZPHS01C (5-parameter, 14-byte frame) sensors on the same UART.
//  *          Sensor type is auto-detected from the start byte of the first
//  *          received frame and persisted to NVS so the correct frame length
//  *          is used immediately on every subsequent boot.
//  *
//  * @version 2.1.0 - Added ZPHS01C dual-sensor support + NVS persistence
//  * @date 2025
//  */

// /*==============================================================================
//  *                              INCLUDES
//  *============================================================================*/
// #include "CmdParser.h"
// #include <Arduino.h>
// #include <HardwareSerial.h>
// #include <freertos/FreeRTOS.h>
// #include <freertos/task.h>
// #include <freertos/semphr.h>
// #include <string.h>
// #include <stdlib.h>
// #include "SensorConfig.h"
// #include <Preferences.h>

// /*==============================================================================
//  *                              DEFINES
//  *============================================================================*/

// /** @brief Set to 1 to enable verbose sensor frame logging.
//  *  KEEP 0 IN PRODUCTION — these Serial.printf calls run on Core 0
//  *  every sensor frame (~1Hz) and cause Serial contention with Core 1
//  *  (LEDHMI, Modbus), corrupting Modbus frames → CRC FAIL. */
// #define CMDPARSER_DEBUG_ENABLE      0

// /** @brief ZPHS01C frame constants */
// #define ZPHS01C_FRAME_LENGTH        14U
// #define ZPHS01C_START_BYTE          0x16U
// #define ZPHS01C_CMD_BYTE            0x01U
// #define ZPHS01C_TEMP_OFFSET         500U

// /**
//  * @brief ZPHS01C active-upload mode command (host -> sensor)
//  * @details Per datasheet: "11 02 01 00 EC"
//  *          Must be sent before the sensor will stream any frames.
//  *          After a valid response the module uploads data every second
//  *          automatically until power-off — no need to repeat.
//  */
// static const uint8_t ZPHS01C_ACTIVE_UPLOAD_CMD[]  = {0x11U, 0x02U, 0x01U, 0x00U, 0xECU};
// #define ZPHS01C_ACTIVE_UPLOAD_CMD_LEN   5U


// /**
//  * @brief Humidity raw-to-%RH divisor
//  * @details Datasheet: raw range 0-1000 maps to 0-100 %RH (multiplier = 10)
//  *          So stored %RH = raw / 10.
//  */
// #define ZPHS01C_HUMIDITY_DIVISOR        10U

// /** @brief Max time to wait for complete frame after first byte */
// #define MESSAGE_TIMEOUT_MS          250U

// /** @brief Max age of data before considered stale (5 seconds) */
// #define DATA_FRESHNESS_TIMEOUT_MS   5000U

// /*==============================================================================
//  *                              TYPEDEFS
//  *============================================================================*/

// typedef enum
// {
//     CMDPARSER_STATE_UNINITIALIZED = 0U,
//     CMDPARSER_STATE_INITIALIZED   = 1U,
//     CMDPARSER_STATE_RECEIVING     = 2U,
//     CMDPARSER_STATE_PROCESSING    = 3U,
//     CMDPARSER_STATE_DATA_READY    = 4U,
//     CMDPARSER_STATE_ERROR         = 5U
// } CmdParser_InternalState_t;

// /*==============================================================================
//  *                              UART PORT SELECTION
//  *============================================================================*/

// #if (CMDPARSER_UART_CHANNEL == 0U)
// #define UART_PORT Serial
// #elif (CMDPARSER_UART_CHANNEL == 1U)
// #define UART_PORT Serial1
// #elif (CMDPARSER_UART_CHANNEL == 2U)
// #define UART_PORT Serial2
// #else
// #error "Invalid CMDPARSER_UART_CHANNEL. Must be 0, 1, or 2"
// #endif

// /*==============================================================================
//  *                          STATIC VARIABLES
//  *============================================================================*/

// /* Module state */
// static volatile CmdParser_InternalState_t module_state      = CMDPARSER_STATE_UNINITIALIZED;

// /* Sensor type detection */
// static uint32_t s_detected_sensor_type = SENSOR_TYPE_DEFAULT;
// static bool     s_sensor_detected      = false;
// static volatile bool                   module_initialized   = false;
// static volatile CmdParser_ErrorType_t  current_error        = CMDPARSER_ERROR_NONE;

// /* UART reception */
// static uint8_t           rx_buffer[ZPHS01B_FRAME_LENGTH];
// static uint8_t           last_complete_buffer[ZPHS01B_FRAME_LENGTH];
// static volatile uint16_t bytes_received      = 0U;
// static uint16_t          last_complete_length = 0U;
// static volatile uint32_t message_start_time  = 0U;
// static volatile uint32_t last_char_time      = 0U;
// static volatile bool     message_complete    = false;
// static volatile bool     new_data_available  = false;
// static volatile bool     raw_buffer_available = false;

// /* Debug frame counters */
// static uint32_t s_frame_counter_9param = 0U;
// static uint32_t s_frame_counter_5param = 0U;

// /* Parsed air quality data */
// static CmdParser_ZPHS01B_Data_t current_aq_data = {0U, 0U, 0U, 0U, 0U,
//                                                     0.0f, 0U, 0.0f, 0.0f,
//                                                     0.0f, 0.0f, false, 0U};

// /* Freshness tracking */
// static volatile uint32_t last_valid_data_time = 0U;

// /* Statistics */
// static uint32_t total_messages_received = 0U;
// static uint32_t total_errors_detected   = 0U;
// static uint32_t consecutive_errors      = 0U;

// /* NVS persistence for sensor type */
// static Preferences s_prefs;
// #define NVS_NAMESPACE  SENSOR_NVS_NAMESPACE
// #define NVS_KEY_SENSOR SENSOR_NVS_KEY_TYPE

// /* FreeRTOS synchronization */
// static SemaphoreHandle_t data_mutex   = NULL;
// static portMUX_TYPE      critical_mux = portMUX_INITIALIZER_UNLOCKED;

// /*==============================================================================
//  *                     STATIC FUNCTION PROTOTYPES
//  *============================================================================*/
// static bool                  IsValidPointer(const void *ptr);
// static void                  ResetMessageState(void);
// static void                  UpdateErrorStatus(CmdParser_ErrorType_t error_type);
// static bool                  IsDataFresh(void);
// static bool                  VerifyChecksum(const uint8_t *frame, uint16_t length);
// static CmdParser_ErrorType_t ParseMessage(void);
// static CmdParser_ErrorType_t ParseMessage_ZPHS01C(void);
// static void                  CmdParser_DetectAndHandleSensorType(uint8_t start_byte);
// static void                  CmdParser_ZPHS01C_SendStartupCommands(void);

// /*==============================================================================
//  * PRIVATE — ZPHS01C Frame Parser (5-parameter, 14 bytes)
//  * Frame: 16 0B 01 CO2(2) VOC/CH2O(2) Hum(2) Temp(2) PM2.5(2) CS
//  *============================================================================*/
// static CmdParser_ErrorType_t ParseMessage_ZPHS01C(void)
// {
//     uint16_t raw_u16;

//     /* 1. Validate frame length */
//     if (bytes_received != ZPHS01C_FRAME_LENGTH)
//         return CMDPARSER_ERROR_INSUFFICIENT_BYTES;

//     /* 2. Validate start and command bytes */
//     if ((rx_buffer[0] != ZPHS01C_START_BYTE) || (rx_buffer[2] != ZPHS01C_CMD_BYTE))
//         return CMDPARSER_ERROR_INVALID_DATA;

//     /* 3. Verify checksum: ~(sum of byte0..byte12) + 1 */
//     uint8_t sum = 0U;
//     for (uint8_t i = 0U; i < (ZPHS01C_FRAME_LENGTH - 1U); i++)
//         sum += rx_buffer[i];
//     uint8_t expected_cs = (~sum) + 1U;
//     if (expected_cs != rx_buffer[ZPHS01C_FRAME_LENGTH - 1U])
//     {
//         // Serial.printf("[CMDPARSER] ZPHS01C Checksum FAIL — got 0x%02X expected 0x%02X\n",
//         //               rx_buffer[ZPHS01C_FRAME_LENGTH - 1U], expected_cs);
//         return CMDPARSER_ERROR_CHECKSUM_FAIL;
//     }

//     /* 4. Parse parameters */

//     /* CO2 — Byte 3-4 */
//     raw_u16 = ((uint16_t)rx_buffer[3] << 8U) | (uint16_t)rx_buffer[4];
//     current_aq_data.co2 = raw_u16;

//     /* VOC/CH2O — Byte 5-6 (treat as VOC grade 0-3) */
//     raw_u16 = ((uint16_t)rx_buffer[5] << 8U) | (uint16_t)rx_buffer[6];
//     current_aq_data.voc  = (uint8_t)(raw_u16 & 0x03U);
//     current_aq_data.ch2o = 0.0f;

//     /* Humidity — Byte 7-8
//      * Stored raw (0-1000) to match ZPHS01B convention.
//      * Display layer divides by 10 to get actual %RH for both sensors. */
//     raw_u16 = ((uint16_t)rx_buffer[7] << 8U) | (uint16_t)rx_buffer[8];
//     current_aq_data.humidity = raw_u16 / 10U; 

//     /* Temperature — Byte 9-10 — (raw - 500) / 10 */
//     raw_u16 = ((uint16_t)rx_buffer[9] << 8U) | (uint16_t)rx_buffer[10];
//     current_aq_data.temperature = ((float)raw_u16 - (float)ZPHS01C_TEMP_OFFSET) * 0.1f;

//     /* PM2.5 — Byte 11-12 */
//     raw_u16 = ((uint16_t)rx_buffer[11] << 8U) | (uint16_t)rx_buffer[12];
//     current_aq_data.pm2_5 = raw_u16;

//     /* Parameters not available on ZPHS01C — set to 0 */
//     current_aq_data.pm1_0 = 0U;
//     current_aq_data.pm10  = 0U;
//     current_aq_data.co    = 0.0f;
//     current_aq_data.o3    = 0.0f;
//     current_aq_data.no2   = 0.0f;

//     /* 5. Debug frame counter */
//     s_frame_counter_5param++;

//     /* 6. Mark valid */
//     current_aq_data.valid        = true;
//     current_aq_data.timestamp_ms = millis();

//     return CMDPARSER_ERROR_NONE;
// }

// /*==============================================================================
//  * PRIVATE — ZPHS01C Startup Command Sequence
//  *
//  * Per datasheet the ZPHS01C does NOT auto-stream on power-up.
//  * The active-upload enable command must be sent first:
//  *   11 02 01 00 EC
//  * After a valid response the sensor streams frames every second automatically.
//  * The dust (PM2.5) sensor runs continuously by default — no extra command needed.
//  *
//  * Called from CmdParser__Init() when sensor type is already known from NVS,
//  * and from CmdParser_DetectAndHandleSensorType() on first live detection.
//  *============================================================================*/
// static void CmdParser_ZPHS01C_SendStartupCommands(void)
// {
//     /* Enable active upload mode */
//     UART_PORT.write(ZPHS01C_ACTIVE_UPLOAD_CMD, ZPHS01C_ACTIVE_UPLOAD_CMD_LEN);
//     Serial.println("[CMDPARSER] ZPHS01C: active-upload command sent (11 02 01 00 EC)");
//     Serial.println("[CMDPARSER] ZPHS01C: startup complete — sensor will stream in ~1s");
// }

// /*==============================================================================
//  * PRIVATE — Sensor type detection and NVS persistence
//  * Re-detects on every call. NVS written only when type actually changes.
//  * Accepts either known start byte so detection works on the very first frame.
//  *============================================================================*/
// static void CmdParser_DetectAndHandleSensorType(uint8_t start_byte)
// {
//     uint32_t new_type = SENSOR_TYPE_DEFAULT;

//     if      (start_byte == SENSOR_9PARAM_START_BYTE) { new_type = SENSOR_9PARAM; }
//     else if (start_byte == SENSOR_5PARAM_START_BYTE) { new_type = SENSOR_5PARAM; }
//     else    { return; }   /* unknown start byte — ignore */

//     /* Always mark as detected from a live frame */
//     s_sensor_detected = true;

//     /* Only update + persist when the type has actually changed */
//     if (new_type != s_detected_sensor_type)
//     {
//         s_detected_sensor_type = new_type;

//         /* Persist to NVS so the correct frame length is used on next boot */
//         s_prefs.begin(NVS_NAMESPACE, false);
//         s_prefs.putUInt(NVS_KEY_SENSOR, new_type);
//         s_prefs.end();

//         // Serial.printf("[CMDPARSER] Sensor type CHANGED -> %s — saved to NVS\n",
//         //               (new_type == SENSOR_5PARAM) ? "ZPHS01C (5-param)" : "ZPHS01B (9-param)");

//         /* If newly detected as ZPHS01C, send startup commands now.
//          * This covers the case where NVS had no saved type and the sensor
//          * is identified for the first time from a live start byte. */
//         if (new_type == SENSOR_5PARAM)
//         {
//             CmdParser_ZPHS01C_SendStartupCommands();
//         }
//     }
//     else
//     {
//         // Serial.printf("[CMDPARSER] Sensor confirmed: %s\n",
//         //               (new_type == SENSOR_5PARAM) ? "ZPHS01C" : "ZPHS01B");
//     }
// }

// /*==============================================================================
//  *                     PUBLIC FUNCTION IMPLEMENTATIONS
//  *============================================================================*/

// /**
//  * @brief Initialize the Command Parser module
//  */
// void CmdParser__Init(void)
// {
//     /* Create FreeRTOS mutex */
//     if (data_mutex == NULL)
//     {
//         data_mutex = xSemaphoreCreateMutex();
//     }

//     /* Initialize UART */
//     UART_PORT.setRxBufferSize(CMDPARSER_RX_BUFFER_SIZE);
//     UART_PORT.begin(CMDPARSER_UART_BAUDRATE,
//                     SERIAL_8N1,
//                     CMDPARSER_UART_RX_PIN,
//                     CMDPARSER_UART_TX_PIN);

//     // Serial.printf("[CMDPARSER] ZPHS01B UART init: channel=%u baud=%lu rx=%d tx=%d\n",
//     //               (unsigned)CMDPARSER_UART_CHANNEL,
//     //               (unsigned long)CMDPARSER_UART_BAUDRATE,
//     //               (int)CMDPARSER_UART_RX_PIN,
//     //               (int)CMDPARSER_UART_TX_PIN);

//     /* Initialize internal state under mutex */
//     if (xSemaphoreTake(data_mutex, portMAX_DELAY) == pdTRUE)
//     {
//         module_state       = CMDPARSER_STATE_INITIALIZED;
//         module_initialized = true;
//         current_error      = CMDPARSER_ERROR_NONE;

//         ResetMessageState();

//         memset(&current_aq_data,     0, sizeof(current_aq_data));
//         memset(last_complete_buffer, 0, sizeof(last_complete_buffer));

//         last_complete_length    = 0U;
//         raw_buffer_available    = false;
//         total_messages_received = 0U;
//         total_errors_detected   = 0U;
//         consecutive_errors      = 0U;
//         last_valid_data_time    = 0U;
//         new_data_available      = false;

//         xSemaphoreGive(data_mutex);
//     }

//     /* Load persisted sensor type from NVS so the correct frame length is
//      * used immediately on boot — no re-detection needed after a power cycle.
//      * Use 0xFFFFFFFF as sentinel so an empty NVS (after flash erase) is
//      * correctly treated as "not set" rather than defaulting to ZPHS01B. */
//     s_prefs.begin(NVS_NAMESPACE, true);
//     uint32_t saved = s_prefs.getUInt(NVS_KEY_SENSOR, 0xFFFFFFFFU);
//     s_prefs.end();

//     if ((saved == SENSOR_9PARAM) || (saved == SENSOR_5PARAM))
//     {
//         s_detected_sensor_type = saved;
//         s_sensor_detected      = true;
//         // Serial.printf("[CMDPARSER] NVS: sensor type loaded = %s\n",
//         //               (saved == SENSOR_5PARAM) ? "ZPHS01C (5-param)" : "ZPHS01B (9-param)");

//         /* ZPHS01C does not auto-stream on power-up — send startup commands
//          * every boot so the sensor begins transmitting immediately. */
//         if (saved == SENSOR_5PARAM)
//         {
//             CmdParser_ZPHS01C_SendStartupCommands();
//         }
//     }
//     else
//     {
//         s_detected_sensor_type = SENSOR_TYPE_DEFAULT;
//         s_sensor_detected      = false;
//         // Serial.println("[CMDPARSER] NVS: no saved type — will auto-detect from first frame");
//     }

//     // Serial.printf("[CMDPARSER] Parser ready — expecting %u-byte frames\n",
//     //               (s_detected_sensor_type == SENSOR_5PARAM) ?
//     //               ZPHS01C_FRAME_LENGTH : ZPHS01B_FRAME_LENGTH);
// }

// /*==============================================================================
//  *                          MAIN HANDLER
//  *============================================================================*/
// void CmdParser__Handler(void)
// {
//     uint32_t current_time;
//     int      available_bytes;
//     bool     timeout_detected = false;
//     bool     should_process   = false;

//     if (!module_initialized)
//     {
//         return;
//     }

//     current_time    = millis();
//     available_bytes = UART_PORT.available();

//     /*
//      * Compute active frame length and start byte from currently known sensor type.
//      * These are recomputed immediately after byte-0 detection inside the loop
//      * to fix the chicken-and-egg problem on the very first frame.
//      */
//     uint8_t active_frame_len  = (s_detected_sensor_type == SENSOR_5PARAM) ?
//                                  ZPHS01C_FRAME_LENGTH : ZPHS01B_FRAME_LENGTH;
//     uint8_t active_start_byte = (s_detected_sensor_type == SENSOR_5PARAM) ?
//                                  ZPHS01C_START_BYTE : ZPHS01B_START_BYTE;

//     if (available_bytes > 0)
//     {
//         taskENTER_CRITICAL(&critical_mux);

//         /* Starting new frame reception */
//         if (bytes_received == 0U)
//         {
//             module_state         = CMDPARSER_STATE_RECEIVING;
//             message_start_time   = current_time;
//             memset(last_complete_buffer, 0, sizeof(last_complete_buffer));
//             last_complete_length = 0U;
//             raw_buffer_available = false;
//         }

//         /* Read bytes into buffer */
//         while (UART_PORT.available() > 0)
//         {
//             int received_byte = UART_PORT.read();
//             if (received_byte < 0) { continue; }

//             /* ── Byte 0: synchronise to start byte + detect sensor type ── */
//             if (bytes_received == 0U)
//             {
//                 uint8_t b = (uint8_t)received_byte;

//                 /*
//                  * Accept either known start byte so detection works on the
//                  * very first frame even before s_detected_sensor_type is set.
//                  * Discard any other byte silently.
//                  */
//                 if ((b != ZPHS01B_START_BYTE) && (b != ZPHS01C_START_BYTE))
//                 {
//                     continue;   /* not a valid start byte — discard */
//                 }

//                 /* Identify the sensor from this start byte */
//                 CmdParser_DetectAndHandleSensorType(b);

//                 /*
//                  * Recompute frame length RIGHT NOW after detection so the
//                  * rest of this frame is collected to the correct size.
//                  * Without this recompute, active_frame_len stays at 26 even
//                  * after a ZPHS01C (14-byte) sensor is detected.
//                  */
//                 active_frame_len  = (s_detected_sensor_type == SENSOR_5PARAM) ?
//                                      ZPHS01C_FRAME_LENGTH : ZPHS01B_FRAME_LENGTH;
//                 active_start_byte = (s_detected_sensor_type == SENSOR_5PARAM) ?
//                                      ZPHS01C_START_BYTE : ZPHS01B_START_BYTE;
//             }

//             /* Fill buffer up to the correct frame length */
//             if (bytes_received < active_frame_len)
//             {
//                 rx_buffer[bytes_received] = (uint8_t)received_byte;
//                 bytes_received++;
//                 last_char_time = current_time;
//             }

//             /* Stop as soon as the complete frame is in the buffer */
//             if (bytes_received >= active_frame_len)
//             {
//                 message_complete = true;
//                 break;
//             }
//         }

//         taskEXIT_CRITICAL(&critical_mux);
//     }

//     /* Detect inter-character timeout */
//     if ((bytes_received > 0U) && ((current_time - message_start_time) > MESSAGE_TIMEOUT_MS))
//     {
//         timeout_detected = true;
//     }

//     taskENTER_CRITICAL(&critical_mux);
//     should_process = (message_complete || timeout_detected);
//     taskEXIT_CRITICAL(&critical_mux);

//     if (should_process)
//     {
//         if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
//         {
//             module_state       = CMDPARSER_STATE_PROCESSING;
//             new_data_available = false;

//             CmdParser_ErrorType_t parse_result = CMDPARSER_ERROR_NONE;

//             if (message_complete && (bytes_received == active_frame_len))
//             {
//                 /* Route to the correct parser based on detected sensor type */
//                 if (s_detected_sensor_type == SENSOR_5PARAM)
//                     parse_result = ParseMessage_ZPHS01C();
//                 else
//                     parse_result = ParseMessage();

//                 if (parse_result == CMDPARSER_ERROR_NONE)
//                 {
//                     module_state            = CMDPARSER_STATE_DATA_READY;
//                     new_data_available      = true;
//                     last_valid_data_time    = current_time;
//                     total_messages_received++;
//                     consecutive_errors      = 0U;
//                     UpdateErrorStatus(CMDPARSER_ERROR_NONE);

//                     /* Save raw frame for debug access */
//                     memcpy(last_complete_buffer, rx_buffer, active_frame_len);
//                     last_complete_length = active_frame_len;
//                     raw_buffer_available = true;
//                 }
//                 else
//                 {
//                     module_state = CMDPARSER_STATE_ERROR;
//                     total_errors_detected++;
//                     consecutive_errors++;
//                     UpdateErrorStatus(parse_result);

//                     // Serial.printf("[CMDPARSER] Parse error: %d (consecutive: %lu)\n",
//                     //               (int)parse_result,
//                     //               (unsigned long)consecutive_errors);
//                 }
//             }
//             else
//             {
//                 /* Timeout or wrong byte count */
//                 module_state = CMDPARSER_STATE_ERROR;
//                 total_errors_detected++;
//                 consecutive_errors++;

//                 if (bytes_received == 0U)
//                 {
//                     UpdateErrorStatus(CMDPARSER_ERROR_INSUFFICIENT_BYTES);
//                 }
//                 else
//                 {
//                     UpdateErrorStatus(CMDPARSER_ERROR_INVALID_DATA);
// #if CMDPARSER_DEBUG_ENABLE
//                     Serial.printf("[CMDPARSER] Incomplete frame: got %u bytes, expected %u (sensor=%s)\n",
//                                   (unsigned)bytes_received,
//                                   (unsigned)active_frame_len,
//                                   (s_detected_sensor_type == SENSOR_5PARAM) ? "ZPHS01C(14)" : "ZPHS01B(26)");
// #endif
//                 }
//             }

//             ResetMessageState();
//             xSemaphoreGive(data_mutex);

//             /* Serial logging OUTSIDE mutex — safe */
//             /* NOTE: Keep CMDPARSER_DEBUG_ENABLE 0 in production.
//              *       These prints run on Core 0 every sensor frame (~1Hz).
//              *       If Core 1 prints at the same time (LEDHMI, Modbus),
//              *       Serial contention causes a CPU stall during UART RX
//              *       which corrupts one byte of the Modbus frame → CRC FAIL. */
// #if CMDPARSER_DEBUG_ENABLE
//             if (new_data_available)
//             {
//                 if (s_detected_sensor_type == SENSOR_5PARAM)
//                 {
//                     Serial.printf("[CMDPARSER] ZPHS01C #%lu — CO2=%u VOC=%u T=%.1f H=%u PM25=%u\n",
//                                   (unsigned long)s_frame_counter_5param,
//                                   (unsigned)current_aq_data.co2,
//                                   (unsigned)current_aq_data.voc,
//                                   current_aq_data.temperature,
//                                   (unsigned)current_aq_data.humidity,
//                                   (unsigned)current_aq_data.pm2_5);
//                 }
//                 else
//                 {
//                     Serial.printf("[CMDPARSER] ZPHS01B #%lu — PM25=%u CO2=%u T=%.1f H=%u\n",
//                                   (unsigned long)s_frame_counter_9param,
//                                   (unsigned)current_aq_data.pm2_5,
//                                   (unsigned)current_aq_data.co2,
//                                   current_aq_data.temperature,
//                                   (unsigned)current_aq_data.humidity);
//                 }
//             }
// #endif
//         }
//     }
// }

// /*==============================================================================
//  *                          DATA ACCESS APIs
//  *============================================================================*/

// /**
//  * @brief Get all air quality parameters in one call
//  */
// CmdParser_StatusType_t CmdParser__GetAirQualityData(CmdParser_ZPHS01B_Data_t *data)
// {
//     CmdParser_StatusType_t status;

//     if (!IsValidPointer(data))
//     {
//         return CMDPARSER_STATUS_INVALID_PARAM;
//     }

//     if (!module_initialized)
//     {
//         return CMDPARSER_STATUS_NOT_INITIALIZED;
//     }

//     if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
//     {
//         if (!IsDataFresh())
//         {
//             status = CMDPARSER_STATUS_DATA_STALE;
//         }
//         else if (!current_aq_data.valid)
//         {
//             status = CMDPARSER_STATUS_NO_DATA;
//         }
//         else
//         {
//             *data  = current_aq_data;
//             status = CMDPARSER_STATUS_OK;
//         }
//         xSemaphoreGive(data_mutex);
//     }
//     else
//     {
//         status = CMDPARSER_STATUS_BUSY;
//     }

//     return status;
// }

// /**
//  * @brief Get raw received frame buffer for debugging
//  */
// CmdParser_StatusType_t CmdParser__GetRawBuffer(uint8_t  *buffer,
//                                                 uint16_t *buffer_length,
//                                                 uint16_t  max_length)
// {
//     if (!IsValidPointer(buffer) || !IsValidPointer(buffer_length))
//     {
//         return CMDPARSER_STATUS_INVALID_PARAM;
//     }

//     if (!module_initialized)
//     {
//         return CMDPARSER_STATUS_NOT_INITIALIZED;
//     }

//     if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
//     {
//         if (!raw_buffer_available || (last_complete_length == 0U))
//         {
//             xSemaphoreGive(data_mutex);
//             return CMDPARSER_STATUS_NO_DATA;
//         }
//         uint16_t copy_len = (last_complete_length < max_length)
//                                 ? last_complete_length : max_length;
//         memcpy(buffer, last_complete_buffer, copy_len);
//         *buffer_length = copy_len;
//         xSemaphoreGive(data_mutex);
//         return CMDPARSER_STATUS_OK;
//     }

//     return CMDPARSER_STATUS_BUSY;
// }

// /*==============================================================================
//  *                          ERROR AND STATUS APIs
//  *============================================================================*/

// CmdParser_StatusType_t CmdParser__GetErrorStatus(CmdParser_ErrorType_t *error_type)
// {
//     if (!IsValidPointer(error_type))
//     {
//         return CMDPARSER_STATUS_INVALID_PARAM;
//     }

//     if (!module_initialized)
//     {
//         return CMDPARSER_STATUS_NOT_INITIALIZED;
//     }

//     *error_type = current_error;
//     return CMDPARSER_STATUS_OK;
// }

// CmdParser_StatusType_t CmdParser__ClearErrorStatus(void)
// {
//     if (!module_initialized)
//     {
//         return CMDPARSER_STATUS_NOT_INITIALIZED;
//     }

//     if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
//     {
//         current_error      = CMDPARSER_ERROR_NONE;
//         consecutive_errors = 0U;
//         xSemaphoreGive(data_mutex);
//     }

//     return CMDPARSER_STATUS_OK;
// }

// bool CmdParser__IsInitialized(void)
// {
//     return module_initialized;
// }

// bool CmdParser__IsDataFresh(void)
// {
//     bool fresh = false;

//     if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
//     {
//         fresh = IsDataFresh() && new_data_available;
//         xSemaphoreGive(data_mutex);
//     }

//     return fresh;
// }

// /*==============================================================================
//  *                          UTILITY APIs
//  *============================================================================*/

// CmdParser_StatusType_t CmdParser__Reset(void)
// {
//     if (!module_initialized)
//     {
//         return CMDPARSER_STATUS_NOT_INITIALIZED;
//     }

//     if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
//     {
//         module_state  = CMDPARSER_STATE_INITIALIZED;
//         current_error = CMDPARSER_ERROR_NONE;

//         ResetMessageState();

//         /* Flush UART buffer */
//         while (UART_PORT.available() > 0) { UART_PORT.read(); }

//         memset(&current_aq_data,     0, sizeof(current_aq_data));
//         memset(last_complete_buffer, 0, sizeof(last_complete_buffer));

//         last_complete_length = 0U;
//         raw_buffer_available = false;
//         consecutive_errors   = 0U;
//         last_valid_data_time = 0U;
//         new_data_available   = false;

//         xSemaphoreGive(data_mutex);
//     }

//     return CMDPARSER_STATUS_OK;
// }

// CmdParser_StatusType_t CmdParser__GetTotalMessages(uint32_t *count)
// {
//     if (!IsValidPointer(count))
//     {
//         return CMDPARSER_STATUS_INVALID_PARAM;
//     }

//     if (!module_initialized)
//     {
//         return CMDPARSER_STATUS_NOT_INITIALIZED;
//     }

//     if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
//     {
//         *count = total_messages_received;
//         xSemaphoreGive(data_mutex);
//         return CMDPARSER_STATUS_OK;
//     }

//     return CMDPARSER_STATUS_BUSY;
// }

// /*==============================================================================
//  *                          SENSOR TYPE APIs
//  *============================================================================*/

// uint32_t CmdParser__GetSensorType(void)
// {
//     return s_detected_sensor_type;
// }

// bool CmdParser__IsSensorDetected(void)
// {
//     return s_sensor_detected;
// }

// /**
//  * @brief Force sensor type re-detection on the next received frame
//  */
// void CmdParser__ResetSensorType(void)
// {
//     taskENTER_CRITICAL(&critical_mux);
//     s_detected_sensor_type = SENSOR_TYPE_DEFAULT;
//     s_sensor_detected      = false;
//     taskEXIT_CRITICAL(&critical_mux);

//     /* Erase NVS entry so next boot also re-detects */
//     s_prefs.begin(NVS_NAMESPACE, false);
//     s_prefs.remove(NVS_KEY_SENSOR);
//     s_prefs.end();

// //     Serial.println("[CMDPARSER] Sensor type reset — will auto-detect from next frame");
//  }

// /*==============================================================================
//  *                     STATIC FUNCTION IMPLEMENTATIONS
//  *============================================================================*/

// static bool IsValidPointer(const void *ptr)
// {
//     return (ptr != NULL);
// }

// static void ResetMessageState(void)
// {
//     taskENTER_CRITICAL(&critical_mux);
//     bytes_received     = 0U;
//     message_complete   = false;
//     message_start_time = 0U;
//     last_char_time     = 0U;
//     memset(rx_buffer, 0, sizeof(rx_buffer));
//     taskEXIT_CRITICAL(&critical_mux);
// }

// static void UpdateErrorStatus(CmdParser_ErrorType_t error_type)
// {
//     current_error = error_type;
// }

// static bool IsDataFresh(void)
// {
//     uint32_t current_time = millis();
//     uint32_t elapsed;

//     if (last_valid_data_time == 0U)
//     {
//         return false;
//     }

//     if (current_time >= last_valid_data_time)
//     {
//         elapsed = current_time - last_valid_data_time;
//     }
//     else
//     {
//         elapsed = (0xFFFFFFFFU - last_valid_data_time) + current_time + 1U;
//     }

//     return (elapsed < DATA_FRESHNESS_TIMEOUT_MS);
// }

// static bool VerifyChecksum(const uint8_t *frame, uint16_t length)
// {
//     uint8_t  sum = 0U;
//     uint16_t i;

//     if (!IsValidPointer(frame) || (length != ZPHS01B_FRAME_LENGTH))
//     {
//         return false;
//     }

//     for (i = 1U; i <= 24U; i++)
//     {
//         sum += frame[i];
//     }

//     uint8_t expected_checksum = (~sum + 1U) & 0xFFU;

//     return (expected_checksum == frame[ZPHS01B_FRAME_LENGTH - 1U]);
// }

// static CmdParser_ErrorType_t ParseMessage(void)
// {
//     uint16_t raw_u16;

//     if (bytes_received != ZPHS01B_FRAME_LENGTH)
//     {
//         return CMDPARSER_ERROR_INSUFFICIENT_BYTES;
//     }

//     if ((rx_buffer[ZPHS01B_BYTE_START] != ZPHS01B_START_BYTE) ||
//         (rx_buffer[ZPHS01B_BYTE_CMD]   != ZPHS01B_CMD_BYTE))
//     {
//         return CMDPARSER_ERROR_INVALID_DATA;
//     }

// #if (CMDPARSER_ENABLE_CHECKSUM_VERIFY == true)
//     if (!VerifyChecksum(rx_buffer, ZPHS01B_FRAME_LENGTH))
//     {
//         // Serial.printf("[CMDPARSER] Checksum FAIL — got 0x%02X\n",
//         //               rx_buffer[ZPHS01B_BYTE_CHECKSUM]);
//         return CMDPARSER_ERROR_CHECKSUM_FAIL;
//     }
// #endif

//     raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_PM10_HIGH] << 8U) |
//                (uint16_t)rx_buffer[ZPHS01B_BYTE_PM10_LOW];
//     current_aq_data.pm1_0 = raw_u16;

//     raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_PM25_HIGH] << 8U) |
//                (uint16_t)rx_buffer[ZPHS01B_BYTE_PM25_LOW];
//     current_aq_data.pm2_5 = raw_u16;

//     raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_PM100_HIGH] << 8U) |
//                (uint16_t)rx_buffer[ZPHS01B_BYTE_PM100_LOW];
//     current_aq_data.pm10 = raw_u16;

//     raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_CO2_HIGH] << 8U) |
//                (uint16_t)rx_buffer[ZPHS01B_BYTE_CO2_LOW];
//     current_aq_data.co2 = raw_u16;

//     current_aq_data.voc = rx_buffer[ZPHS01B_BYTE_VOC];

//     raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_TEMP_HIGH] << 8U) |
//                (uint16_t)rx_buffer[ZPHS01B_BYTE_TEMP_LOW];
//     current_aq_data.temperature = ((float)raw_u16 - (float)ZPHS01B_TEMP_OFFSET) * 0.1f;

//     raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_HUM_HIGH] << 8U) |
//                (uint16_t)rx_buffer[ZPHS01B_BYTE_HUM_LOW];
//     current_aq_data.humidity = raw_u16 / 10U; 

//     raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_CH2O_HIGH] << 8U) |
//                (uint16_t)rx_buffer[ZPHS01B_BYTE_CH2O_LOW];
//     current_aq_data.ch2o = (float)raw_u16 * 0.001f;

//     raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_CO_HIGH] << 8U) |
//                (uint16_t)rx_buffer[ZPHS01B_BYTE_CO_LOW];
//     current_aq_data.co = (float)raw_u16 * 0.1f;

//     raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_O3_HIGH] << 8U) |
//                (uint16_t)rx_buffer[ZPHS01B_BYTE_O3_LOW];
//     current_aq_data.o3 = (float)raw_u16 * 0.01f;

//     raw_u16 = ((uint16_t)rx_buffer[ZPHS01B_BYTE_NO2_HIGH] << 8U) |
//                (uint16_t)rx_buffer[ZPHS01B_BYTE_NO2_LOW];
//     current_aq_data.no2 = (float)raw_u16 * 0.01f;

//     current_aq_data.valid        = true;
//     current_aq_data.timestamp_ms = millis();

//     s_frame_counter_9param++;

//     return CMDPARSER_ERROR_NONE;
// }