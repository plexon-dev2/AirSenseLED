/**
 * @file ThresholdSettings.h
 * @brief Threshold configuration for air quality parameters
 * @details Stores min/max alert thresholds and increment steps.
 *          Limits are selected at runtime based on sensor model set via
 *          Thresholds__SetSensorModel() before Thresholds__Init().
 *
 *          ZPHS01B (9-param, 26-byte frame, start=0xFF):
 *          ┌──────────────┬──────────────────┬─────────────────────────────┐
 *          │ CO2          │ 0    ~ 5000      │ ppm                         │
 *          │ PM2.5        │ 0    ~ 1000      │ µg/m³                       │
 *          │ CH2O         │ 0    ~ 6250      │ µg/m³ (raw × 0.001 mg/m³)  │
 *          │ TVOC         │ 0    ~ 3         │ grade                       │
 *          │ Temperature  │ -20  ~ 65        │ °C                          │
 *          │ Humidity     │ 0    ~ 100       │ %RH                         │
 *          │ O3           │ 0    ~ 10        │ ppm NATIVE (raw × 0.01)     │
 *          │ CO           │ 0    ~ 500       │ ppm NATIVE (raw × 0.1)      │
 *          │ NO2          │ 0.1  ~ 10        │ ppm NATIVE (raw × 0.01)     │
 *          └──────────────┴──────────────────┴─────────────────────────────┘
 *
 *          ZPHS01C (5-param, 14-byte frame, start=0x16):
 *          ┌──────────────┬──────────────────┬─────────────────────────────┐
 *          │ CO2          │ 400  ~ 5000      │ ppm                         │
 *          │ PM2.5        │ 0    ~ 1000      │ µg/m³                       │
 *          │ CH2O         │ 0    ~ 2000      │ µg/m³                       │
 *          │ TVOC         │ 0    ~ 3         │ grade                       │
 *          │ Temperature  │ 0    ~ 65        │ °C                          │
 *          │ Humidity     │ 0    ~ 100       │ %RH                         │
 *          │ O3/CO/NO2    │ —                │ not native, frame = 0       │
 *          └──────────────┴──────────────────┴─────────────────────────────┘
 *
 * @version 2.0.0 — dual-model support; backward-compatible signatures
 */

#ifndef THRESHOLD_SETTINGS_H
#define THRESHOLD_SETTINGS_H

#include <Arduino.h>

/*==============================================================================
 *   MODEL-SPECIFIC LIMIT MACROS
 *   The Init() function reads s_sensor_model and writes the correct value
 *   into the thresholds struct. Increase/DecreaseMin/Max use the same
 *   runtime model to pick the correct hard stop.
 *============================================================================*/

/* ── Formaldehyde / CH2O ─────────────────────────────────────────────────────
 *  ZPHS01B: raw × 0.001 mg/m³, range 0~6.250 mg/m³ → stored as µg/m³
 *  ZPHS01C: raw × 1 µg/m³,     range 0~2000 µg/m³
 * ──────────────────────────────────────────────────────────────────────── */
#define FORMALDEHYDE_MIN_LIMIT_01B   0.0f
#define FORMALDEHYDE_MAX_LIMIT_01B   6250.0f
#define FORMALDEHYDE_MIN_LIMIT_01C   0.0f
#define FORMALDEHYDE_MAX_LIMIT_01C   2000.0f
#define FORMALDEHYDE_STEP            10.0f
#define FORMALDEHYDE_DEFAULT_MIN     0.0f
#define FORMALDEHYDE_DEFAULT_MAX     100.0f   /* WHO guideline ~100 µg/m³  */

/* ── PM2.5 (µg/m³) — identical on both models ──────────────────────────── */
#define PM25_MIN_LIMIT               0.0f
#define PM25_MAX_LIMIT               1000.0f
#define PM25_STEP                    5.0f
#define PM25_DEFAULT_MIN             0.0f
#define PM25_DEFAULT_MAX             35.0f    /* WHO 24h guideline          */

