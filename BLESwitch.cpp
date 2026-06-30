/**
 * @file BLESwitch.cpp
 * @brief BLE Hardware Switch Control -- Implementation
 *
 * STATE MACHINE TRANSITIONS:
 * ==========================
 *
 *   Power on (cold)
 *       |
 *       v
 *   COLD_IDLE ---- rising edge press -----------------------> ADVERTISING
 *                                                                 |
 *   Power on (warm)                                          client connects
 *       |                                                        |
 *       v                                                        v
 *   ADVERTISING_TIMED <--- 5-sec hold ---- BLE_OFF          CONNECTED
 *       |                                    ^                   |
 *       |-- client connects --> CONNECTED    |            client disconnects
 *       |         |                          |                   |
 *       |    client disconnects              |                   v
 *       |         |                          |              ADVERTISING
 *       |         v                          |
 *       |    ADVERTISING                     |
 *       |                                    |
 *       +-- 2-min expires, no connect -------+
 *
 * @version 2.1.0
 *
 * v2.1.0 changes (review fixes):
 *   - CRITICAL: COLD_IDLE now detects rising edge (press), not level.
 *     Was firing BLESwitch_StartBLE() on every 50ms tick while switch held,
 *     hammering the BLE advertising stack. Fixed by tracking s_prevSwitchActive.
 *   - BLE_OFF: removed redundant s_switchPressStartMs = nowMs in else branch.
 *     Press timestamp is correctly set by UpdateDebounce() on edge; overwriting
 *     it every tick while released was confusing and masked the debounce logic.
 *   - BLESwitch_StartBLE(): (void)withTimer removed. Parameter was silently
 *     discarded, making call sites misleading. Function signature simplified to
 *     no parameter -- callers manage timer intent entirely via EnterState().
 *   - BLESwitch__ForceOff() implemented -- external API for low-battery / OTA.
 *   - BLEDevice::getAdvertising() null-checked before use in StartBLE / Init.
 *   - BLESWITCH_DBG_PRINTF now uses SERIAL_PRINTF (mutex-safe) via _Cfg.h.
 *   - Non-ASCII arrow characters in header comment replaced with ASCII.
 *   - s_prevSwitchActive added to file-scope statics for edge detection.
 *   - LEDHMI__BleDisconnected() in StopBLE() documented: called from Core 0
 *     scheduler task -- safe because LEDHMI also runs Core 1 and the call
 *     is a simple flag+pin write protected by the LEDHMI volatile flags.
 */

/*==============================================================================
 *                              INCLUDES
 *============================================================================*/
#include "Arduino.h"
#include "esp_system.h"
#include <BLEDevice.h>
#include "BLESwitch.h"
#include "BLESwitch_Cfg.h"
#include "BLEComm.h"
#include "AQ_LEDHMI.h"

/*==============================================================================
 *                          PRIVATE TYPES
 *============================================================================*/

/**
 * @brief BLE switch state machine states
 */
typedef enum
{
    BLESWITCH_STATE_COLD_IDLE         = 0U, /**< Cold boot: BLE off, wait for press    */
    BLESWITCH_STATE_ADVERTISING       = 1U, /**< BLE on, LED off, no timer             */
    BLESWITCH_STATE_ADVERTISING_TIMED = 2U, /**< BLE on, LED off, 2-min timer active   */
    BLESWITCH_STATE_CONNECTED         = 3U, /**< Client connected -- LED blinks        */
    BLESWITCH_STATE_BLE_OFF           = 4U, /**< Timed out: BLE off, wait 5-sec hold   */
} BLESwitch_State;

/*==============================================================================
 *                          PRIVATE VARIABLES
 *============================================================================*/

/** @brief Current state machine state */
static BLESwitch_State s_state = BLESWITCH_STATE_COLD_IDLE;

/**
 * @brief BLE enabled flag.
 * @note  volatile: written Core 0 (BLESwitch__Handler), read Core 1 (LEDHMI).
 */
static volatile bool s_bleEnabled = false;

