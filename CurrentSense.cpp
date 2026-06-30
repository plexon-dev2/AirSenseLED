/**
 * @file CurrentSense.cpp
 * @brief Current Sense Module Implementation -- AirSense ESP32-S3
 *
 * ADC Strategy:
 *   - 0dB attenuation: full-scale ~950mV -- correct for <100mV shunt voltages
 *   - 64-sample oversampling: improves effective resolution by ~3 bits
 *   - esp_adc_cal: corrects ESP32-S3 ADC nonlinearity using factory eFuse values
 *   - 8-point rolling average: smooths reading across handler ticks
 *
 * Conversion:
 *   V_shunt (mV) = calibrated_adc_mv
 *   I (mA)       = V_shunt_mV / shunt_ohm
 *       Fan:    I = adc_mv / 0.1 = adc_mv * 10
 *       Sensor: I = adc_mv / 0.2 = adc_mv * 5
 *
 * ADC API NOTE:
 *   This module uses the legacy ESP-IDF ADC driver (driver/adc.h,
 *   esp_adc_cal.h) which is deprecated in IDF v5.0+.
 *   TODO: Migrate to esp_adc oneshot driver when Arduino-ESP32 core
 *   fully stabilises the new API (esp_adc/adc_oneshot.h).
 *   Migration path documented at the bottom of this file.
 *
 * @version 1.1.0
 *
 * v1.1.0 changes (review fixes):
 *   - CRITICAL: adc1_get_raw() return value checked -- was cast directly to
 *     uint32_t; a return of -1 (error) cast to uint32_t = 0xFFFFFFFF, inflating
 *     raw_sum by ~4.3 billion and producing garbage current readings.
 *     Bad samples are now skipped; average computed from valid samples only.
 *   - CurrentSense_ReadMA(): result clamped to UINT16_MAX before cast --
 *     prevents silent wrap-around if mV/shunt_ohm exceeds 65535.
 *   - Over-current warning: rising-edge detection added (s_fan_oc_prev,
 *     s_sensor_oc_prev) -- was printing every sample tick while OC persisted,
 *     flooding serial output at ~5 messages/second.
 *   - Serial.printf for OC warning replaced with SERIAL_PRINTF (mutex-safe).
 *   - s_json_buf access in GetJSON protected with taskENTER/EXIT_CRITICAL --
 *     was a cross-task race if GetJSON called from a different task.
 *   - esp_adc_cal calibration quality stored in s_cal_valid flag; logged as
 *     warning if eFuse calibration not available (accuracy degraded).
 *   - CurrentSense__IsInitialized() implemented.
 *   - s_sample_tick / s_mqtt_tick widened to uint16_t (were uint8_t --
 *     would silently wrap if CFG_CURRENT_MQTT_INTERVAL > 255).
 *   - Init log moved to CFG_DBG_PRINTF (consistent with runtime output).
 *   - snprintf("%s") in GetJSON replaced with strncpy for clarity.
 */

#include "CurrentSense.h"
#include "ProjectConfig.h"
#include "WiFiComm.h"
#include "BLEComm.h"
#include "AppMutex.h"    /* SERIAL_PRINTF -- mutex-safe serial output */

#include <Arduino.h>
#include <esp_adc_cal.h>
#include <driver/adc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <stdio.h>

/*==============================================================================
 *                          COMPILE-TIME VALIDATION
 *============================================================================*/

#if (CFG_CURRENT_ROLLING_AVG_DEPTH == 0U)
#error "CFG_CURRENT_ROLLING_AVG_DEPTH must be >= 1"
#endif

#if (CFG_CURRENT_ROLLING_AVG_DEPTH > 255U)
#error "CFG_CURRENT_ROLLING_AVG_DEPTH max 255 (uint8_t head/count fields)"
#endif

/* Overflow check: sum = depth * UINT16_MAX must fit in uint32_t */
#if ((CFG_CURRENT_ROLLING_AVG_DEPTH * 65535UL) > 0xFFFFFFFFUL)
#error "CFG_CURRENT_ROLLING_AVG_DEPTH too large -- rolling sum overflows uint32_t"
#endif

#if (CFG_CURRENT_OVERSAMPLE_COUNT == 0U)
#error "CFG_CURRENT_OVERSAMPLE_COUNT must be >= 1"
#endif

/*==============================================================================
 *                          PRIVATE TYPES
 *============================================================================*/

typedef struct
{
    uint16_t buf[CFG_CURRENT_ROLLING_AVG_DEPTH]; /**< Circular sample buffer  */
    uint8_t  head;                                /**< Next write position     */
    uint8_t  count;                               /**< Number of valid samples */
    uint32_t sum;                                 /**< Running sum of buf[]    */
} RollingAvg_t;

/*==============================================================================
 *                          PRIVATE VARIABLES
 *============================================================================*/

