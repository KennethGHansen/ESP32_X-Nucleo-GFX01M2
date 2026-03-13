/*
    Running code, using the TMP37 sensor ADC readings via the custom TMP37 driver.
    Results are filtered by use of EMA and Kalman technique.
    The driver was originatly developed for the STM32 microcontroller and then rewritten for ESP32

    ESP-IDF entrypoint is app_main()
    ESP-IDF projects run on FreeRTOS
*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "tmp37.h"

static const char *TAG = "APP";

void app_main(void)
{
    /*
        1) Decide which ADC pin, wired TMP37 Vout, to use
           Example below uses ADC_CHANNEL_2
    */

    TMP37_Handle htmp = {
        // ADC config
        .unit     = ADC_UNIT_1,
        .channel  = ADC_CHANNEL_2,          // <-- CHANGE THIS to match your wiring
        .atten    = ADC_ATTEN_DB_12,        // widest range; safe default
        .bitwidth = ADC_BITWIDTH_DEFAULT,

        // Averaging
        .samples  = 16,                     // take 16 readings and average

        // Fallback vref
        .vref_mv  = 3300,                   // used if calibration isn't available

        // Filter setup
        // Slow / low noise
        .ema_alpha = 0.01f,
        .kalman_q  = 0.001f,
        .kalman_r  = 5.0f,
        
        // Fast / higher noise
        //.ema_alpha = 0.2f,
        //.kalman_q  = 0.05f,
        //.kalman_r  = 1.0f,
    };

    /*
        Initialize ADC oneshot + channel config and attempt calibration.
        If calibration works, raw->mV becomes much more accurate.
    */
    TMP37_Init(&htmp);

    while (1) {
        // Read filtered temperatures
        float ema_temp = TMP37_ReadFiltered(&htmp);
        float kal_temp = TMP37_ReadFilteredKalman(&htmp);

        // Print to console (idf.py monitor)
        ESP_LOGI(TAG, "TMP37: EMA=%.2f C | Kalman=%.2f C", ema_temp, kal_temp);

        /*
            FreeRTOS delay in milliseconds:
            vTaskDelay(pdMS_TO_TICKS(ms));
        */
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // If you ever break out of loop, you could call:
    // TMP37_Deinit(&htmp);
}