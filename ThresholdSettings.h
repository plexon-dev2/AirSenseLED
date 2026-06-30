/**
 * @file ThresholdSettings.h
 * @brief Threshold configuration for air quality parameters
 * @details Stores min/max alert thresholds and increment steps.
 *          Limits are selected at runtime based on sensor model set via
 *          Thresholds__SetSensorModel() before Thresholds__Init().
 *
 *          ZPHS01B (9-param, 26-byte frame, start=0xFF):
 *
 *            CO2         :  0    to 5000    ppm
 *            PM2.5       :  0    to 1000    ug/m3
 *            CH2O        :  0    to 6250    ug/m3 (raw * 0.001 mg/m3)
 *            TVOC        :  0    to 3       grade
 *            Temperature : -20   to 65      degC
 *            Humidity    :  0    to 100     %RH
 *            O3          :  0    to 10      ppm native (raw * 0.01)
 *            CO          :  0    to 500     ppm native (raw * 0.1)
 *            NO2         :  0.1  to 10      ppm native (raw * 0.01)
 *
 *          ZPHS01C (5-param, 14-byte frame, start=0x16):
 *
 *            CO2         :  400  to 5000    ppm
 *            PM2.5       :  0    to 1000    ug/m3
 *            CH2O        :  0    to 2000    ug/m3
 *            TVOC        :  0    to 3       grade
 *            Temperature :  0    to 65      degC
 *            Humidity    :  0    to 100     %RH
 *            O3/CO/NO2   :  not native -- frame field always 0
 *
 * @warning 'thresholds' and 'tempThresholds' are global structs written on
 *          Core 1 (Thresholds__Init, Thresholds__Save) and read from Core 0
 *          (App.cpp, LEDHMI__Handler via extern). No mutex protects them.
 *          Reads from Core 0 may see partially-written values during a Save()
 *          call. Full fix: expose accessor functions only and protect with a
 *          mutex. Current mitigation: Save() is only called from a UI event
 *          which is infrequent -- torn reads are unlikely but not impossible.
 *
 * @version 2.1.0
 *
 * v2.1.0 changes (review fixes):
 *   - Non-ASCII characters (box-drawing, degree, micro) replaced with ASCII.
 *   - FLOAT_EPSILON moved from .cpp to this header so callers can use it.
 *   - cross-core @warning added for thresholds / tempThresholds globals.
 *   - G2 note added: Thresholds__Save() does not persist to NVS yet --
 *     values are lost on reboot.
 */

#ifndef THRESHOLD_SETTINGS_H
#define THRESHOLD_SETTINGS_H

#include <Arduino.h>

/*==============================================================================
 *                          FLOAT COMPARISON EPSILON
 *  Used by Increase/Decrease functions to guard against float rounding.
 *  Moved from .cpp so callers can use the same value for their own comparisons.
 *============================================================================*/
#define FLOAT_EPSILON               (0.0001f)

/*==============================================================================
 *   MODEL-SPECIFIC LIMIT MACROS
 *============================================================================*/

/* -- Formaldehyde / CH2O --------------------------------------------------
 *  ZPHS01B: raw * 0.001 mg/m3, range 0-6.250 mg/m3 -> stored as ug/m3
 *  ZPHS01C: raw * 1 ug/m3,     range 0-2000 ug/m3                      */
#define FORMALDEHYDE_MIN_LIMIT_01B   0.0f
#define FORMALDEHYDE_MAX_LIMIT_01B   6250.0f
#define FORMALDEHYDE_MIN_LIMIT_01C   0.0f
#define FORMALDEHYDE_MAX_LIMIT_01C   2000.0f
#define FORMALDEHYDE_STEP            10.0f
#define FORMALDEHYDE_DEFAULT_MIN     0.0f
#define FORMALDEHYDE_DEFAULT_MAX     100.0f   /* WHO guideline ~100 ug/m3  */

/* -- PM2.5 (ug/m3) -- identical on both models -------------------------- */
#define PM25_MIN_LIMIT               0.0f
#define PM25_MAX_LIMIT               1000.0f
#define PM25_STEP                    5.0f
#define PM25_DEFAULT_MIN             0.0f
#define PM25_DEFAULT_MAX             35.0f    /* WHO 24h guideline         */

/* -- Carbon Dioxide / CO2 (ppm) ------------------------------------------
 *  ZPHS01B: 0 to 5000 ppm  (sensor range starts at 0)
 *  ZPHS01C: 400 to 5000 ppm (datasheet min 400)                         */
#define CO2_MIN_LIMIT_01B            0.0f
#define CO2_MIN_LIMIT_01C            400.0f
#define CO2_MAX_LIMIT                5000.0f
#define CO2_STEP                     50.0f
#define CO2_DEFAULT_MIN              400.0f
#define CO2_DEFAULT_MAX              1000.0f  /* ASHRAE comfort guideline  */

/* -- TVOC (grade 0-3) -- identical on both models ----------------------- */
#define TVOC_MIN_LIMIT               0.0f
#define TVOC_MAX_LIMIT               3.0f
#define TVOC_STEP                    1.0f     /* integer grade steps only  */
#define TVOC_DEFAULT_MIN             0.0f
#define TVOC_DEFAULT_MAX             2.0f     /* alert at grade 2 (medium) */

/* -- Temperature (degC) --------------------------------------------------
 *  ZPHS01B: -20 to 65 degC  (supports below-zero)
 *  ZPHS01C:   0 to 65 degC                                               */
#define TEMP_MIN_LIMIT_01B           -20.0f
#define TEMP_MIN_LIMIT_01C           0.0f
#define TEMP_MAX_LIMIT               65.0f
#define TEMP_STEP                    0.5f
#define TEMP_DEFAULT_MIN             15.0f
#define TEMP_DEFAULT_MAX             30.0f