static esp_adc_cal_characteristics_t s_adc_chars_fan;
static esp_adc_cal_characteristics_t s_adc_chars_sensor;

static RollingAvg_t s_avg_fan    = { {0U}, 0U, 0U, 0UL };
static RollingAvg_t s_avg_sensor = { {0U}, 0U, 0U, 0UL };

static uint16_t s_fan_ma         = 0U;
static uint16_t s_sensor_ma      = 0U;
static bool     s_initialized    = false;

/**
 * @brief Calibration quality flag -- set in Init().
 * @details false = eFuse calibration unavailable; ADC accuracy degraded (~10%).
 *          Over-current thresholds are less reliable when false.
 */
static bool     s_cal_valid      = false;

/** @brief Widened to uint16_t -- prevents silent wrap if interval > 255 */
static uint16_t s_sample_tick    = 0U;
static uint16_t s_mqtt_tick      = 0U;

/**
 * @brief JSON output buffer -- written by Handler, read by GetJSON.
 *        Access protected with portMUX_TYPE critical section.
 */
static char          s_json_buf[96U];
static portMUX_TYPE  s_json_mux = portMUX_INITIALIZER_UNLOCKED;

/** @brief Over-current edge detection -- avoids serial flood on persistent OC */
static bool s_fan_oc_prev    = false;
static bool s_sensor_oc_prev = false;

/*==============================================================================
 *                          PRIVATE FUNCTIONS
 *============================================================================*/

/**
 * @brief Push a new sample into the rolling average circular buffer.
 * @param[in,out] avg   Rolling average instance.
 * @param[in]     value New sample value (mA).
 */
static void RollingAvg_Push(RollingAvg_t *avg, uint16_t value)
{
    if (avg->count == (uint8_t)CFG_CURRENT_ROLLING_AVG_DEPTH)
    {
        /* Buffer full -- remove oldest sample from sum */
        avg->sum -= (uint32_t)avg->buf[avg->head];
    }
    else
    {
        avg->count++;
    }

    avg->buf[avg->head] = value;
    avg->sum           += (uint32_t)value;
    avg->head           = (uint8_t)((avg->head + 1U) % (uint8_t)CFG_CURRENT_ROLLING_AVG_DEPTH);
}

/**
 * @brief Get rolling average value.
 * @param[in] avg Rolling average instance (read-only).
 * @return uint16_t Average of buffered samples, or 0 if no samples yet.
 */
static uint16_t RollingAvg_Get(const RollingAvg_t *avg)
{
    if (avg->count == 0U) { return 0U; }
    return (uint16_t)(avg->sum / (uint32_t)avg->count);
}

/**
 * @brief Read one ADC channel: oversample -> calibrate -> convert to mA.
 * @details 64-sample oversampling reduces quantisation noise.
 *          Bad samples (adc1_get_raw returns -1) are skipped.
 *          Result clamped to UINT16_MAX before cast to prevent wrap-around.
 *
 * @param[in] channel    ADC1 channel to read.
 * @param[in] chars      Calibration characteristics from esp_adc_cal_characterize.
 * @param[in] shunt_ohm  Shunt resistance in ohms (e.g. 0.1f or 0.2f).
 * @return uint16_t Calculated current in milliamps (mA).
 */
static uint16_t CurrentSense_ReadMA(adc1_channel_t channel,
                                     const esp_adc_cal_characteristics_t *chars,
                                     float shunt_ohm)
{
    uint32_t raw_sum     = 0UL;
    uint8_t  valid_count = 0U;
    uint8_t  i;

    for (i = 0U; i < (uint8_t)CFG_CURRENT_OVERSAMPLE_COUNT; i++)
    {
        int raw = adc1_get_raw(channel);
        if (raw >= 0)
        {
            raw_sum += (uint32_t)raw;
            valid_count++;
        }
        /* Bad sample (raw == -1): skip -- do not accumulate 0xFFFFFFFF */
    }

    if (valid_count == 0U) { return 0U; }  /* all samples invalid */

    uint32_t raw_avg    = raw_sum / (uint32_t)valid_count;
    uint32_t voltage_mv = esp_adc_cal_raw_to_voltage(raw_avg, chars);

    /* I (mA) = V (mV) / R (ohm) -- clamp to UINT16_MAX before cast */
    float    current_f  = (float)voltage_mv / shunt_ohm;
    uint32_t current_u  = (current_f > 65535.0f) ? 65535UL : (uint32_t)current_f;

    return (uint16_t)current_u;
}

/**
 * @brief Build JSON string into s_json_buf.
 *        Called from Handler under json_mux critical section.
 */
