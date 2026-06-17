// /**
//  * @file ThresholdSettings.cpp
//  * @brief Implementation of threshold management functions
//  * @version 2.0.0 — dual-model support (ZPHS01B / ZPHS01C)
//  *
//  * CHANGES vs v1.x:
//  *   + Thresholds__SetSensorModel() — call once at boot before Init()
//  *   + Thresholds__GetSensorModel() — read active model from any module
//  *   + Thresholds__GetParamLimits() — display layer reads correct slider range
//  *   ~ Thresholds__Init()           — now branches on s_sensor_model for limits
//  *   ~ Thresholds__IncreaseMin/Max, DecreaseMin/Max — use model-aware limits
//  *   = All other functions          — UNCHANGED
//  */

// #include "ThresholdSettings.h"

// /*==============================================================================
//  *                         GLOBAL VARIABLES
//  *============================================================================*/

// ThresholdData     thresholds;
// ThresholdData     tempThresholds;
// SettingsParameter currentSettingsParam = SETTINGS_FORMALDEHYDE;

// /*==============================================================================
//  *                         STATIC (MODULE-PRIVATE) STATE
//  *============================================================================*/

// /** Active sensor model — set by Thresholds__SetSensorModel() before Init() */
// static ThresholdSensorModel_t s_sensor_model = SENSOR_MODEL_ZPHS01B;

// /*==============================================================================
//  *                  PRIVATE HELPER — model-aware limit lookup
//  *============================================================================*/

// /**
//  * @brief Fill min/max limit and step for the given parameter + active model.
//  *        Used by IncreaseMin, DecreaseMin, IncreaseMax, DecreaseMax so that
//  *        hard stops always match the connected sensor's datasheet.
//  */
// static void GetLimitsForParam(SettingsParameter param,
//                                float *out_min_limit,
//                                float *out_max_limit,
//                                float *out_step)
// {
//     bool is01B = (s_sensor_model == SENSOR_MODEL_ZPHS01B);

//     switch (param) {
//         case SETTINGS_FORMALDEHYDE:
//             *out_min_limit = is01B ? FORMALDEHYDE_MIN_LIMIT_01B : FORMALDEHYDE_MIN_LIMIT_01C;
//             *out_max_limit = is01B ? FORMALDEHYDE_MAX_LIMIT_01B : FORMALDEHYDE_MAX_LIMIT_01C;
//             *out_step      = FORMALDEHYDE_STEP;
//             break;
//         case SETTINGS_PM25:
//             *out_min_limit = PM25_MIN_LIMIT;
//             *out_max_limit = PM25_MAX_LIMIT;
//             *out_step      = PM25_STEP;
//             break;
//         case SETTINGS_CO:
//             *out_min_limit = CO_MIN_LIMIT;
//             *out_max_limit = is01B ? CO_MAX_LIMIT_01B : CO_MAX_LIMIT_01C;
//             *out_step      = CO_STEP;
//             break;
//         case SETTINGS_NO2:
//             *out_min_limit = NO2_MIN_LIMIT;
//             *out_max_limit = NO2_MAX_LIMIT;
//             *out_step      = NO2_STEP;
//             break;
//         case SETTINGS_CO2:
//             *out_min_limit = is01B ? CO2_MIN_LIMIT_01B : CO2_MIN_LIMIT_01C;
//             *out_max_limit = CO2_MAX_LIMIT;
//             *out_step      = CO2_STEP;
//             break;
//         case SETTINGS_O3:
//             *out_min_limit = O3_MIN_LIMIT;
//             *out_max_limit = O3_MAX_LIMIT;
//             *out_step      = O3_STEP;
//             break;
//         case SETTINGS_TVOC:
//             *out_min_limit = TVOC_MIN_LIMIT;
//             *out_max_limit = TVOC_MAX_LIMIT;
//             *out_step      = TVOC_STEP;
//             break;
//         case SETTINGS_TEMPERATURE:
//             *out_min_limit = is01B ? TEMP_MIN_LIMIT_01B : TEMP_MIN_LIMIT_01C;
//             *out_max_limit = TEMP_MAX_LIMIT;
//             *out_step      = TEMP_STEP;
//             break;
//         case SETTINGS_HUMIDITY:
//             *out_min_limit = HUMIDITY_MIN_LIMIT;
//             *out_max_limit = HUMIDITY_MAX_LIMIT;
//             *out_step      = HUMIDITY_STEP;
//             break;
//         default:
//             *out_min_limit = 0.0f;
//             *out_max_limit = 0.0f;
//             *out_step      = 1.0f;
//             break;
//     }
// }

