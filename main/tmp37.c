#include "tmp37.h"

#include "esp_log.h"
#include <stdlib.h>

/*
    Logging tag for ESP-IDF log output:
      ESP_LOGI(TAG, "...")  info
      ESP_LOGW(TAG, "...")  warning
      ESP_LOGE(TAG, "...")  error
*/
static const char *TAG = "TMP37";

/*
    -------------------------
    Calibration helper
    -------------------------

    ESP-IDF ADC calibration:
      - tries to compensate for real reference voltage & non-linear ADC behavior
      - when available, you can convert raw -> millivolts via adc_cali_raw_to_voltage()

    The docs describe creating a calibration "scheme" handle and then converting results.
    [3](https://github.com/KennethGHansen/HC-SR04_STM32F446RE/tree/main/Drivers)[4](https://www.uwe-sieber.de/usbtreeview_e.html)
*/
static void tmp37_try_enable_cali(TMP37_Handle *htmp)
{
    htmp->cali_enabled = false;
    htmp->cali = NULL;

    /*
        Curve fitting scheme:
        - available on some chips / configurations
        - may fail if required eFuse data isn't present
    */
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    {
        adc_cali_curve_fitting_config_t cal_cfg = {
            .unit_id  = htmp->unit,
            .chan     = htmp->channel,
            .atten    = htmp->atten,
            .bitwidth = htmp->bitwidth,
        };

        if (adc_cali_create_scheme_curve_fitting(&cal_cfg, &htmp->cali) == ESP_OK) {
            htmp->cali_enabled = true;
            return;
        }
    }
#endif

    /*
        Line fitting scheme:
        - alternative calibration approach
        - also may depend on eFuse support / target support
    */
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    {
        adc_cali_line_fitting_config_t cal_cfg = {
            .unit_id  = htmp->unit,
            .atten    = htmp->atten,
            .bitwidth = htmp->bitwidth,
        };

        if (adc_cali_create_scheme_line_fitting(&cal_cfg, &htmp->cali) == ESP_OK) {
            htmp->cali_enabled = true;
            return;
        }
    }
#endif
}

static void tmp37_delete_cali(TMP37_Handle *htmp)
{
    if (!htmp || !htmp->cali_enabled || !htmp->cali) return;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_delete_scheme_curve_fitting(htmp->cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_delete_scheme_line_fitting(htmp->cali);
#endif

    htmp->cali = NULL;
    htmp->cali_enabled = false;
}

/*
    -------------------------
    TMP37_Init
    -------------------------

    This replaces what my STM32 project did via:
      MX_ADC1_Init();
      then used HAL_ADC_Start/Read in the loop.

    ESP-IDF approach (oneshot mode):
      1) Allocate an ADC unit handle: adc_oneshot_new_unit()
      2) Configure the channel: adc_oneshot_config_channel()
      3) Read whenever you want: adc_oneshot_read()

    This flow is documented in the ESP-IDF oneshot ADC guide.
    [1](https://api.github.com/repos/reservoirprotocol/core)[2](https://huggingface.co/ggerganov/whisper.cpp/tree/main)
*/
void TMP37_Init(TMP37_Handle *htmp)
{
    if (!htmp) return;

    // Provide reasonable defaults if the user didn’t set fields.
    if (htmp->samples == 0) htmp->samples = 1;
    if (htmp->ema_alpha <= 0.0f) htmp->ema_alpha = 0.01f;
    if (htmp->kalman_q <= 0.0f) htmp->kalman_q = 0.001f;
    if (htmp->kalman_r <= 0.0f) htmp->kalman_r = 5.0f;
    if (htmp->vref_mv <= 0) htmp->vref_mv = 3300;

    // Reset filter states (so init starts “fresh”)
    htmp->ema_initialized = false;
    htmp->ema_filtered = 0.0f;
    htmp->k_initialized = false;
    htmp->k_x = 0.0f;
    htmp->k_P = 1.0f;

    htmp->last_err = ESP_OK;

    // 1) Allocate ADC unit
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = htmp->unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    htmp->last_err = adc_oneshot_new_unit(&unit_cfg, &htmp->oneshot);
    if (htmp->last_err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(htmp->last_err));
        return;
    }

    // 2) Configure the channel settings
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = htmp->atten,
        .bitwidth = htmp->bitwidth,
    };

    htmp->last_err = adc_oneshot_config_channel(htmp->oneshot, htmp->channel, &chan_cfg);
    if (htmp->last_err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(htmp->last_err));
        adc_oneshot_del_unit(htmp->oneshot);
        htmp->oneshot = NULL;
        return;
    }

    // 3) Try enabling calibration to convert raw -> mV more accurately
    tmp37_try_enable_cali(htmp);

    if (htmp->cali_enabled) {
        ESP_LOGI(TAG, "ADC calibration enabled");
    } else {
        ESP_LOGW(TAG, "ADC calibration not enabled; using fallback vref_mv=%d", htmp->vref_mv);
    }
}

/*
    Optional cleanup:
    - delete calibration handle
    - delete ADC oneshot unit

    This is mainly to show good practice and avoid resource leaks.
*/
void TMP37_Deinit(TMP37_Handle *htmp)
{
    if (!htmp) return;

    tmp37_delete_cali(htmp);

    if (htmp->oneshot) {
        adc_oneshot_del_unit(htmp->oneshot);
        htmp->oneshot = NULL;
    }
}