static void CurrentSense_BuildJSON(void)
{
    /* s_fan_ma and s_sensor_ma already updated this cycle -- no extra lock needed */
    snprintf(s_json_buf, sizeof(s_json_buf),
             "{\"fan_ma\":%u,\"sensor_ma\":%u,\"fan_oc\":%d,\"sensor_oc\":%d}",
             (unsigned)s_fan_ma,
             (unsigned)s_sensor_ma,
             (int)CurrentSense__IsFanOC(),
             (int)CurrentSense__IsSensorOC());
}

/*==============================================================================
 *                          PUBLIC FUNCTIONS
 *============================================================================*/

void CurrentSense__Init(void)
{
    /* Configure ADC resolution */
    adc1_config_width(ADC_WIDTH_BIT_12);

    /* Configure attenuation per channel */
    adc1_config_channel_atten(CFG_CURRENT_FAN_ADC_CHANNEL,    CFG_CURRENT_ADC_ATTEN);
    adc1_config_channel_atten(CFG_CURRENT_SENSOR_ADC_CHANNEL, CFG_CURRENT_ADC_ATTEN);

    /*--------------------------------------------------------------------------
     * Characterise ADC using eFuse two-point calibration.
     * Falls back to default Vref if eFuse not programmed.
     * s_cal_valid is set only when eFuse calibration is available.
     * When false, ADC accuracy is ~10% -- OC thresholds are less reliable.
     *------------------------------------------------------------------------*/
    esp_adc_cal_value_t cal_fan    = esp_adc_cal_characterize(
        ADC_UNIT_1, CFG_CURRENT_ADC_ATTEN, ADC_WIDTH_BIT_12,
        CFG_CURRENT_ADC_VREF_MV, &s_adc_chars_fan);

    esp_adc_cal_value_t cal_sensor = esp_adc_cal_characterize(
        ADC_UNIT_1, CFG_CURRENT_ADC_ATTEN, ADC_WIDTH_BIT_12,
        CFG_CURRENT_ADC_VREF_MV, &s_adc_chars_sensor);

    s_cal_valid = (cal_fan    == ESP_ADC_CAL_VAL_EFUSE_TP) &&
                  (cal_sensor == ESP_ADC_CAL_VAL_EFUSE_TP);

    s_initialized = true;

    CFG_DBG_PRINTF("[CSENSE] Init OK -- Fan:GPIO1(CH%d) Sensor:GPIO2(CH%d)\n",
                   (int)CFG_CURRENT_FAN_ADC_CHANNEL,
                   (int)CFG_CURRENT_SENSOR_ADC_CHANNEL);
    CFG_DBG_PRINTF("[CSENSE] Cal: Fan=%s Sensor=%s%s\n",
                   (cal_fan    == ESP_ADC_CAL_VAL_EFUSE_TP) ? "eFuse" : "default",
                   (cal_sensor == ESP_ADC_CAL_VAL_EFUSE_TP) ? "eFuse" : "default",
                   s_cal_valid ? "" : " -- WARNING: accuracy degraded");

    if (!s_cal_valid)
    {
        /* Always log calibration warning regardless of debug level */
        Serial.println("[CSENSE] WARNING: eFuse calibration unavailable -- "
                       "ADC accuracy ~10%, OC thresholds may be unreliable");
    }
}