// /*==============================================================================
//  *                         NEW FUNCTIONS (v2.0.0)
//  *============================================================================*/

// void Thresholds__SetSensorModel(ThresholdSensorModel_t model)
// {
//     s_sensor_model = model;
//     Serial.printf("[THRESHOLDS] Model set to %s\n",
//                   (model == SENSOR_MODEL_ZPHS01B) ? "ZPHS01B" : "ZPHS01C");
// }

// ThresholdSensorModel_t Thresholds__GetSensorModel(void)
// {
//     return s_sensor_model;
// }

// void Thresholds__GetParamLimits(SettingsParameter  param,
//                                  float             *out_min_limit,
//                                  float             *out_max_limit,
//                                  float             *out_step)
// {
//     if (!out_min_limit || !out_max_limit || !out_step) return;
//     GetLimitsForParam(param, out_min_limit, out_max_limit, out_step);
// }

// /*==============================================================================
//  *              EXISTING FUNCTIONS — unchanged except Init() branching
//  *============================================================================*/

// void Thresholds__Init(void) {
//     /* Load model-appropriate defaults into the live thresholds struct.
//      * Parameters identical on both models get the same value either way. */

//     bool is01B = (s_sensor_model == SENSOR_MODEL_ZPHS01B);

//     thresholds.formaldehyde_min = FORMALDEHYDE_DEFAULT_MIN;
//     thresholds.formaldehyde_max = FORMALDEHYDE_DEFAULT_MAX;

//     thresholds.pm25_min = PM25_DEFAULT_MIN;
//     thresholds.pm25_max = PM25_DEFAULT_MAX;

//     thresholds.co_min = CO_DEFAULT_MIN;
//     thresholds.co_max = CO_DEFAULT_MAX;

//     thresholds.no2_min = NO2_DEFAULT_MIN;
//     thresholds.no2_max = NO2_DEFAULT_MAX;

//     /* CO2 min differs: ZPHS01B sensor starts at 0, ZPHS01C starts at 400 */
//     thresholds.co2_min = is01B ? CO2_MIN_LIMIT_01B : CO2_MIN_LIMIT_01C;
//     thresholds.co2_max = CO2_DEFAULT_MAX;

//     thresholds.o3_min = O3_DEFAULT_MIN;
//     thresholds.o3_max = O3_DEFAULT_MAX;

//     thresholds.tvoc_min = TVOC_DEFAULT_MIN;
//     thresholds.tvoc_max = TVOC_DEFAULT_MAX;

//     /* Temperature min differs: ZPHS01B supports -20 °C, ZPHS01C starts at 0 */
//     thresholds.temperature_min = is01B ? TEMP_MIN_LIMIT_01B : TEMP_MIN_LIMIT_01C;
//     thresholds.temperature_max = TEMP_DEFAULT_MAX;

//     thresholds.humidity_min = HUMIDITY_DEFAULT_MIN;
//     thresholds.humidity_max = HUMIDITY_DEFAULT_MAX;

//     Serial.printf("[THRESHOLDS] Init complete (%s)\n",
//                   is01B ? "ZPHS01B" : "ZPHS01C");
// }

// void Thresholds__Handler(void) {
//     // Reserved for future features (EEPROM auto-save, etc.)
//     // Currently no periodic tasks needed
// }

// void Thresholds__StartEditing(SettingsParameter param) {
//     currentSettingsParam = param;
//     tempThresholds = thresholds;
//     Serial.printf("[THRESHOLDS] Started editing parameter %d\n", param);
// }

// void Thresholds__Save(void) {
//     thresholds = tempThresholds;

//     Serial.println("[THRESHOLDS] Saved!");
//     Serial.printf("  CH2O: %.1f - %.1f µg/m³\n",  thresholds.formaldehyde_min, thresholds.formaldehyde_max);
//     Serial.printf("  PM2.5: %.0f - %.0f µg/m³\n",  thresholds.pm25_min,         thresholds.pm25_max);
//     Serial.printf("  CO: %.1f - %.1f ppm\n",         thresholds.co_min,           thresholds.co_max);
//     Serial.printf("  NO2: %.2f - %.2f ppm\n",        thresholds.no2_min,          thresholds.no2_max);
//     Serial.printf("  CO2: %.0f - %.0f ppm\n",        thresholds.co2_min,          thresholds.co2_max);
//     Serial.printf("  O3: %.2f - %.2f ppm\n",         thresholds.o3_min,           thresholds.o3_max);
//     Serial.printf("  TVOC: %.0f - %.0f grade\n",     thresholds.tvoc_min,         thresholds.tvoc_max);
//     Serial.printf("  Temp: %.1f - %.1f °C\n",        thresholds.temperature_min,  thresholds.temperature_max);
//     Serial.printf("  Humidity: %.0f - %.0f %%\n",    thresholds.humidity_min,     thresholds.humidity_max);
// }