/*
    Read N samples from ADC and average them.

    Why average?
      ADC readings can be noisy due to:
        - power supply ripple
        - MCU digital switching noise
        - analog pin impedance / wiring
      Averaging reduces random noise at the cost of some response speed.
*/
static esp_err_t tmp37_read_raw_avg(TMP37_Handle *htmp, int *out_raw)
{
    if (!htmp || !out_raw || !htmp->oneshot) return ESP_ERR_INVALID_ARG;

    int64_t acc = 0;

    for (uint16_t i = 0; i < htmp->samples; i++) {
        int raw = 0;

        // Single conversion read
        esp_err_t err = adc_oneshot_read(htmp->oneshot, htmp->channel, &raw);
        if (err != ESP_OK) return err;

        acc += raw;
    }

    *out_raw = (int)(acc / htmp->samples);
    return ESP_OK;
}

/*
    Convert raw ADC to millivolts.

    If calibration is enabled:
        adc_cali_raw_to_voltage() gives a calibrated voltage in mV. [3](https://github.com/KennethGHansen/HC-SR04_STM32F446RE/tree/main/Drivers)[4](https://www.uwe-sieber.de/usbtreeview_e.html)
    Otherwise:
        use a rough fallback based on vref_mv and an assumed 12-bit max of 4095.

    NOTE:
      The fallback is “educational” and works okay for rough readings,
      but calibrated conversion is strongly preferred.
*/
static esp_err_t tmp37_raw_to_mv(TMP37_Handle *htmp, int raw, int *out_mv)
{
    if (!htmp || !out_mv) return ESP_ERR_INVALID_ARG;

    if (htmp->cali_enabled && htmp->cali) {
        return adc_cali_raw_to_voltage(htmp->cali, raw, out_mv);
    }

    // Fallback: treat as 12-bit scale (0..4095)
    const int max_raw = 4095;
    *out_mv = (raw * htmp->vref_mv) / max_raw;
    return ESP_OK;
}

/*
    TMP37 conversion:
      20 mV per °C => TempC = Vout_mV / 20

    This matches TMP37 documentation: 20 mV/°C and 500 mV at 25°C. [5](https://www.howtogeek.com/devops/how-to-download-single-files-from-a-github-repository/)[6](https://careerkarma.com/blog/git-download-a-single-file-from-github/)
*/
static float tmp37_mv_to_tempC(int mv)
{
    return ((float)mv) / 20.0f;
}

/*
    -------------------------
    EMA filtered read (your original function)
    -------------------------

    Same structure as your STM32 code:
      - read raw ADC
      - convert to voltage
      - convert to temperature
      - apply EMA:
            filtered = filtered + alpha * (new - filtered)

    Difference:
      - reading raw ADC is done via adc_oneshot_read() instead of HAL_ADC_Start/Poll/GetValue. [1](https://api.github.com/repos/reservoirprotocol/core)[2](https://huggingface.co/ggerganov/whisper.cpp/tree/main)
*/
float TMP37_ReadFiltered(TMP37_Handle *htmp)
{
    if (!htmp) return 0.0f;

    // 1) Read raw average
    int raw = 0;
    htmp->last_err = tmp37_read_raw_avg(htmp, &raw);
    if (htmp->last_err != ESP_OK) {
        // If we fail, return previous value (or 0 if never initialized)
        return htmp->ema_initialized ? htmp->ema_filtered : 0.0f;
    }

    // 2) Convert raw -> mV
    int mv = 0;
    htmp->last_err = tmp37_raw_to_mv(htmp, raw, &mv);
    if (htmp->last_err != ESP_OK) {
        return htmp->ema_initialized ? htmp->ema_filtered : 0.0f;
    }

    // 3) Convert mV -> °C
    float tempC = tmp37_mv_to_tempC(mv);

    // 4) EMA initialize or update
    if (!htmp->ema_initialized) {
        htmp->ema_filtered = tempC;
        htmp->ema_initialized = true;
    } else {
        float alpha = htmp->ema_alpha;
        htmp->ema_filtered += alpha * (tempC - htmp->ema_filtered);
    }

    return htmp->ema_filtered;
}

/*
    -------------------------
    Kalman filtered read 
    -------------------------  
      - x = estimate
      - P = uncertainty
      - Q = process noise
      - R = measurement noise
      - Predict: P = P + Q
      - Update:
          K = P / (P + R)
          x = x + K*(z - x)
          P = (1-K)*P
*/
float TMP37_ReadFilteredKalman(TMP37_Handle *htmp)
{
    if (!htmp) return 0.0f;

    // 1) read raw average
    int raw = 0;
    htmp->last_err = tmp37_read_raw_avg(htmp, &raw);
    if (htmp->last_err != ESP_OK) {
        return htmp->k_initialized ? htmp->k_x : 0.0f;
    }

    // 2) raw -> mV
    int mv = 0;
    htmp->last_err = tmp37_raw_to_mv(htmp, raw, &mv);
    if (htmp->last_err != ESP_OK) {
        return htmp->k_initialized ? htmp->k_x : 0.0f;
    }

    // 3) mV -> measured temp (z)
    float z = tmp37_mv_to_tempC(mv);

    // Init on first run
    if (!htmp->k_initialized) {
        htmp->k_x = z;
        htmp->k_P = 1.0f;
        htmp->k_initialized = true;
        return htmp->k_x;
    }

    // Tunables
    float Q = htmp->kalman_q;
    float R = htmp->kalman_r;

    // Predict: state x remains same (no model of drift), uncertainty increases
    htmp->k_P = htmp->k_P + Q;

    // Update
    float K = htmp->k_P / (htmp->k_P + R);
    htmp->k_x = htmp->k_x + K * (z - htmp->k_x);
    htmp->k_P = (1.0f - K) * htmp->k_P;

    return htmp->k_x;
}