void CurrentSense__Handler(void)
{
    if (!s_initialized) { return; }

    s_sample_tick++;
    s_mqtt_tick++;

    /*--------------------------------------------------------------------------
     * Sample ADC at CFG_CURRENT_SAMPLE_INTERVAL rate
     *------------------------------------------------------------------------*/
    if (s_sample_tick >= (uint16_t)CFG_CURRENT_SAMPLE_INTERVAL)
    {
        s_sample_tick = 0U;

        uint16_t fan_raw    = CurrentSense_ReadMA(CFG_CURRENT_FAN_ADC_CHANNEL,
                                                   &s_adc_chars_fan,
                                                   CFG_CURRENT_FAN_SHUNT_OHM);

        uint16_t sensor_raw = CurrentSense_ReadMA(CFG_CURRENT_SENSOR_ADC_CHANNEL,
                                                   &s_adc_chars_sensor,
                                                   CFG_CURRENT_SENSOR_SHUNT_OHM);

        RollingAvg_Push(&s_avg_fan,    fan_raw);
        RollingAvg_Push(&s_avg_sensor, sensor_raw);

        s_fan_ma    = RollingAvg_Get(&s_avg_fan);
        s_sensor_ma = RollingAvg_Get(&s_avg_sensor);

        /*----------------------------------------------------------------------
         * Over-current detection -- rising edge only to avoid serial flood.
         * Was printing every sample tick while OC persisted (~5x/second).
         *--------------------------------------------------------------------*/
        bool fan_oc_now    = CurrentSense__IsFanOC();
        bool sensor_oc_now = CurrentSense__IsSensorOC();

        if (fan_oc_now && !s_fan_oc_prev)
        {
            SERIAL_PRINTF("[CSENSE] *** FAN OVER-CURRENT: %umA (limit %umA) ***\n",
                          (unsigned)s_fan_ma, (unsigned)CFG_CURRENT_FAN_ALARM_MA);
        }
        if (sensor_oc_now && !s_sensor_oc_prev)
        {
            SERIAL_PRINTF("[CSENSE] *** SENSOR OVER-CURRENT: %umA (limit %umA) ***\n",
                          (unsigned)s_sensor_ma, (unsigned)CFG_CURRENT_SENSOR_ALARM_MA);
        }

        s_fan_oc_prev    = fan_oc_now;
        s_sensor_oc_prev = sensor_oc_now;
    }

    /*--------------------------------------------------------------------------
     * Publish to MQTT + BLE at CFG_CURRENT_MQTT_INTERVAL rate
     *------------------------------------------------------------------------*/
    if (s_mqtt_tick >= (uint16_t)CFG_CURRENT_MQTT_INTERVAL)
    {
        s_mqtt_tick = 0U;

        /* Build JSON under critical section -- GetJSON may be called concurrently */
        taskENTER_CRITICAL(&s_json_mux);
        CurrentSense_BuildJSON();
        taskEXIT_CRITICAL(&s_json_mux);

        WiFiComm__MQTTPublish(CFG_MQTT_TOPIC_CURRENT, s_json_buf);
        BLEComm__SendSensorData(s_json_buf);

        CFG_DBG_PRINTF("[CSENSE] Fan=%umA  Sensor=%umA  CalValid=%d\n",
                       (unsigned)s_fan_ma, (unsigned)s_sensor_ma, (int)s_cal_valid);
    }
}

uint16_t CurrentSense__GetFanMA(void)    { return s_fan_ma;    }
uint16_t CurrentSense__GetSensorMA(void) { return s_sensor_ma; }
bool     CurrentSense__IsInitialized(void) { return s_initialized; }

bool CurrentSense__IsFanOC(void)
{
    return (s_fan_ma >= (uint16_t)CFG_CURRENT_FAN_ALARM_MA);
}

bool CurrentSense__IsSensorOC(void)
{
    return (s_sensor_ma >= (uint16_t)CFG_CURRENT_SENSOR_ALARM_MA);
}

void CurrentSense__GetJSON(char *buf, uint16_t size)
{
    if ((buf == NULL) || (size == 0U)) { return; }

    /* Copy under critical section -- s_json_buf may be updated concurrently */
    taskENTER_CRITICAL(&s_json_mux);
    strncpy(buf, s_json_buf, (size_t)(size - 1U));
    taskEXIT_CRITICAL(&s_json_mux);

    buf[size - 1U] = '\0';  /* Guarantee null-termination */
}

/*==============================================================================
 *  TODO: Migration to IDF v5+ ADC oneshot API
 *  ============================================
 *  The legacy driver/adc.h API is deprecated in ESP-IDF v5.0.
 *  When Arduino-ESP32 core stabilises esp_adc, replace Init with:
 *
 *  #include <esp_adc/adc_oneshot.h>
 *  #include <esp_adc/adc_cali.h>
 *  #include <esp_adc/adc_cali_scheme.h>
 *
 *  adc_oneshot_unit_handle_t adc1_handle;
 *  adc_oneshot_unit_init_cfg_t init_cfg = { .unit_id = ADC_UNIT_1 };
 *  adc_oneshot_new_unit(&init_cfg, &adc1_handle);
 *
 *  adc_oneshot_chan_cfg_t ch_cfg = {
 *      .atten    = ADC_ATTEN_DB_0,
 *      .bitwidth = ADC_BITWIDTH_12
 *  };
 *  adc_oneshot_config_channel(adc1_handle, CFG_CURRENT_FAN_ADC_CHANNEL,    &ch_cfg);
 *  adc_oneshot_config_channel(adc1_handle, CFG_CURRENT_SENSOR_ADC_CHANNEL, &ch_cfg);
 *
 *  // Calibration:
 *  adc_cali_handle_t cali_handle;
 *  adc_cali_curve_fitting_config_t cali_cfg = {
 *      .unit_id  = ADC_UNIT_1,
 *      .atten    = ADC_ATTEN_DB_0,
 *      .bitwidth = ADC_BITWIDTH_12
 *  };
 *  adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle);
 *
 *  // Reading (replaces adc1_get_raw + esp_adc_cal_raw_to_voltage):
 *  int raw = 0;
 *  adc_oneshot_read(adc1_handle, channel, &raw);
 *  int voltage_mv = 0;
 *  adc_cali_raw_to_voltage(cali_handle, raw, &voltage_mv);
 *============================================================================*/