// void Thresholds__Cancel(void) {
//     Serial.println("[THRESHOLDS] Changes cancelled");
// }

// bool Thresholds__IncreaseMin(void) {
//     float minLimit, maxLimit, step;
//     GetLimitsForParam(currentSettingsParam, &minLimit, &maxLimit, &step);

//     float *minPtr, *maxPtr;

//     switch (currentSettingsParam) {
//         case SETTINGS_FORMALDEHYDE: minPtr = &tempThresholds.formaldehyde_min; maxPtr = &tempThresholds.formaldehyde_max; break;
//         case SETTINGS_PM25:         minPtr = &tempThresholds.pm25_min;         maxPtr = &tempThresholds.pm25_max;         break;
//         case SETTINGS_CO:           minPtr = &tempThresholds.co_min;           maxPtr = &tempThresholds.co_max;           break;
//         case SETTINGS_NO2:          minPtr = &tempThresholds.no2_min;          maxPtr = &tempThresholds.no2_max;          break;
//         case SETTINGS_CO2:          minPtr = &tempThresholds.co2_min;          maxPtr = &tempThresholds.co2_max;          break;
//         case SETTINGS_O3:           minPtr = &tempThresholds.o3_min;           maxPtr = &tempThresholds.o3_max;           break;
//         case SETTINGS_TVOC:         minPtr = &tempThresholds.tvoc_min;         maxPtr = &tempThresholds.tvoc_max;         break;
//         case SETTINGS_TEMPERATURE:  minPtr = &tempThresholds.temperature_min;  maxPtr = &tempThresholds.temperature_max;  break;
//         case SETTINGS_HUMIDITY:     minPtr = &tempThresholds.humidity_min;     maxPtr = &tempThresholds.humidity_max;     break;
//         default: return false;
//     }

//     float newValue = *minPtr + step;
//     if (newValue >= *maxPtr)  { Serial.println("Min would exceed Max"); return false; }
//     if (newValue > maxLimit)  { Serial.println("At maximum limit");     return false; }

//     *minPtr = newValue;
//     Serial.printf("Min → %.2f\n", newValue);
//     return true;
// }

// bool Thresholds__DecreaseMin(void) {
//     float minLimit, maxLimit, step;
//     GetLimitsForParam(currentSettingsParam, &minLimit, &maxLimit, &step);

//     float *minPtr;

//     switch (currentSettingsParam) {
//         case SETTINGS_FORMALDEHYDE: minPtr = &tempThresholds.formaldehyde_min; break;
//         case SETTINGS_PM25:         minPtr = &tempThresholds.pm25_min;         break;
//         case SETTINGS_CO:           minPtr = &tempThresholds.co_min;           break;
//         case SETTINGS_NO2:          minPtr = &tempThresholds.no2_min;          break;
//         case SETTINGS_CO2:          minPtr = &tempThresholds.co2_min;          break;
//         case SETTINGS_O3:           minPtr = &tempThresholds.o3_min;           break;
//         case SETTINGS_TVOC:         minPtr = &tempThresholds.tvoc_min;         break;
//         case SETTINGS_TEMPERATURE:  minPtr = &tempThresholds.temperature_min;  break;
//         case SETTINGS_HUMIDITY:     minPtr = &tempThresholds.humidity_min;     break;
//         default: return false;
//     }

//     float newValue = *minPtr - step;
//     if (newValue < minLimit) { Serial.println("At minimum limit"); return false; }

//     *minPtr = newValue;
//     Serial.printf("Min → %.2f\n", newValue);
//     return true;
// }

// bool Thresholds__IncreaseMax(void) {
//     float minLimit, maxLimit, step;
//     GetLimitsForParam(currentSettingsParam, &minLimit, &maxLimit, &step);

//     float *maxPtr;

//     switch (currentSettingsParam) {
//         case SETTINGS_FORMALDEHYDE: maxPtr = &tempThresholds.formaldehyde_max; break;
//         case SETTINGS_PM25:         maxPtr = &tempThresholds.pm25_max;         break;
//         case SETTINGS_CO:           maxPtr = &tempThresholds.co_max;           break;
//         case SETTINGS_NO2:          maxPtr = &tempThresholds.no2_max;          break;
//         case SETTINGS_CO2:          maxPtr = &tempThresholds.co2_max;          break;
//         case SETTINGS_O3:           maxPtr = &tempThresholds.o3_max;           break;
//         case SETTINGS_TVOC:         maxPtr = &tempThresholds.tvoc_max;         break;
//         case SETTINGS_TEMPERATURE:  maxPtr = &tempThresholds.temperature_max;  break;
//         case SETTINGS_HUMIDITY:     maxPtr = &tempThresholds.humidity_max;     break;
//         default: return false;
//     }

