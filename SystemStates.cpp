/**
 * @file SystemStates.cpp
 * @brief System State Machine Implementation
 * @details Manages overall system state and health monitoring
 *          Implements state transitions and error handling
 *
 * State Machine:
 * - INIT: Initialization state
 * - NORMAL: Normal operation
 * - FAILURE: Error condition detected
 *
 * @author Generated Module
 * @date 2025-12-02
 * @version 2.1.0
 */

/*==============================================================================
 *                              INCLUDES
 *============================================================================*/
#include "SystemStates.h"
#include "SystemStates_Cfg.h"
#include "Modbus.h"
#include "CmdParser.h"
#include "App.h"
#include "Arduino.h"
//#include "BLEScreenProcess.h"

/*==============================================================================
 *                          PRIVATE VARIABLES
 *============================================================================*/

/* Current system state */
static SystemStates_State_t SystemStates_CurrentState = SYSTEMSTATES_INIT;

/* Error tracking */
static SystemStates_ErrorCode_t SystemStates_LastError = SYSTEMSTATES_ERROR_NONE;
static uint32_t SystemStates_ErrorCount = 0U;
static uint32_t SystemStates_ConsecutiveErrors = 0U;

/* Timing */
static uint32_t SystemStates_LastValidDataTime = 0U;
static uint32_t SystemStates_HealthCheckCounter = 0U;

/* Module initialization status */
static bool SystemStates_ModuleInitialized = false;

/*==============================================================================
 *                          PRIVATE FUNCTION PROTOTYPES
 *============================================================================*/
static void SystemStates_EnterInit(void);
static void SystemStates_EnterNormal(void);
static void SystemStates_EnterFailure(void);
static void SystemStates_HandleInit(void);
static void SystemStates_HandleNormal(void);
static void SystemStates_HandleFailure(void);
static bool SystemStates_CheckCommunication(void);
static bool SystemStates_CheckPowerSupply(void);
static void SystemStates_UpdateErrorStatus(SystemStates_ErrorCode_t error);
static uint32_t SystemStates_GetTimeDifference(uint32_t startTime);

/*==============================================================================
 *                          PUBLIC FUNCTION IMPLEMENTATIONS
 *============================================================================*/

/**
 * @brief Initialize System States Module
 * @details Initializes state machine and enters INIT state
 */
void SystemStates__Init(void)
{
    /* Initialize variables */
    SystemStates_CurrentState = SYSTEMSTATES_INIT;
    SystemStates_LastError = SYSTEMSTATES_ERROR_NONE;
    SystemStates_ErrorCount = 0U;
    SystemStates_ConsecutiveErrors = 0U;
    SystemStates_LastValidDataTime = millis();
    SystemStates_HealthCheckCounter = 0U;
    SystemStates_ModuleInitialized = true;

    Serial.println("[SYSTEMSTATES] Module initialized");
    Serial.println("[SYSTEMSTATES] State: INIT");
}

/**
 * @brief Main System States Handler
 * @details Called periodically by scheduler to update state machine
 */
void SystemStates__Handler(void)
{
    if (SystemStates_ModuleInitialized != true)
    {
        return;
    }

    /* Update health check counter */
    SystemStates_HealthCheckCounter++;

    /* Execute state-specific handler */
    switch (SystemStates_CurrentState)
    {
        case SYSTEMSTATES_INIT:
        {
            SystemStates_HandleInit();
            break;
        }

        case SYSTEMSTATES_NORMAL:
        {
            SystemStates_HandleNormal();
            break;
        }

        case SYSTEMSTATES_FAILURE:
        {
            SystemStates_HandleFailure();
            break;
        }

        default:
        {
            Serial.println("[SYSTEMSTATES] ERROR: Invalid state!");
            SystemStates__SetState(SYSTEMSTATES_INIT);
            break;
        }
    }
}

/**
 * @brief Get current system state
 * @return Current system state
 */
SystemStates_State_t SystemStates__GetState(void)
{
    return SystemStates_CurrentState;
}

/**
 * @brief Set system state
 * @details Forces a state transition
 * @param newState State to transition to
 */