/* -- Humidity (%RH) -- identical on both models ------------------------- */
#define HUMIDITY_MIN_LIMIT           0.0f
#define HUMIDITY_MAX_LIMIT           100.0f
#define HUMIDITY_STEP                5.0f
#define HUMIDITY_DEFAULT_MIN         30.0f
#define HUMIDITY_DEFAULT_MAX         70.0f

/* -- Carbon Monoxide / CO (ppm) ------------------------------------------
 *  ZPHS01B: NATIVE electrochemical, 0-500 ppm (raw * 0.1)
 *  ZPHS01C: not native -- frame field is always 0                        */
#define CO_MIN_LIMIT                 0.0f
#define CO_MAX_LIMIT_01B             500.0f   /* ZPHS01B datasheet max     */
#define CO_MAX_LIMIT_01C             1000.0f  /* placeholder for non-native */
#define CO_STEP                      1.0f
#define CO_DEFAULT_MIN               0.0f
#define CO_DEFAULT_MAX               50.0f    /* OSHA permissible limit    */

/* -- Nitrogen Dioxide / NO2 (ppm) ----------------------------------------
 *  ZPHS01B: NATIVE electrochemical, 0.1-10 ppm (raw * 0.01), res 0.05 ppm
 *  ZPHS01C: not native -- frame field is always 0                        */
#define NO2_MIN_LIMIT                0.0f
#define NO2_MAX_LIMIT                10.0f
#define NO2_STEP                     0.05f    /* ZPHS01B resolution        */
#define NO2_DEFAULT_MIN              0.0f
#define NO2_DEFAULT_MAX              1.0f

/* -- Ozone / O3 (ppm) ----------------------------------------------------
 *  ZPHS01B: NATIVE electrochemical, 0-10 ppm (raw * 0.01), res 0.01 ppm
 *  ZPHS01C: not native -- frame field is always 0
 *  UNIT: ppm -- previous code incorrectly used ppb.                      */
#define O3_MIN_LIMIT                 0.0f
#define O3_MAX_LIMIT                 10.0f    /* ZPHS01B datasheet max     */
#define O3_STEP                      0.01f    /* matches sensor resolution */
#define O3_DEFAULT_MIN               0.0f
#define O3_DEFAULT_MAX               0.1f     /* WHO 8h guideline          */

/*==============================================================================
 *                         DATA STRUCTURES
 *============================================================================*/

/** @brief Sensor model -- set once at boot before Thresholds__Init(). */
typedef enum
{
    SENSOR_MODEL_ZPHS01B = 0U,   /**< 9-param 26-byte frame */
    SENSOR_MODEL_ZPHS01C = 1U    /**< 5-param 14-byte frame */
} ThresholdSensorModel_t;

/** @brief Settings parameter selector (matches display settings pages) */
typedef enum
{
    SETTINGS_TEMPERATURE  = 0,
    SETTINGS_HUMIDITY,
    SETTINGS_O3,
    SETTINGS_NO2,
    SETTINGS_FORMALDEHYDE,
    SETTINGS_PM25,
    SETTINGS_CO,
    SETTINGS_CO2,
    SETTINGS_TVOC
} SettingsParameter;

/** @brief Threshold data for all 9 parameters */
typedef struct
{
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
 *  @warning See file-level @warning re: cross-core access without mutex.
 *============================================================================*/
extern ThresholdData     thresholds;
extern ThresholdData     tempThresholds;
extern SettingsParameter currentSettingsParam;

/*==============================================================================
 *                         FUNCTION PROTOTYPES
 *============================================================================*/

/**
 * @brief Set sensor model BEFORE calling Thresholds__Init().
 * @details Call in setup() after reading sensor type from NVS, before
 *          Scheduler_ExecuteInitCore1(). Ensures Init() loads the correct
 *          datasheet limits for the connected sensor.
 */
void Thresholds__SetSensorModel(ThresholdSensorModel_t model);

/** @brief Get the currently active sensor model. */
ThresholdSensorModel_t Thresholds__GetSensorModel(void);

/**
 * @brief Get the hard min/max limits and step for a parameter.
 * @param[in]  param         Parameter to query.
 * @param[out] out_min_limit Lowest allowed value.
 * @param[out] out_max_limit Highest allowed value.
 * @param[out] out_step      Increment per button press.
 */
void Thresholds__GetParamLimits(SettingsParameter  param,
                                 float             *out_min_limit,
                                 float             *out_max_limit,
                                 float             *out_step);

/* -- Scheduler interface -- */
void  Thresholds__Init(void);

/**
 * @brief Periodic handler -- currently a no-op.
 * @note  Reserved for future NVS auto-save feature.
 *        @warning Thresholds__Save() does NOT persist to NVS -- values
 *        are lost on reboot. NVS save is a planned future addition.
 */
void  Thresholds__Handler(void);

void  Thresholds__StartEditing(SettingsParameter param);

/**
 * @brief Apply tempThresholds to live thresholds.
 * @note  Does NOT save to NVS -- values are lost on reboot.
 *        NVS persistence is a planned future addition.
 */
void  Thresholds__Save(void);

void  Thresholds__Cancel(void);
bool  Thresholds__IncreaseMin(void);
bool  Thresholds__DecreaseMin(void);
bool  Thresholds__IncreaseMax(void);
bool  Thresholds__DecreaseMax(void);
float Thresholds__GetTempMin(void);
float Thresholds__GetTempMax(void);

#endif /* THRESHOLD_SETTINGS_H */