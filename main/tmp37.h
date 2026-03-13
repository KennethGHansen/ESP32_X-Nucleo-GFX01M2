#ifndef TMP37_H
#define TMP37_H

/*
    TMP37 driver for ESP32-S3 using ESP-IDF.

    This is a "port" of my STM32 approach:
      - STM32: HAL_ADC_Start -> HAL_ADC_PollForConversion -> HAL_ADC_GetValue
      - ESP32: adc_oneshot_read() returns a raw sample immediately (oneshot mode)

    Key differences on ESP32:
      1) ADC accuracy varies a lot chip-to-chip (non-linearity + Vref differences)
         so ESP-IDF provides an ADC calibration driver to convert raw -> millivolts.
      2) You don't "start" ADC like STM32; you configure once and read when needed.

    ESP-IDF oneshot ADC docs show the typical API flow:
      - adc_oneshot_new_unit()
      - adc_oneshot_config_channel()
      - adc_oneshot_read()
    [1](https://api.github.com/repos/reservoirprotocol/core)[2](https://huggingface.co/ggerganov/whisper.cpp/tree/main)

    ESP-IDF calibration docs show:
      - adc_cali_create_scheme_curve_fitting() or line fitting
      - adc_cali_raw_to_voltage()
    [3](https://github.com/KennethGHansen/HC-SR04_STM32F446RE/tree/main/Drivers)[4](https://www.uwe-sieber.de/usbtreeview_e.html)
*/

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

/*
    TMP37 physics / transfer function:

    - TMP37 scale: 20 mV / °C
    - TMP37 output at 25°C: ~500 mV

    Thus: Temperature (°C) ≈ Vout(mV) / 20

    That relationship is explicitly described in the TMP37 product info/datasheet.
    [5](https://www.howtogeek.com/devops/how-to-download-single-files-from-a-github-repository/)[6](https://careerkarma.com/blog/git-download-a-single-file-from-github/)
*/

/*
    TMP37_Handle (ESP32 version)

    On STM32 I stored:
      - ADC_HandleTypeDef* hadc
      - float vref

    On ESP32 we store:
      - which ADC unit (ADC_UNIT_1 is recommended on many ESP32 variants)
      - which ADC channel corresponds to the GPIO you wired
      - attenuation & bitwidth configuration (how ADC scales input voltage)
      - optional calibration handle (to convert raw to mV accurately)
      - your filter state (EMA and Kalman) so multiple sensors can coexist
*/
typedef struct
{
    // ----------------------------
    // USER CONFIG (set these)
    // ----------------------------

    /*
        ADC unit selection:
          - ADC_UNIT_1 or ADC_UNIT_2 depending on chip/pin usage.
        Many projects prefer ADC_UNIT_1 to avoid conflicts on some chips.
    */
    adc_unit_t unit;

    /*
        ADC channel:
        ESP32 ADC uses "channels" that map to specific ADC-capable GPIO pins.
        You choose the channel that corresponds to the pin you wired.
    */
    adc_channel_t channel;

    /*
        Attenuation controls measurable voltage range.
        Bigger attenuation allows reading higher voltages but may reduce resolution.

        Typical choices:
          - ADC_ATTEN_DB_0   : low range
          - ADC_ATTEN_DB_11  : widest range (often a good safe default)

        If TMP37 is powered at 3.3V, its output is typically under ~2V
        in many practical ranges, so DB_11 is usually safe.
    */
    adc_atten_t atten;

    /*
        Bit width of ADC output (resolution).
        ADC_BITWIDTH_DEFAULT lets ESP-IDF pick the best supported width.
    */
    adc_bitwidth_t bitwidth;

    /*
        Fallback reference voltage in millivolts:
        If calibration cannot be enabled (eFuse not available, scheme unsupported),
        we approximate voltage by:
            mv ≈ raw / max_raw * vref_mv

        NOTE: This fallback is less accurate than calibration.
    */
    int vref_mv;

    /*
        Simple oversampling/averaging count.
        Instead of taking 1 ADC reading, take N and average:
          - reduces random noise
          - improves stability
        Keep it small for responsiveness (e.g., 8–32).
    */
    uint16_t samples;

    // ----------------------------
    // FILTER TUNING (set these)
    // ----------------------------

    /*
        EMA alpha:
          - smaller => more smoothing, slower response
          - larger  => less smoothing, faster response

        I used 0.01 on STM32; we keep that as default if not set.
    */
    float ema_alpha;

    /*
        Kalman filter tuning:
          Q = process noise (how much you believe temperature can drift between samples)
          R = measurement noise (how noisy ADC readings are)
    */
    float kalman_q;
    float kalman_r;

    // ----------------------------
    // INTERNAL STATE (driver-managed)
    // ----------------------------
    adc_oneshot_unit_handle_t oneshot;

    adc_cali_handle_t cali;
    bool cali_enabled;

    esp_err_t last_err; // last error seen by the driver

    // EMA state
    bool ema_initialized;
    float ema_filtered;

    // Kalman state
    bool k_initialized;
    float k_x;  // estimate
    float k_P;  // uncertainty
} TMP37_Handle;

/*
    Initialize the TMP37 driver.
    This configures the ESP32 ADC one-shot unit + channel and tries to enable calibration.
*/
void TMP37_Init(TMP37_Handle *htmp);

/*
    Read temperature with EMA smoothing.
*/
float TMP37_ReadFiltered(TMP37_Handle *htmp);

/*
    Read temperature with 1D Kalman filtering.
*/
float TMP37_ReadFilteredKalman(TMP37_Handle *htmp);

/*
    Clean up resources (recommended).
    On STM32 you usually don't need to "deinit" because HAL is static.
    On ESP-IDF the oneshot unit allocates resources, so it’s nice to free them.
*/
void TMP37_Deinit(TMP37_Handle *htmp);

#endif // TMP37_H