void SystemStates__SetState(SystemStates_State_t newState)
{
    /* Validate input */
    if (newState >= SYSTEMSTATES_INVALID)
    {
        Serial.println("[SYSTEMSTATES] ERROR: Invalid state requested!");
        return;
    }
    
    if (newState == SystemStates_CurrentState)
    {
        return;
    }

    Serial.printf("[SYSTEMSTATES] State transition: %d -> %d\n",
                  (int)SystemStates_CurrentState, (int)newState);

    /* Exit current state */
    /* (No exit actions needed currently) */

    /* Update state */
    SystemStates_CurrentState = newState;

    /* Enter new state */
    switch (newState)
    {
        case SYSTEMSTATES_INIT:
        {
            SystemStates_EnterInit();
            break;
        }

        case SYSTEMSTATES_NORMAL:
        {
            SystemStates_EnterNormal();
            break;
        }

        case SYSTEMSTATES_FAILURE:
        {
            SystemStates_EnterFailure();
            break;
        }

        default:
        {
            break;
        }
    }
}

/**
 * @brief Get last error code
 * @return Last detected error code
 */
SystemStates_ErrorCode_t SystemStates__GetLastError(void)
{
    return SystemStates_LastError;
}

/**
 * @brief Get total error count
 * @return Total number of errors detected
 */
uint32_t SystemStates__GetErrorCount(void)
{
    return SystemStates_ErrorCount;
}

/**
 * @brief Perform diagnostic status check
 * @details Checks all system components for errors
 * @return true if all diagnostics pass, false if error detected
 */
bool SystemStates__DiagnosticStatus(void)
{
    uint32_t timeSinceData = 0U;
    
    /* Check 1: Communication timeout */
    timeSinceData = SystemStates_GetTimeDifference(SystemStates_LastValidDataTime);
    if (timeSinceData > SYSTEMSTATES_COMM_TIMEOUT_MS)
    {
        Serial.printf("[SYSTEMSTATES] Diagnostic FAIL: Communication timeout (%lu ms)\n",
                      (unsigned long)timeSinceData);
        return false;
    }

    /* Check 2: Modbus initialized */
    if (Modbus_IsInitialized() != true)
    {
        Serial.println("[SYSTEMSTATES] Diagnostic FAIL: Modbus not initialized");
        return false;
    }

    /* Check 3: CmdParser initialized */
    if (CmdParser__IsInitialized() != true)
    {
        Serial.println("[SYSTEMSTATES] Diagnostic FAIL: CmdParser not initialized");
        return false;
    }

    /* Check 4: Communication errors */
    if (SystemStates_CheckCommunication() != true)
    {
        Serial.println("[SYSTEMSTATES] Diagnostic FAIL: Communication error");
        return false;
    }

    /* Check 5: Power supply errors */
    if (SystemStates_CheckPowerSupply() != true)
    {
        Serial.println("[SYSTEMSTATES] Diagnostic FAIL: Power supply error");
        return false;
    }

    /* All checks passed */
    return true;
}

/**
 * @brief Clear all errors and reset counters
 * @details Resets error counters and clears error flags in all modules
 */
void SystemStates__ClearErrors(void)
{
    SystemStates_LastError = SYSTEMSTATES_ERROR_NONE;
    SystemStates_ErrorCount = 0U;
    SystemStates_ConsecutiveErrors = 0U;
    SystemStates_LastValidDataTime = millis();

    /* Clear errors in App module */
    App__ClearErrors();

    /* Clear errors in CmdParser */
    CmdParser__ClearErrorStatus();

    Serial.println("[SYSTEMSTATES] All errors cleared");
}

/**
 * @brief Force system reset from failure state (manual intervention)
 * @details Call this after fixing hardware issues to exit lockout mode
 */
void SystemStates__ForceReset(void)
{
    Serial.println("[SYSTEMSTATES] ===================================");
    Serial.println("[SYSTEMSTATES] MANUAL RESET TRIGGERED");
    Serial.println("[SYSTEMSTATES] ===================================");

    /* Clear all errors */
    SystemStates__ClearErrors();

    /* Force transition to INIT state */
    SystemStates__SetState(SYSTEMSTATES_INIT);

    Serial.println("[SYSTEMSTATES] System reset complete");
}

/**
 * @brief Calculate time difference handling millis() overflow
 * @details Handles 32-bit overflow that occurs after approximately 49 days
 * @param startTime Starting time from millis()
 * @return Time difference in milliseconds
 */
static uint32_t SystemStates_GetTimeDifference(uint32_t startTime)
{
    uint32_t currentTime = 0U;
    uint32_t timeDifference = 0U;

    currentTime = millis();

    if (currentTime >= startTime)
    {
        timeDifference = currentTime - startTime;
    }
    else
    {
        /* Overflow occurred (after ~49 days) */
        timeDifference = (UINT32_MAX - startTime) + currentTime + 1U;
    }
    
    return timeDifference;
}

/*==============================================================================
 *                          PRIVATE FUNCTION IMPLEMENTATIONS
 *============================================================================*/