//     float newValue = *maxPtr + step;
//     if (newValue > maxLimit) { Serial.println("At maximum limit"); return false; }

//     *maxPtr = newValue;
//     Serial.printf("Max → %.2f\n", newValue);
//     return true;
// }

// bool Thresholds__DecreaseMax(void) {
//     float minLimit, maxLimit, step;
//     GetLimitsForParam(currentSettingsParam, &minLimit, &maxLimit, &step);

//     float *minPtr, *maxPtr;

//     switch (currentSettingsParam) {
//         case SETTINGS_FORMALDEHYDE: minPtr = &tempThresholds.formaldehyde_min; maxPtr = &tempThresholds.formaldehyde_max; break;
//         case SETTINGS_PM25:         minPtr = &tempThresholds.pm25_min;         maxPtr = &tempThresholds.pm25_max;         break;
//         case SETTINGS_CO:           minPtr = &tempThresholds.co_min;           maxPtr = &tempThresholds.co_max;           break;
//         case SETTINGS_NO2:          minPtr = &tempThresholds.no2_min;          maxPtr = &tempThresholds.no2_max;          break;
//         case SETTINGS_CO2:          minPtr = &tempThresholds.co2_min;          maxPtr = &tempThresholds.co2_max;          break;
//         case SETTINGS_O3:           minPtr = &tempThresholds.o3_min;           maxPtr = &tempThresholds.o3_max;           break;
//         case SETTINGS_TVOC:         minPtr = &tempThresholds.tvoc_min;         maxPtr = &tempThresholds.tvoc_max;         break;
//         case SETTINGS_TEMPERATURE:  minPtr = &tempThresholds.temperature_min;  maxPtr = &tempThresholds.temperature_max;  break;
//         case SETTINGS_HUMIDITY:     minPtr = &tempThresholds.humidity_min;     maxPtr = &tempThresholds.humidity_max;     break;
//         default: return false;
//     }

//     float newValue = *maxPtr - step;
//     if (newValue <= *minPtr) { Serial.println("Max would be <= Min"); return false; }
//     if (newValue < minLimit) { Serial.println("At minimum limit");    return false; }

//     *maxPtr = newValue;
//     Serial.printf("Max → %.2f\n", newValue);
//     return true;
// }

// float Thresholds__GetTempMin(void) {
//     switch (currentSettingsParam) {
//         case SETTINGS_FORMALDEHYDE: return tempThresholds.formaldehyde_min;
//         case SETTINGS_PM25:         return tempThresholds.pm25_min;
//         case SETTINGS_CO:           return tempThresholds.co_min;
//         case SETTINGS_NO2:          return tempThresholds.no2_min;
//         case SETTINGS_CO2:          return tempThresholds.co2_min;
//         case SETTINGS_O3:           return tempThresholds.o3_min;
//         case SETTINGS_TVOC:         return tempThresholds.tvoc_min;
//         case SETTINGS_TEMPERATURE:  return tempThresholds.temperature_min;
//         case SETTINGS_HUMIDITY:     return tempThresholds.humidity_min;
//         default:                    return 0.0f;
//     }
// }

// float Thresholds__GetTempMax(void) {
//     switch (currentSettingsParam) {
//         case SETTINGS_FORMALDEHYDE: return tempThresholds.formaldehyde_max;
//         case SETTINGS_PM25:         return tempThresholds.pm25_max;
//         case SETTINGS_CO:           return tempThresholds.co_max;
//         case SETTINGS_NO2:          return tempThresholds.no2_max;
//         case SETTINGS_CO2:          return tempThresholds.co2_max;
//         case SETTINGS_O3:           return tempThresholds.o3_max;
//         case SETTINGS_TVOC:         return tempThresholds.tvoc_max;
//         case SETTINGS_TEMPERATURE:  return tempThresholds.temperature_max;
//         case SETTINGS_HUMIDITY:     return tempThresholds.humidity_max;
//         default:                    return 0.0f;
//     }
// }