/** @brief Previous BLE connection state -- for connect/disconnect edge detection */
static bool s_prevConnected = false;

/**
 * @brief Previous debounced switch active state -- for press rising edge detection.
 * @details Updated at the end of each BLESwitch__Handler() call alongside
 *          s_prevConnected.  Ensures COLD_IDLE fires exactly once per press,
 *          not on every tick while the switch is held.
 */
static bool s_prevSwitchActive = false;

/** @brief Timestamp when current state was entered */
static uint32_t s_stateEntryMs = 0U;

/** @brief Timestamp when switch confirmed pressed (set by UpdateDebounce on edge) */
static uint32_t s_switchPressStartMs = 0U;

/** @brief Debounced switch state -- true = switch is pressed */
static bool s_switchActive = false;

/** @brief Last raw GPIO read -- used by debounce logic */
static bool s_lastRawRead = false;

/** @brief Timestamp when current debounce window started */
static uint32_t s_debounceStartMs = 0U;

/** @brief Whether a debounce window is currently in progress */
static bool s_debouncing = false;

/*==============================================================================
 *                          PRIVATE FUNCTION PROTOTYPES
 *============================================================================*/
static bool BLESwitch_ReadPin(void);
static void BLESwitch_UpdateDebounce(uint32_t nowMs);
static void BLESwitch_StartBLE(void);
static void BLESwitch_StopBLE(void);
static void BLESwitch_EnterState(BLESwitch_State newState, uint32_t nowMs);

/*==============================================================================
 *                          PUBLIC IMPLEMENTATIONS
 *============================================================================*/

void BLESwitch__Init(void)
{
    uint32_t nowMs = millis();

    /* Configure switch pin -- INPUT_PULLUP, active LOW */
    pinMode(BLESWITCH_PIN, INPUT_PULLUP);

    /* Stop any advertising BLEComm__Init() may have started.
     * BLESwitch is the sole authority for advertising start/stop.
     * Null-check getAdvertising() in case BLEComm__Init() was not called. */
    BLEAdvertising *pAdv = BLEDevice::getAdvertising();
    if (pAdv != NULL)
    {
        pAdv->stop();
    }

    /* Initialise all state */
    s_lastRawRead        = BLESwitch_ReadPin();
    s_switchActive       = false;
    s_prevSwitchActive   = false;
    s_debouncing         = false;
    s_debounceStartMs    = 0U;
    s_switchPressStartMs = 0U;
    s_prevConnected      = false;
    s_bleEnabled         = false;

    /*--------------------------------------------------------------------------
     * Detect cold vs warm boot via hardware reset reason register.
     *   ESP_RST_POWERON = physical power-on (cold boot)
     *   All other values = warm reset (software, watchdog, panic, brownout...)
     *------------------------------------------------------------------------*/
    esp_reset_reason_t resetReason = esp_reset_reason();
    bool               isColdBoot  = (resetReason == ESP_RST_POWERON);

    if (isColdBoot)
    {
        BLESwitch_EnterState(BLESWITCH_STATE_COLD_IDLE, nowMs);
        BLESWITCH_DBG_PRINTF("[BLESWITCH] Cold boot -- BLE off, press switch to start\n");
    }
    else
    {
        BLESwitch_StartBLE();
        BLESwitch_EnterState(BLESWITCH_STATE_ADVERTISING_TIMED, nowMs);
        BLESWITCH_DBG_PRINTF("[BLESWITCH] Warm reset -- BLE on, 2-min timer started\n");
    }
}