/* ── Carbon Dioxide / CO2 (ppm) ─────────────────────────────────────────────
 *  ZPHS01B: 0 ~ 5000 ppm  (sensor range starts at 0)
 *  ZPHS01C: 400 ~ 5000 ppm (datasheet min 400)
 * ──────────────────────────────────────────────────────────────────────── */
#define CO2_MIN_LIMIT_01B            0.0f
#define CO2_MIN_LIMIT_01C            400.0f
#define CO2_MAX_LIMIT                5000.0f
#define CO2_STEP                     50.0f
#define CO2_DEFAULT_MIN              400.0f
#define CO2_DEFAULT_MAX              1000.0f  /* ASHRAE comfort guideline   */

/* ── TVOC (grade 0–3) — identical on both models ───────────────────────── */
#define TVOC_MIN_LIMIT               0.0f
#define TVOC_MAX_LIMIT               3.0f
#define TVOC_STEP                    1.0f     /* integer grade steps only   */
#define TVOC_DEFAULT_MIN             0.0f
#define TVOC_DEFAULT_MAX             2.0f     /* alert at grade 2 (medium)  */

/* ── Temperature (°C) ───────────────────────────────────────────────────────
 *  ZPHS01B: -20 ~ 65 °C  (supports below-zero)
 *  ZPHS01C:   0 ~ 65 °C
 * ──────────────────────────────────────────────────────────────────────── */
#define TEMP_MIN_LIMIT_01B           -20.0f
#define TEMP_MIN_LIMIT_01C           0.0f
#define TEMP_MAX_LIMIT               65.0f
#define TEMP_STEP                    0.5f
#define TEMP_DEFAULT_MIN             15.0f
#define TEMP_DEFAULT_MAX             30.0f

/* ── Humidity (%RH) — identical on both models ──────────────────────────── */
#define HUMIDITY_MIN_LIMIT           0.0f
#define HUMIDITY_MAX_LIMIT           100.0f
#define HUMIDITY_STEP                5.0f
#define HUMIDITY_DEFAULT_MIN         30.0f
#define HUMIDITY_DEFAULT_MAX         70.0f

/* ── Carbon Monoxide / CO (ppm) ─────────────────────────────────────────────
 *  ZPHS01B: NATIVE electrochemical, 0~500 ppm (raw × 0.1)
 *  ZPHS01C: not native — frame field is always 0
 * ──────────────────────────────────────────────────────────────────────── */
#define CO_MIN_LIMIT                 0.0f
#define CO_MAX_LIMIT_01B             500.0f   /* ZPHS01B datasheet max      */
#define CO_MAX_LIMIT_01C             1000.0f  /* placeholder for non-native */
#define CO_STEP                      1.0f
#define CO_DEFAULT_MIN               0.0f
#define CO_DEFAULT_MAX               50.0f    /* OSHA permissible limit     */

/* ── Nitrogen Dioxide / NO2 (ppm) ───────────────────────────────────────────
 *  ZPHS01B: NATIVE electrochemical, 0.1~10 ppm (raw × 0.01), res 0.05 ppm
 *  ZPHS01C: not native — frame field is always 0
 * ──────────────────────────────────────────────────────────────────────── */
#define NO2_MIN_LIMIT                0.0f
#define NO2_MAX_LIMIT                10.0f
#define NO2_STEP                     0.05f    /* ZPHS01B resolution         */
#define NO2_DEFAULT_MIN              0.0f
#define NO2_DEFAULT_MAX              1.0f

/* ── Ozone / O3 (ppm) ───────────────────────────────────────────────────────
 *  ZPHS01B: NATIVE electrochemical, 0~10 ppm (raw × 0.01), res 0.01 ppm
 *  ZPHS01C: not native — frame field is always 0
 *  UNIT: ppm — previous code incorrectly used ppb.
 * ──────────────────────────────────────────────────────────────────────── */
#define O3_MIN_LIMIT                 0.0f
#define O3_MAX_LIMIT                 10.0f    /* ZPHS01B datasheet max      */
#define O3_STEP                      0.01f    /* matches sensor resolution  */
#define O3_DEFAULT_MIN               0.0f
#define O3_DEFAULT_MAX               0.1f     /* WHO 8h guideline           */