/**
 * @file ThresholdSettings.cpp
 * @brief Implementation of threshold management functions
 * @version 2.0.0 — dual-model support (ZPHS01B / ZPHS01C)
 *
 * CHANGES vs v1.x:
 *   + Thresholds__SetSensorModel() — call once at boot before Init()
 *   + Thresholds__GetSensorModel() — read active model from any module
 *   + Thresholds__GetParamLimits() — display layer reads correct slider range
 *   ~ Thresholds__Init()           — now branches on s_sensor_model for limits
 *   ~ Thresholds__IncreaseMin/Max, DecreaseMin/Max — use model-aware limits
 *   = All other functions          — UNCHANGED
 *
 * FIX: floating-point epsilon added to all limit comparisons.
 *      O3_STEP (0.01f) and NO2_STEP (0.05f) accumulate floating-point
 *      error on each addition (e.g. 0.09f + 0.01f = 0.09999999f).
 *      Strict > / < comparisons against maxLimit/minLimit caused
 *      IncreaseMax/DecreaseMax to return false on nearly every press,
 *      requiring 9-10 presses before the value visibly changed.
 *      Solution: use (newValue > maxLimit + FLOAT_EPSILON) so that
 *      values within floating-point noise of the limit are allowed.
 */

#include "ThresholdSettings.h"

/*==============================================================================
 *                         EPSILON FOR FLOAT COMPARISONS
 *============================================================================*/
#define FLOAT_EPSILON  0.0001f

/*==============================================================================
 *                         GLOBAL VARIABLES
 *============================================================================*/

ThresholdData     thresholds;
ThresholdData     tempThresholds;
SettingsParameter currentSettingsParam = SETTINGS_FORMALDEHYDE;

/*==============================================================================
 *                         STATIC (MODULE-PRIVATE) STATE
 *============================================================================*/

/** Active sensor model — set by Thresholds__SetSensorModel() before Init() */
static ThresholdSensorModel_t s_sensor_model = SENSOR_MODEL_ZPHS01B;

/*==============================================================================
 *                  PRIVATE HELPER — model-aware limit lookup
 *============================================================================*/

/**
 * @brief Fill min/max limit and step for the given parameter + active model.
 *        Used by IncreaseMin, DecreaseMin, IncreaseMax, DecreaseMax so that
 *        hard stops always match the connected sensor's datasheet.
 */
static void GetLimitsForParam(SettingsParameter param,
                               float *out_min_limit,
                               float *out_max_limit,
                               float *out_step)
{
    bool is01B = (s_sensor_model == SENSOR_MODEL_ZPHS01B);

    switch (param) {
        case SETTINGS_FORMALDEHYDE:
            *out_min_limit = is01B ? FORMALDEHYDE_MIN_LIMIT_01B : FORMALDEHYDE_MIN_LIMIT_01C;
            *out_max_limit = is01B ? FORMALDEHYDE_MAX_LIMIT_01B : FORMALDEHYDE_MAX_LIMIT_01C;
            *out_step      = FORMALDEHYDE_STEP;
            break;
        case SETTINGS_PM25:
            *out_min_limit = PM25_MIN_LIMIT;
            *out_max_limit = PM25_MAX_LIMIT;
            *out_step      = PM25_STEP;
            break;
        case SETTINGS_CO:
            *out_min_limit = CO_MIN_LIMIT;
            *out_max_limit = is01B ? CO_MAX_LIMIT_01B : CO_MAX_LIMIT_01C;
            *out_step      = CO_STEP;
            break;
        case SETTINGS_NO2:
            *out_min_limit = NO2_MIN_LIMIT;
            *out_max_limit = NO2_MAX_LIMIT;
            *out_step      = NO2_STEP;
            break;
        case SETTINGS_CO2:
            *out_min_limit = is01B ? CO2_MIN_LIMIT_01B : CO2_MIN_LIMIT_01C;
            *out_max_limit = CO2_MAX_LIMIT;
            *out_step      = CO2_STEP;
            break;
        case SETTINGS_O3:
            *out_min_limit = O3_MIN_LIMIT;
            *out_max_limit = O3_MAX_LIMIT;
            *out_step      = O3_STEP;
            break;
        case SETTINGS_TVOC:
            *out_min_limit = TVOC_MIN_LIMIT;
            *out_max_limit = TVOC_MAX_LIMIT;
            *out_step      = TVOC_STEP;
            break;
        case SETTINGS_TEMPERATURE:
            *out_min_limit = is01B ? TEMP_MIN_LIMIT_01B : TEMP_MIN_LIMIT_01C;
            *out_max_limit = TEMP_MAX_LIMIT;
            *out_step      = TEMP_STEP;
            break;
        case SETTINGS_HUMIDITY:
            *out_min_limit = HUMIDITY_MIN_LIMIT;
            *out_max_limit = HUMIDITY_MAX_LIMIT;
            *out_step      = HUMIDITY_STEP;
            break;
        default:
            *out_min_limit = 0.0f;
            *out_max_limit = 0.0f;
            *out_step      = 1.0f;
            break;
    }
}