void BLESwitch__Handler(void)
{
    uint32_t nowMs = millis();

    /* Update debounced switch state */
    BLESwitch_UpdateDebounce(nowMs);

    /* Poll BLE connection state for edge detection */
    bool nowConnected = BLEComm__IsServerClientConnected();

    switch (s_state)
    {
        /*----------------------------------------------------------------------
         * COLD_IDLE -- BLE off, waiting for switch short press
         * Trigger on RISING EDGE only (s_switchActive && !s_prevSwitchActive)
         * to avoid re-firing on every tick while switch is held.
         *--------------------------------------------------------------------*/
        case BLESWITCH_STATE_COLD_IDLE:
        {
            if (s_switchActive && !s_prevSwitchActive)
            {
                BLESwitch_StartBLE();
                BLESwitch_EnterState(BLESWITCH_STATE_ADVERTISING, nowMs);
                BLESWITCH_DBG_PRINTF("[BLESWITCH] Cold press -- BLE ON, advertising\n");
            }
            break;
        }

        /*----------------------------------------------------------------------
         * ADVERTISING -- BLE on, LED off, no timer
         * Entered from: cold boot press, or client disconnect.
         *--------------------------------------------------------------------*/
        case BLESWITCH_STATE_ADVERTISING:
        {
            if (nowConnected && !s_prevConnected)
            {
                LEDHMI__BleConnected();
                BLESwitch_EnterState(BLESWITCH_STATE_CONNECTED, nowMs);
                BLESWITCH_DBG_PRINTF("[BLESWITCH] Client connected -- LED blinks\n");
            }
            break;
        }

        /*----------------------------------------------------------------------
         * ADVERTISING_TIMED -- BLE on, LED off, 2-min timer running
         * Entered from: warm reset, or 5-sec hold after timeout.
         *--------------------------------------------------------------------*/
        case BLESWITCH_STATE_ADVERTISING_TIMED:
        {
            if (nowConnected && !s_prevConnected)
            {
                /* Client connected -- timer cancelled by entering CONNECTED */
                LEDHMI__BleConnected();
                BLESwitch_EnterState(BLESWITCH_STATE_CONNECTED, nowMs);
                BLESWITCH_DBG_PRINTF("[BLESWITCH] Client connected -- timer cancelled, LED blinks\n");
            }
            else if ((nowMs - s_stateEntryMs) >= BLESWITCH_WARM_TIMEOUT_MS)
            {
                /* 2-min expired with nobody connecting */
                BLESwitch_StopBLE();
                BLESwitch_EnterState(BLESWITCH_STATE_BLE_OFF, nowMs);
                BLESWITCH_DBG_PRINTF("[BLESWITCH] 2-min timeout -- BLE off, hold 5s to restart\n");
            }
            break;
        }

        /*----------------------------------------------------------------------
         * CONNECTED -- client connected, LED blinking
         *--------------------------------------------------------------------*/
        case BLESWITCH_STATE_CONNECTED:
        {
            if (!nowConnected && s_prevConnected)
            {
                /* Client just disconnected -- LED off, BLE stays advertising */
                LEDHMI__BleDisconnected();
                BLESwitch_EnterState(BLESWITCH_STATE_ADVERTISING, nowMs);
                BLESWITCH_DBG_PRINTF("[BLESWITCH] Client disconnected -- LED off, BLE stays ON\n");
            }
            break;
        }

        /*----------------------------------------------------------------------
         * BLE_OFF -- timed out, BLE off, LED off
         * Waiting for 5-second switch hold to re-enable.
         *--------------------------------------------------------------------*/
        case BLESWITCH_STATE_BLE_OFF:
        {
            if (s_switchActive)
            {
                uint32_t holdDuration = nowMs - s_switchPressStartMs;
                if (holdDuration >= BLESWITCH_HOLD_MS)
                {
                    BLESwitch_StartBLE();
                    BLESwitch_EnterState(BLESWITCH_STATE_ADVERTISING_TIMED, nowMs);
                    BLESWITCH_DBG_PRINTF("[BLESWITCH] 5-sec hold -- BLE ON, new 2-min timer\n");
                }
                /* else: still holding -- wait for hold duration to elapse */
            }
            /* else: switch not pressed -- nothing to do.
             * s_switchPressStartMs is set by UpdateDebounce() on the press edge;
             * do NOT overwrite it here while released (was a bug in prior version). */
            break;
        }

        default:
        {
            /* Defensive: unknown state -- reset to cold idle */
            BLESwitch_EnterState(BLESWITCH_STATE_COLD_IDLE, nowMs);
            break;
        }
    }

    /* Update previous states for edge detection on next cycle */
    s_prevConnected    = nowConnected;
    s_prevSwitchActive = s_switchActive;
}