/*==============================================================================
 *                         DATA STRUCTURES
 *============================================================================*/

/**
 * @brief Sensor model — set once at boot before Thresholds__Init().
 */
typedef enum {
    SENSOR_MODEL_ZPHS01B = 0U,   /**< 9-param 26-byte frame  */
    SENSOR_MODEL_ZPHS01C = 1U    /**< 5-param 14-byte frame  */
} ThresholdSensorModel_t;

/**
 * @brief Settings parameter selector (matches display settings pages)
 */
typedef enum {
    SETTINGS_TEMPERATURE = 0,
    SETTINGS_HUMIDITY,
    SETTINGS_O3,
    SETTINGS_NO2,
    SETTINGS_FORMALDEHYDE,
    SETTINGS_PM25,
    SETTINGS_CO,
    SETTINGS_CO2,
    SETTINGS_TVOC
} SettingsParameter;

/**
 * @brief Threshold data for all 9 parameters
 */
typedef struct {
    float formaldehyde_min;
    float formaldehyde_max;
    float pm25_min;
    float pm25_max;
    float co_min;
    float co_max;
    float no2_min;
    float no2_max;
    float co2_min;
    float co2_max;
    float o3_min;
    float o3_max;
    float tvoc_min;
    float tvoc_max;
    float temperature_min;
    float temperature_max;
    float humidity_min;
    float humidity_max;
} ThresholdData;

/*==============================================================================
 *                         GLOBAL INSTANCES
 *============================================================================*/

extern ThresholdData     thresholds;
extern ThresholdData     tempThresholds;
extern SettingsParameter currentSettingsParam;

/*==============================================================================
 *                         FUNCTION PROTOTYPES
 *============================================================================*/

/**
 * @brief Set sensor model BEFORE calling Thresholds__Init().
 * @details Call in setup() after reading g_sensor_type from NVS, before
 *          Scheduler_ExecuteInitCore0(). This ensures Init() loads the
 *          correct datasheet limits for the connected sensor.
 *
 * Placement in AirSense_DualCore.ino (STEP 3 block):
 * @code
 *   g_sensor_type = prefs.getUInt(SENSOR_NVS_KEY_TYPE, SENSOR_TYPE_DEFAULT);
 *   g_screen_count = (g_sensor_type == SENSOR_9PARAM) ? ...
 *
 *   // ADD HERE — before Scheduler_ExecuteInitCore0():
 *   if (g_sensor_type == SENSOR_9PARAM)
 *       Thresholds__SetSensorModel(SENSOR_MODEL_ZPHS01B);
 *   else
 *       Thresholds__SetSensorModel(SENSOR_MODEL_ZPHS01C);
 * @endcode
 */
void Thresholds__SetSensorModel(ThresholdSensorModel_t model);

/**
 * @brief Get the currently active sensor model.
 */
ThresholdSensorModel_t Thresholds__GetSensorModel(void);

/**
 * @brief Get the hard min/max limits and step for a parameter.
 * @details Call from display layer to show correct slider range to the user.
 *          Returns model-appropriate values (e.g. CO2 min is 0 for ZPHS01B,
 *          400 for ZPHS01C).
 *
 * @param[in]  param         Parameter to query
 * @param[out] out_min_limit Lowest allowed value
 * @param[out] out_max_limit Highest allowed value
 * @param[out] out_step      Increment per button press
 */
void Thresholds__GetParamLimits(SettingsParameter  param,
                                 float             *out_min_limit,
                                 float             *out_max_limit,
                                 float             *out_step);

/* ── Existing API — signatures unchanged, safe for scheduler ── */
void  Thresholds__Init(void);
void  Thresholds__Handler(void);
void  Thresholds__StartEditing(SettingsParameter param);
void  Thresholds__Save(void);
void  Thresholds__Cancel(void);
bool  Thresholds__IncreaseMin(void);
bool  Thresholds__DecreaseMin(void);
bool  Thresholds__IncreaseMax(void);
bool  Thresholds__DecreaseMax(void);
float Thresholds__GetTempMin(void);
float Thresholds__GetTempMax(void);

#endif /* THRESHOLD_SETTINGS_H */