/*==============================================================================
 *                         NEW FUNCTIONS (v2.0.0)
 *============================================================================*/

void Thresholds__SetSensorModel(ThresholdSensorModel_t model)
{
    s_sensor_model = model;
    Serial.printf("[THRESHOLDS] Model set to %s\n",
                  (model == SENSOR_MODEL_ZPHS01B) ? "ZPHS01B" : "ZPHS01C");
}

ThresholdSensorModel_t Thresholds__GetSensorModel(void)
{
    return s_sensor_model;
}

void Thresholds__GetParamLimits(SettingsParameter  param,
                                 float             *out_min_limit,
                                 float             *out_max_limit,
                                 float             *out_step)
{
    if (!out_min_limit || !out_max_limit || !out_step) return;
    GetLimitsForParam(param, out_min_limit, out_max_limit, out_step);
}

/*==============================================================================
 *              EXISTING FUNCTIONS — unchanged except Init() branching
 *============================================================================*/

void Thresholds__Init(void) {
    /* Load model-appropriate defaults into the live thresholds struct.
     * Parameters identical on both models get the same value either way. */

    bool is01B = (s_sensor_model == SENSOR_MODEL_ZPHS01B);

    thresholds.formaldehyde_min = FORMALDEHYDE_DEFAULT_MIN;
    thresholds.formaldehyde_max = FORMALDEHYDE_DEFAULT_MAX;

    thresholds.pm25_min = PM25_DEFAULT_MIN;
    thresholds.pm25_max = PM25_DEFAULT_MAX;

    thresholds.co_min = CO_DEFAULT_MIN;
    thresholds.co_max = CO_DEFAULT_MAX;

    thresholds.no2_min = NO2_DEFAULT_MIN;
    thresholds.no2_max = NO2_DEFAULT_MAX;

    /* CO2 min differs: ZPHS01B sensor starts at 0, ZPHS01C starts at 400 */
    thresholds.co2_min = is01B ? CO2_MIN_LIMIT_01B : CO2_MIN_LIMIT_01C;
    thresholds.co2_max = CO2_DEFAULT_MAX;

    thresholds.o3_min = O3_DEFAULT_MIN;
    thresholds.o3_max = O3_DEFAULT_MAX;

    thresholds.tvoc_min = TVOC_DEFAULT_MIN;
    thresholds.tvoc_max = TVOC_DEFAULT_MAX;

    /* Temperature min differs: ZPHS01B supports -20 °C, ZPHS01C starts at 0 */
    thresholds.temperature_min = is01B ? TEMP_MIN_LIMIT_01B : TEMP_MIN_LIMIT_01C;
    thresholds.temperature_max = TEMP_DEFAULT_MAX;

    thresholds.humidity_min = HUMIDITY_DEFAULT_MIN;
    thresholds.humidity_max = HUMIDITY_DEFAULT_MAX;

    Serial.printf("[THRESHOLDS] Init complete (%s)\n",
                  is01B ? "ZPHS01B" : "ZPHS01C");
}

void Thresholds__Handler(void) {
    // Reserved for future features (EEPROM auto-save, etc.)
    // Currently no periodic tasks needed
}

void Thresholds__StartEditing(SettingsParameter param) {
    currentSettingsParam = param;
    tempThresholds = thresholds;
    Serial.printf("[THRESHOLDS] Started editing parameter %d\n", param);
}

void Thresholds__Save(void) {
    thresholds = tempThresholds;

    Serial.println("[THRESHOLDS] Saved!");
    Serial.printf("  CH2O: %.1f - %.1f µg/m³\n",  thresholds.formaldehyde_min, thresholds.formaldehyde_max);
    Serial.printf("  PM2.5: %.0f - %.0f µg/m³\n",  thresholds.pm25_min,         thresholds.pm25_max);
    Serial.printf("  CO: %.1f - %.1f ppm\n",         thresholds.co_min,           thresholds.co_max);
    Serial.printf("  NO2: %.2f - %.2f ppm\n",        thresholds.no2_min,          thresholds.no2_max);
    Serial.printf("  CO2: %.0f - %.0f ppm\n",        thresholds.co2_min,          thresholds.co2_max);
    Serial.printf("  O3: %.2f - %.2f ppm\n",         thresholds.o3_min,           thresholds.o3_max);
    Serial.printf("  TVOC: %.0f - %.0f grade\n",     thresholds.tvoc_min,         thresholds.tvoc_max);
    Serial.printf("  Temp: %.1f - %.1f °C\n",        thresholds.temperature_min,  thresholds.temperature_max);
    Serial.printf("  Humidity: %.0f - %.0f %%\n",    thresholds.humidity_min,     thresholds.humidity_max);
}

