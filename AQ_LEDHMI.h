/**
 * @file AQ_LEDHMI.h
 * @brief Air Quality LED HMI Interface
 * @details LED management system for the Air Quality Monitor project.
 *          Provides LED instance control with state machine support.
 *
 * SUPPORTED LED STATES:
 * =====================
 *   AQ_LED_OFF       - LED permanently OFF
 *   AQ_LED_ON        - LED permanently ON
 *   AQ_LED_BLINK     - LED blinking for N counts then OFF
 *   AQ_LED_TIMED_ON  - LED ON for specified duration then OFF
 *
 * LED BEHAVIOUR (per product specification):
 * ==========================================
 *   POWER  LED (Green) : Solid ON whenever power is connected
 *   CPU    LED (Green) : 1Hz blink (500ms ON / 500ms OFF), continuous after power-on
 *   ERROR  LED (Red)   : 1Hz blink while any system error is active
 *                        Errors: Fan failed | Sensor stopped | No Modbus master response
 *   ALARM  LED (Red)   : 1Hz blink while any air-quality threshold is exceeded
 *   MODBUS LED (Yellow): 1Hz blink while Modbus master communication is active;
 *                        stops blinking when master goes silent
 *   BLE    LED (Blue)  : OFF at boot — controlled by physical switch (GPIO8)
 *                        1Hz blink while advertising / pairing (switch ON or connected)
 *                        OFF when switch turned OFF via LEDHMI__BleOff()
 *
 * @date 2025-12-19
 * @version 1.3.0
 *
 * v1.3.0 changes (review fixes):
 *   - BLE LED behaviour corrected in header: "Solid ON" removed -- LED blinks 1Hz
 *     when connected, matching the implementation in LEDHMI__Handler()
 *   - LEDHMI__BleOff() added (was declared but not implemented -- linker error)
 *   - AQ_LED_Status: AQ_LED_STATUS_ERROR changed from -1 to 0xFF and enum
 *     given explicit uint8_t-compatible values (MISRA Rule 10.3 fix)
 */

#ifndef AQ_LEDHMI_H
#define AQ_LEDHMI_H

/*==============================================================================
 *                              INCLUDES
 *============================================================================*/
#include <stdint.h>

/*==============================================================================
 *                              PUBLIC TYPES
 *============================================================================*/

/**
 * @brief LED state enumeration
 */
typedef enum
{
    AQ_LED_OFF      = 0U, /**< LED is permanently OFF          */
    AQ_LED_ON       = 1U, /**< LED is permanently ON           */
    AQ_LED_BLINK    = 2U, /**< LED is blinking (N count)       */
    AQ_LED_TIMED_ON = 3U  /**< LED is ON for a fixed duration  */
} AQ_LED_State;

/**
 * @brief LED operation status enumeration
 * @note  Both values are non-negative to avoid signed/unsigned mixing (MISRA Rule 10.3).
 */
typedef enum
{
    AQ_LED_STATUS_OK    = 0U,   /**< Operation successful */
    AQ_LED_STATUS_ERROR = 0xFFU /**< Operation failed     */
} AQ_LED_Status;

/**
 * @brief LED hardware and timing configuration structure
 */
typedef struct
{
    uint8_t  pin;          /**< GPIO pin number                        */
    uint16_t blink_on_ms;  /**< Blink ON  duration in milliseconds     */
    uint16_t blink_off_ms; /**< Blink OFF duration in milliseconds     */
    uint8_t  active_low;   /**< 1 = Active Low, 0 = Active High        */
} AQ_LED_Config;

/**
 * @brief LED runtime instance structure
 */
typedef struct
{
    AQ_LED_Config config;            /**< Hardware configuration              */
    AQ_LED_State  state;             /**< Current LED state                   */
    uint32_t      last_toggle_time;  /**< Timestamp of last toggle (ms)       */
    uint32_t      timed_on_start;    /**< Timestamp when timed ON started(ms) */
    uint16_t      timed_on_duration; /**< Timed ON duration (ms)              */
    uint8_t       blink_count;       /**< Remaining blink phases (ON+OFF = 2) */
    uint8_t       is_on;             /**< Current physical pin state          */
} AQ_LED_Instance;