/**
 * @brief Enter INIT state
 * @details Resets error counters and initializes timing
 */
static void SystemStates_EnterInit(void)
{
    Serial.println("[SYSTEMSTATES] Entering INIT state");

    /* Reset error counters */
    SystemStates_ConsecutiveErrors = 0U;
    SystemStates_LastValidDataTime = millis();
}

/**
 * @brief Enter NORMAL state
 * @details Clears all error conditions
 */
static void SystemStates_EnterNormal(void)
{
    Serial.println("[SYSTEMSTATES] Entering NORMAL state");

    /* Clear errors */
    SystemStates_LastError = SYSTEMSTATES_ERROR_NONE;
    SystemStates_ConsecutiveErrors = 0U;
    SystemStates_LastValidDataTime = millis();
}

/**
 * @brief Enter FAILURE state
 * @details Logs error information
 */
static void SystemStates_EnterFailure(void)
{
    Serial.println("[SYSTEMSTATES] Entering FAILURE state");
    Serial.printf("[SYSTEMSTATES] Error code: %d\n", (int)SystemStates_LastError);
    Serial.printf("[SYSTEMSTATES] Total errors: %lu\n", 
                  (unsigned long)SystemStates_ErrorCount);
}

/**
 * @brief Handle INIT state
 * @details Waits for initialization delay then transitions to Normal
 */
static void SystemStates_HandleInit(void)
{
    static uint32_t initStartTime = 0U;
    uint32_t timeInInit = 0U;

    if (initStartTime == 0U)
    {
        initStartTime = millis();
    }

    timeInInit = SystemStates_GetTimeDifference(initStartTime);
    
    if (timeInInit > SYSTEMSTATES_INIT_DELAY_MS)
    {
        initStartTime = 0U;
        SystemStates__SetState(SYSTEMSTATES_NORMAL);
    }
}

/**
 * @brief Handle NORMAL state
 * @details Performs periodic health checks and monitors for errors
 */
static void SystemStates_HandleNormal(void)
{
    uint32_t timeSinceData = 0U;
    uint16_t dataValid = 0U;
    
    /* Periodic health check */
    if (SystemStates_HealthCheckCounter >= SYSTEMSTATES_HEALTH_CHECK_INTERVAL)
    {
        SystemStates_HealthCheckCounter = 0U;

        /* Check if data is valid */
        if (Modbus_GetDataValid(&dataValid) == MODBUS_STATUS_OK)
        {
            if (dataValid == 1U)
            {
                SystemStates_LastValidDataTime = millis();
            }
        }

        /* Periodic status log */
        timeSinceData = SystemStates_GetTimeDifference(SystemStates_LastValidDataTime);

        Serial.printf("[SYSTEMSTATES] Normal | Total Errors: %lu | Consecutive: %lu | Data age: %lu ms\n",
                      (unsigned long)SystemStates_ErrorCount, 
                      (unsigned long)SystemStates_ConsecutiveErrors, 
                      (unsigned long)timeSinceData);
    }

    /* Check for power supply errors */
    if (SystemStates_CheckPowerSupply() != true)
    {
        SystemStates_UpdateErrorStatus(SYSTEMSTATES_ERROR_POWER_SUPPLY);
        SystemStates__SetState(SYSTEMSTATES_FAILURE);
        return;
    }

    /* Check for communication errors */
    if (SystemStates_CheckCommunication() != true)
    {
        SystemStates_UpdateErrorStatus(SYSTEMSTATES_ERROR_COMMUNICATION);

        /* Increment consecutive errors */
        SystemStates_ConsecutiveErrors++;

        /* Transition to failure if too many consecutive errors */
        if (SystemStates_ConsecutiveErrors >= SYSTEMSTATES_MAX_CONSECUTIVE_ERRORS)
        {
            SystemStates__SetState(SYSTEMSTATES_FAILURE);
        }
        return;
    }

    /* No errors - reset consecutive error counter */
    SystemStates_ConsecutiveErrors = 0U;
}

/**
 * @brief Handle FAILURE state
 * @details Attempts automatic recovery or enters lockout mode
 */