void Thresholds__Cancel(void) {
    Serial.println("[THRESHOLDS] Changes cancelled");
}

bool Thresholds__IncreaseMin(void) {
    float minLimit, maxLimit, step;
    GetLimitsForParam(currentSettingsParam, &minLimit, &maxLimit, &step);

    float *minPtr, *maxPtr;

    switch (currentSettingsParam) {
        case SETTINGS_FORMALDEHYDE: minPtr = &tempThresholds.formaldehyde_min; maxPtr = &tempThresholds.formaldehyde_max; break;
        case SETTINGS_PM25:         minPtr = &tempThresholds.pm25_min;         maxPtr = &tempThresholds.pm25_max;         break;
        case SETTINGS_CO:           minPtr = &tempThresholds.co_min;           maxPtr = &tempThresholds.co_max;           break;
        case SETTINGS_NO2:          minPtr = &tempThresholds.no2_min;          maxPtr = &tempThresholds.no2_max;          break;
        case SETTINGS_CO2:          minPtr = &tempThresholds.co2_min;          maxPtr = &tempThresholds.co2_max;          break;
        case SETTINGS_O3:           minPtr = &tempThresholds.o3_min;           maxPtr = &tempThresholds.o3_max;           break;
        case SETTINGS_TVOC:         minPtr = &tempThresholds.tvoc_min;         maxPtr = &tempThresholds.tvoc_max;         break;
        case SETTINGS_TEMPERATURE:  minPtr = &tempThresholds.temperature_min;  maxPtr = &tempThresholds.temperature_max;  break;
        case SETTINGS_HUMIDITY:     minPtr = &tempThresholds.humidity_min;     maxPtr = &tempThresholds.humidity_max;     break;
        default: return false;
    }

    float newValue = *minPtr + step;
    if (newValue >= *maxPtr - FLOAT_EPSILON) { Serial.println("Min would exceed Max"); return false; }  /* ← FIXED */
    if (newValue > maxLimit + FLOAT_EPSILON) { Serial.println("At maximum limit");     return false; }  /* ← FIXED */

    *minPtr = newValue;
    Serial.printf("Min → %.3f\n", newValue);
    return true;
}

bool Thresholds__DecreaseMin(void) {
    float minLimit, maxLimit, step;
    GetLimitsForParam(currentSettingsParam, &minLimit, &maxLimit, &step);

    float *minPtr;

    switch (currentSettingsParam) {
        case SETTINGS_FORMALDEHYDE: minPtr = &tempThresholds.formaldehyde_min; break;
        case SETTINGS_PM25:         minPtr = &tempThresholds.pm25_min;         break;
        case SETTINGS_CO:           minPtr = &tempThresholds.co_min;           break;
        case SETTINGS_NO2:          minPtr = &tempThresholds.no2_min;          break;
        case SETTINGS_CO2:          minPtr = &tempThresholds.co2_min;          break;
        case SETTINGS_O3:           minPtr = &tempThresholds.o3_min;           break;
        case SETTINGS_TVOC:         minPtr = &tempThresholds.tvoc_min;         break;
        case SETTINGS_TEMPERATURE:  minPtr = &tempThresholds.temperature_min;  break;
        case SETTINGS_HUMIDITY:     minPtr = &tempThresholds.humidity_min;     break;
        default: return false;
    }

    float newValue = *minPtr - step;
    if (newValue < minLimit - FLOAT_EPSILON) { Serial.println("At minimum limit"); return false; }  /* ← FIXED */

    *minPtr = newValue;
    Serial.printf("Min → %.3f\n", newValue);
    return true;
}