/*==============================================================================
 *                          PUBLIC FUNCTION DECLARATIONS
 *============================================================================*/

/**
 * @brief Initialize an LED instance
 * @param[out] led    Pointer to LED instance to initialize
 * @param[in]  config Pointer to LED hardware configuration
 * @return AQ_LED_STATUS_OK on success, AQ_LED_STATUS_ERROR on NULL pointer
 */
AQ_LED_Status AQLED__Init(AQ_LED_Instance *led, const AQ_LED_Config *config);

/**
 * @brief Turn LED permanently ON
 * @param[in,out] led Pointer to LED instance
 */
void AQLED__On(AQ_LED_Instance *led);

/**
 * @brief Turn LED permanently OFF
 * @param[in,out] led Pointer to LED instance
 */
void AQLED__Off(AQ_LED_Instance *led);

/**
 * @brief Start LED blink sequence for N blinks then OFF
 * @details Ignored if a blink is already in progress.
 * @param[in,out] led   Pointer to LED instance
 * @param[in]     count Number of blinks (1–127; values above 127 are clamped)
 */
void AQLED__Blink(AQ_LED_Instance *led, uint8_t count);

/**
 * @brief Turn LED ON for a fixed duration then OFF automatically
 * @param[in,out] led        Pointer to LED instance
 * @param[in]     durationMs ON duration in milliseconds (must be > 0)
 */
void AQLED__TimedOn(AQ_LED_Instance *led, uint16_t durationMs);

/**
 * @brief Update LED state machine — call periodically every 50ms
 * @param[in,out] led           Pointer to LED instance
 * @param[in]     currentTimeMs Current system time from millis()
 */
void AQLED__Update(AQ_LED_Instance *led, uint32_t currentTimeMs);

/**
 * @brief Initialize the LED HMI module
 * @details Initializes all 6 LED instances:
 *          POWER solid ON, CPU 1Hz blink, all others OFF.
 */
void LEDHMI__Init(void);

/**
 * @brief LED HMI periodic handler — must be called every 50ms
 * @details Drives all LED state machines and evaluates alarm/error conditions.
 */
void LEDHMI__Handler(void);

/** @brief Indicate active alarm condition — starts ALARM LED 1Hz blink */
void LEDHMI__AlarmActive(void);

/** @brief Indicate alarm cleared — stops ALARM LED and turns it OFF */
void LEDHMI__AlarmCleared(void);

/** @brief Indicate system error condition — starts ERROR LED 1Hz blink */
void LEDHMI__ErrorActive(void);

/** @brief Indicate error cleared — stops ERROR LED and turns it OFF */
void LEDHMI__ErrorCleared(void);

/**
 * @brief Notify that a Modbus frame was received from the master
 * @details MODBUS LED blinks while polls arrive; stops after MODBUS_LED_TIMEOUT_MS silence.
 */
void LEDHMI__ModbusPoll(void);

/**
 * @brief Indicate Modbus data received (response sent)
 * @details Updates last-activity timestamp for Modbus LED keep-alive logic.
 */
void LEDHMI__ModbusDataReceived(void);

/**
 * @brief Indicate BLE central device connected
 * @details Starts BLE LED 1Hz blink (advertising/connected indication).
 */
void LEDHMI__BleConnected(void);

/**
 * @brief Indicate BLE central device disconnected
 * @details Turns BLE LED OFF (stops blink SM).
 */
void LEDHMI__BleDisconnected(void);

/**
 * @brief Turn BLE LED fully OFF — called when physical BLE switch is turned OFF
 * @details Unlike BleDisconnected(), clears the BLE active flag so the
 *          blink SM does not restart until BleConnected() is called again.
 */
void LEDHMI__BleOff(void);

#endif /* AQ_LEDHMI_H */