static void SystemStates_HandleFailure(void)
{
    static uint32_t failureStartTime = 0U;
    static uint32_t recoveryAttemptCount = 0U;
    uint32_t timeInFailure = 0U;
    bool communicationRestored = false;
    uint16_t dataValid = 0U;

    /* Initialize failure timer on first entry (but not if in lockout mode) */
    if (failureStartTime == 0U)
    {
        failureStartTime = millis();
        recoveryAttemptCount = 0U;
    }
    else if (failureStartTime == UINT32_MAX)
    {
        /* In lockout mode - do nothing until manual reset */
        return;
    }

    /* Calculate time in failure state */
    timeInFailure = SystemStates_GetTimeDifference(failureStartTime);

    /* Check if we should attempt recovery */
    if (timeInFailure > SYSTEMSTATES_FAILURE_RECOVERY_DELAY_MS)
    {
        recoveryAttemptCount++;
        Serial.printf("[SYSTEMSTATES] Recovery attempt #%lu...\n", 
                      (unsigned long)recoveryAttemptCount);

        /* Check if too many recovery attempts */
        if (recoveryAttemptCount >= SYSTEMSTATES_MAX_RECOVERY_ATTEMPTS)
        {
            Serial.printf("[SYSTEMSTATES] WARNING: %lu recovery attempts failed\n",
                          (unsigned long)recoveryAttemptCount);
            Serial.println("[SYSTEMSTATES] Entering extended failure mode");

            /* Increase the error count to trigger lockout sooner */
            SystemStates_ErrorCount++;
        }

        /* DON'T clear errors yet - first check if communication is working */
        communicationRestored = false;

        /* Check if we're receiving valid data now */
        if (Modbus_GetDataValid(&dataValid) == MODBUS_STATUS_OK)
        {
            if (dataValid == 1U)
            {
                /* We got valid data! Communication is working */
                communicationRestored = true;
                Serial.println("[SYSTEMSTATES] Valid data received - communication restored!");
            }
        }

       
        /* If communication is truly restored, attempt full recovery */
        if (communicationRestored == true)
        {
            /* NOW we can clear errors */
            SystemStates__ClearErrors();

            /* Double-check all diagnostics */
            if (SystemStates__DiagnosticStatus() == true)
            {
                Serial.println("[SYSTEMSTATES] Recovery successful!");
                failureStartTime = 0U;
                recoveryAttemptCount = 0U;
                SystemStates__SetState(SYSTEMSTATES_NORMAL);
            }
            else
            {
                Serial.println("[SYSTEMSTATES] Communication restored but other errors persist");
                /* Don't reset timer - will retry on next cycle */
            }
        }
        else
        {
            /* Communication still broken */
            Serial.println("[SYSTEMSTATES] Recovery failed - communication still broken");

            /* Check if we've exceeded maximum errors */
            if (SystemStates_ErrorCount >= SYSTEMSTATES_MAX_ERROR_COUNT)
            {
                Serial.println("[SYSTEMSTATES] ===================================");
                Serial.println("[SYSTEMSTATES] CRITICAL FAILURE - LOCKOUT MODE");
                Serial.println("[SYSTEMSTATES] Too many errors detected");
                Serial.println("[SYSTEMSTATES] Manual intervention required:");
                Serial.println("[SYSTEMSTATES]   1. Check RS485 connections");
                Serial.println("[SYSTEMSTATES]   2. Check weighing machine power");
                Serial.println("[SYSTEMSTATES]   3. Send 'RESET' command to clear");
                Serial.println("[SYSTEMSTATES] ===================================");

                /* Enter permanent lockout - set timer to special value */
                failureStartTime = UINT32_MAX;
                recoveryAttemptCount = 0U;

                /* Don't attempt further recovery */
                return;
            }

            /* Not at max errors yet - reset timer to allow proper delay before next attempt */
            failureStartTime = millis();
        }
    }
}

/**
 * @brief Check communication status
 * @details Checks RS232 and RS485 communication for errors
 * @return true if communication is healthy, false if error
 */
static bool SystemStates_CheckCommunication(void)
{
    /* Check RS485 communication */
    if (App__IsRS485CommError() == true)
    {
        Serial.println("[SYSTEMSTATES] RS485 communication error detected");
        return false;
    }

    return true;
}
/**
 * @brief Check power supply status
 * @details Checks RS232 and RS485 power supplies for errors
 * @return true if power supplies are healthy, false if error
 */
static bool SystemStates_CheckPowerSupply(void)
{
    /* No isolated power supply monitoring in this hardware revision */
    return true;
}

/**
 * @brief Update error status
 * @details Records error code and increments error counter
 * @param error Error code to record
 */
static void SystemStates_UpdateErrorStatus(SystemStates_ErrorCode_t error)
{
    SystemStates_LastError = error;
    SystemStates_ErrorCount++;

    Serial.printf("[SYSTEMSTATES] Error recorded: %d (Total: %lu)\n",
                  (int)error, (unsigned long)SystemStates_ErrorCount);
}