bool Thresholds__IncreaseMax(void) {
    float minLimit, maxLimit, step;
    GetLimitsForParam(currentSettingsParam, &minLimit, &maxLimit, &step);

    float *maxPtr;

    switch (currentSettingsParam) {
        case SETTINGS_FORMALDEHYDE: maxPtr = &tempThresholds.formaldehyde_max; break;
        case SETTINGS_PM25:         maxPtr = &tempThresholds.pm25_max;         break;
        case SETTINGS_CO:           maxPtr = &tempThresholds.co_max;           break;
        case SETTINGS_NO2:          maxPtr = &tempThresholds.no2_max;          break;
        case SETTINGS_CO2:          maxPtr = &tempThresholds.co2_max;          break;
        case SETTINGS_O3:           maxPtr = &tempThresholds.o3_max;           break;
        case SETTINGS_TVOC:         maxPtr = &tempThresholds.tvoc_max;         break;
        case SETTINGS_TEMPERATURE:  maxPtr = &tempThresholds.temperature_max;  break;
        case SETTINGS_HUMIDITY:     maxPtr = &tempThresholds.humidity_max;     break;
        default: return false;
    }

    float newValue = *maxPtr + step;
    if (newValue > maxLimit + FLOAT_EPSILON) { Serial.println("At maximum limit"); return false; }  /* ← FIXED */

    *maxPtr = newValue;
    Serial.printf("Max → %.3f\n", newValue);
    return true;
}

bool Thresholds__DecreaseMax(void) {
    float minLimit, maxLimit, step;
    GetLimitsForParam(currentSettingsParam, &minLimit, &maxLimit, &step);

    float *minPtr, *maxPtr;

    switch (currentSettingsParam) {
        case SETTINGS_FORMALDEHYDE: minPtr = &tempThresholds.formaldehyde_min; maxPtr = &tempThresholds.formaldehyde_max; break;
        case SETTINGS_PM25:         minPtr = &tempThresholds.pm25_min;         maxPtr = &tempThresholds.pm25_max;         break;
        case SETTINGS_CO:           minPtr = &tempThresholds.co_min;           maxPtr = &tempThresholds.co_max;           break;
        case SETTINGS_NO2:          minPtr = &tempThresholds.no2_min;          maxPtr = &tempThresholds.no2_max;          break;
        case SETTINGS_CO2:          minPtr = &tempThresholds.co2_min;          maxPtr = &tempThresholds.co2_max;          break;
        case SETTINGS_O3:           minPtr = &tempThresholds.o3_min;           maxPtr = &tempThresholds.o3_max;           break;
        case SETTINGS_TVOC:         minPtr = &tempThresholds.tvoc_min;         maxPtr = &tempThresholds.tvoc_max;         break;
        case SETTINGS_TEMPERATURE:  minPtr = &tempThresholds.temperature_min;  maxPtr = &tempThresholds.temperature_max;  break;
        case SETTINGS_HUMIDITY:     minPtr = &tempThresholds.humidity_min;     maxPtr = &tempThresholds.humidity_max;     break;
        default: return false;
    }

    float newValue = *maxPtr - step;
    if (newValue <= *minPtr + FLOAT_EPSILON) { Serial.println("Max would be <= Min"); return false; }  /* ← FIXED */
    if (newValue < minLimit - FLOAT_EPSILON) { Serial.println("At minimum limit");    return false; }  /* ← FIXED */

    *maxPtr = newValue;
    Serial.printf("Max → %.3f\n", newValue);
    return true;
}

float Thresholds__GetTempMin(void) {
    switch (currentSettingsParam) {
        case SETTINGS_FORMALDEHYDE: return tempThresholds.formaldehyde_min;
        case SETTINGS_PM25:         return tempThresholds.pm25_min;
        case SETTINGS_CO:           return tempThresholds.co_min;
        case SETTINGS_NO2:          return tempThresholds.no2_min;
        case SETTINGS_CO2:          return tempThresholds.co2_min;
        case SETTINGS_O3:           return tempThresholds.o3_min;
        case SETTINGS_TVOC:         return tempThresholds.tvoc_min;
        case SETTINGS_TEMPERATURE:  return tempThresholds.temperature_min;
        case SETTINGS_HUMIDITY:     return tempThresholds.humidity_min;
        default:                    return 0.0f;
    }
}

float Thresholds__GetTempMax(void) {
    switch (currentSettingsParam) {
        case SETTINGS_FORMALDEHYDE: return tempThresholds.formaldehyde_max;
        case SETTINGS_PM25:         return tempThresholds.pm25_max;
        case SETTINGS_CO:           return tempThresholds.co_max;
        case SETTINGS_NO2:          return tempThresholds.no2_max;
        case SETTINGS_CO2:          return tempThresholds.co2_max;
        case SETTINGS_O3:           return tempThresholds.o3_max;
        case SETTINGS_TVOC:         return tempThresholds.tvoc_max;
        case SETTINGS_TEMPERATURE:  return tempThresholds.temperature_max;
        case SETTINGS_HUMIDITY:     return tempThresholds.humidity_max;
        default:                    return 0.0f;
    }
}