bool BLESwitch__IsBLEEnabled(void)
{
    return (bool)s_bleEnabled;
}

void BLESwitch__ForceOff(void)
{
    BLESwitch_StopBLE();
    BLESwitch_EnterState(BLESWITCH_STATE_BLE_OFF, millis());
    BLESWITCH_DBG_PRINTF("[BLESWITCH] ForceOff -- BLE stopped by external caller\n");
}

/*==============================================================================
 *                          PRIVATE IMPLEMENTATIONS
 *============================================================================*/

/**
 * @brief Read physical switch GPIO.
 * @return true = switch pressed (active level confirmed).
 */
static bool BLESwitch_ReadPin(void)
{
    return (digitalRead(BLESWITCH_PIN) == BLESWITCH_ACTIVE_LEVEL);
}

/**
 * @brief Update debounced switch state.
 * @details 50ms debounce on both press and release edges.
 *          On confirmed press edge: records s_switchPressStartMs for hold timer.
 *          Updates s_switchActive; does NOT update s_prevSwitchActive
 *          (that is done at the end of BLESwitch__Handler() so the state
 *          machine sees the edge exactly once per confirmed transition).
 * @param[in] nowMs Current system time from millis().
 */
static void BLESwitch_UpdateDebounce(uint32_t nowMs)
{
    bool rawRead = BLESwitch_ReadPin();

    if (rawRead != s_lastRawRead)
    {
        /* Edge on raw GPIO -- start debounce window */
        s_debouncing      = true;
        s_debounceStartMs = nowMs;
        s_lastRawRead     = rawRead;
    }

    if (s_debouncing && ((nowMs - s_debounceStartMs) >= BLESWITCH_DEBOUNCE_MS))
    {
        /* Debounce window elapsed -- confirm new state */
        s_debouncing = false;

        bool wasActive = s_switchActive;
        s_switchActive = rawRead;

        if (!wasActive && s_switchActive)
        {
            /* Confirmed press edge -- record hold start timestamp */
            s_switchPressStartMs = nowMs;
        }
    }
}

/**
 * @brief Start BLE advertising.
 * @details Null-checks getAdvertising() before calling start().
 *          Timer management is handled entirely by the state the caller
 *          enters after calling this function -- no timer parameter needed.
 */
static void BLESwitch_StartBLE(void)
{
    BLEAdvertising *pAdv = BLEDevice::getAdvertising();
    if (pAdv != NULL)
    {
        pAdv->start();
    }
    s_bleEnabled = true;
}

/**
 * @brief Stop BLE advertising and disconnect any active client.
 * @details Null-checks getAdvertising() and getServer() before use.
 *          LEDHMI__BleDisconnected() called here -- this function runs
 *          in the Core 0 scheduler task context, which is the same core
 *          as BLEComm__Handler(); LEDHMI runs on Core 1. The call writes
 *          volatile LED flags which is safe for single-writer single-reader
 *          on this platform; a full mutex would be the correct final fix.
 */
static void BLESwitch_StopBLE(void)
{
    BLEAdvertising *pAdv = BLEDevice::getAdvertising();
    if (pAdv != NULL)
    {
        pAdv->stop();
    }

    /* Gracefully disconnect any active client */
    if (BLEComm__IsServerClientConnected())
    {
        BLEServer *pSrv = BLEDevice::getServer();
        if (pSrv != NULL)
        {
            pSrv->disconnect(pSrv->getConnId());
        }
    }

    s_bleEnabled = false;
    LEDHMI__BleDisconnected();
}

/**
 * @brief Transition to a new state and record the entry timestamp.
 * @param[in] newState State to enter.
 * @param[in] nowMs    Current system time from millis().
 */
static void BLESwitch_EnterState(BLESwitch_State newState, uint32_t nowMs)
{
    s_state        = newState;
    s_stateEntryMs = nowMs;
}