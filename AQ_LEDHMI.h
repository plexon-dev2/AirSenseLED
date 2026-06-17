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
 *                        1Hz blink while advertising / pairing (switch ON)
 *                        Solid ON once a central device is connected
 *                        OFF when switch turned OFF via LEDHMI__BleOff()
 *
 * @author
 * @date 2025-12-19
 * @version 1.1.0
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
    AQ_LED_OFF      = 0, /**< LED is permanently OFF          */
    AQ_LED_ON       = 1, /**< LED is permanently ON           */
    AQ_LED_BLINK    = 2, /**< LED is blinking (N count)       */
    AQ_LED_TIMED_ON = 3  /**< LED is ON for a fixed duration  */
} AQ_LED_State;

/**
 * @brief LED operation status enumeration
 */
typedef enum
{
    AQ_LED_STATUS_OK    =  0, /**< Operation successful */
    AQ_LED_STATUS_ERROR = -1  /**< Operation failed     */
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
    AQ_LED_Config config;           /**< Hardware configuration              */
    AQ_LED_State  state;            /**< Current LED state                   */
    uint32_t      last_toggle_time; /**< Timestamp of last toggle (ms)       */
    uint32_t      timed_on_start;   /**< Timestamp when timed ON started(ms) */
    uint16_t      timed_on_duration;/**< Timed ON duration (ms)              */
    uint8_t       blink_count;      /**< Remaining blink phases (ON+OFF = 2) */
    uint8_t       is_on;            /**< Current physical pin state          */
} AQ_LED_Instance;

/*==============================================================================
 *                          PUBLIC FUNCTION DECLARATIONS
 *============================================================================*/

/**
 * @brief Initialize an LED instance
 * @details Configures GPIO pin direction and sets LED to OFF state
 * @param[out] led    Pointer to LED instance to initialize
 * @param[in]  config Pointer to LED hardware configuration
 * @return AQ_LED_STATUS_OK on success, AQ_LED_STATUS_ERROR on NULL pointer
 */
AQ_LED_Status AQLED__Init(AQ_LED_Instance *led, const AQ_LED_Config *config);

/**
 * @brief Turn LED permanently ON
 * @details Sets LED state to AQ_LED_ON and drives the GPIO pin
 * @param[in,out] led Pointer to LED instance
 */
void AQLED__On(AQ_LED_Instance *led);

/**
 * @brief Turn LED permanently OFF
 * @details Sets LED state to AQ_LED_OFF and drives the GPIO pin
 * @param[in,out] led Pointer to LED instance
 */
void AQLED__Off(AQ_LED_Instance *led);

/**
 * @brief Start LED blink sequence
 * @details Blinks LED for specified count then turns OFF.
 *          Ignored if blink is already in progress.
 * @param[in,out] led   Pointer to LED instance
 * @param[in]     count Number of blinks (must be > 0)
 */
void AQLED__Blink(AQ_LED_Instance *led, uint8_t count);

/**
 * @brief Turn LED ON for a fixed duration
 * @details Sets LED to ON and schedules automatic OFF after durationMs
 * @param[in,out] led        Pointer to LED instance
 * @param[in]     durationMs ON duration in milliseconds (must be > 0)
 */
void AQLED__TimedOn(AQ_LED_Instance *led, uint16_t durationMs);

/**
 * @brief Update LED state machine
 * @details Must be called periodically (every 50ms) to process
 *          blink counts and timed ON expiry
 * @param[in,out] led          Pointer to LED instance
 * @param[in]     currentTimeMs Current system time from millis()
 */
void AQLED__Update(AQ_LED_Instance *led, uint32_t currentTimeMs);

/**
 * @brief Initialize the LED HMI module
 * @details Initializes all 6 LED instances and starts:
 *          - POWER  LED solid ON
 *          - CPU    LED 1Hz continuous blink
 *          - BLE    LED OFF at boot (BLESwitch__Init enables it when switch is ON)
 *          - All other LEDs in OFF state
 */
void LEDHMI__Init(void);

/**
 * @brief LED HMI periodic handler
 * @details Drives all LED state machines. Must be called every 50ms.
 *          Manages:
 *          - CPU    1Hz continuous blink  (500ms ON / 500ms OFF)
 *          - ERROR  1Hz blink when active (500ms ON / 500ms OFF)
 *          - ALARM  1Hz blink when active (500ms ON / 500ms OFF)
 *          - MODBUS 1Hz blink while Modbus master is active
 *          - BLE    OFF when switch OFF; 1Hz blink advertising; solid ON connected
 */
void LEDHMI__Handler(void);

/**
 * @brief Indicate active alarm condition
 * @details Starts ALARM LED 1Hz blink (500ms ON / 500ms OFF)
 */
void LEDHMI__AlarmActive(void);

/**
 * @brief Indicate alarm cleared
 * @details Stops ALARM LED blink and turns it OFF
 */
void LEDHMI__AlarmCleared(void);

/**
 * @brief Indicate system error condition
 * @details Starts ERROR LED 1Hz blink (500ms ON / 500ms OFF).
 *          Error sources: fan failure, sensor stopped, no Modbus master response.
 */
void LEDHMI__ErrorActive(void);

/**
 * @brief Indicate error cleared
 * @details Stops ERROR LED blink and turns it OFF
 */
void LEDHMI__ErrorCleared(void);

/**
 * @brief Notify the LED HMI that a Modbus frame was received from the master
 * @details Call this on every valid Modbus poll/request.
 *          MODBUS LED blinks continuously while master is polling;
 *          it stops automatically when master goes silent (>2s timeout).
 */
void LEDHMI__ModbusPoll(void);

/**
 * @brief Indicate Modbus data received (response sent successfully)
 * @details Kept for API compatibility with Modbus.cpp call sites.
 *          Internally updates the last-activity timestamp used by the
 *          Modbus LED keep-alive logic; no separate visual behaviour.
 */
void LEDHMI__ModbusDataReceived(void);

/**
 * @brief Indicate BLE central device connected
 * @details Stops pairing blink and turns BLE LED solid ON
 */
void LEDHMI__BleConnected(void);

/**
 * @brief Indicate BLE central device disconnected
 * @details Turns BLE LED OFF and resumes 1Hz pairing blink (advertising)
 */
void LEDHMI__BleDisconnected(void);

/**
 * @brief Turn BLE LED fully OFF — called when physical switch is turned OFF
 * @details Unlike BleDisconnected(), this does NOT restart the pairing blink.
 *          LED stays solid OFF until BLESwitch turns BLE back ON.
 */
void LEDHMI__BleOff(void);

#endif /* AQ_LEDHMI_H */