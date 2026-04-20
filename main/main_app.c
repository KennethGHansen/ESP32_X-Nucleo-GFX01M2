#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "st7789h2.h"

void app_main(void)
{
    st7789h2_config_t cfg = {
        .host = SPI2_HOST,

        // SPI2 IO_MUX defaults on ESP32-S3: CS0=10 MOSI=11 SCLK=12 MISO=13 [1](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/spi_master.html)
        .pin_cs   = 10,
        .pin_mosi = 11,
        .pin_sclk = 12,
        .pin_miso = -1,   // LCD is write-only here; set to 13 if you actually wire MISO

        // Use the remaining SPI2 IO_MUX “quad” pins as GPIO for LCD control:
        //.pin_dc   = 9,    // QUADHD pin used as DC
        .pin_dc = 46,     // FOR WEATHER-STATION
        .pin_rst  = 14,   // QUADWP pin used as RST
        .pin_bckl = -1,   // set to a GPIO if you control backlight

        .spi_clock_hz = 20 * 1000 * 1000, // start at 10MHz, increase later
        .spi_mode     = 0,

        .width    = 240,
        .height   = 320,
        .x_offset = 0,
        .y_offset = 0,
    };

    ESP_ERROR_CHECK(st7789h2_init(&cfg));

    st7789h2_fill(0x0000);

    st7789h2_draw_string_scaled(
        50, 50,
        "Hello World!\n\nJust a Test\n\nTo see\n\nIf it works",
        0xFFFF, 0x0000, 2
    );
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    st7789h2_fill(0x0000);
    st7789h2_draw_string_scaled_fast(50,50,"Hello World!\n\nJust a Test\n\nTo see\n\nIf it works", 0xFFFF, 0x0000, 2);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}