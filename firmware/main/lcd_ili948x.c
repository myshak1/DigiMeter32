/*
 * Minimalny sterownik SPI dla ILI9488 / ILI9486 / ST7796S 480x320
 * oparty o esp_lcd_panel_io_spi (DMA, asynchroniczne przesylanie).
 *
 * ILI9488 i ILI9486 po SPI przyjmuja wylacznie kolor 18-bit (3 bajty
 * na piksel), wiec RGB565 z LVGL jest konwertowane w locie.
 * ST7796S i wyswietlacze "RPi" (ILI9486 za rejestrem 16-bit) przyjmuja
 * RGB565 w kolejnosci big-endian.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_check.h"
#include "sdkconfig.h"
#include "lcd_ili948x.h"

static const char *TAG = "lcd";

#if CONFIG_IDF_TARGET_ESP32
#define LCD_HOST SPI3_HOST   /* VSPI: natywne piny SCK 18, MOSI 23, CS 5 */
#else
#define LCD_HOST SPI2_HOST
#endif

#if CONFIG_DM32_LCD_ILI9488
#define DRV_NAME "ILI9488"
#define PIX_BYTES 3
#elif CONFIG_DM32_LCD_ILI9486
#define DRV_NAME "ILI9486"
#define PIX_BYTES 3
#elif CONFIG_DM32_LCD_ILI9486_RPI
#define DRV_NAME "ILI9486-RPi"
#define PIX_BYTES 2
#define RPI_16BIT 1
#else
#define DRV_NAME "ST7796S"
#define PIX_BYTES 2
#endif

#ifndef RPI_16BIT
#define RPI_16BIT 0
#endif

/* MADCTL */
#define MAD_MY  0x80
#define MAD_MX  0x40
#define MAD_MV  0x20
#define MAD_BGR 0x08

typedef struct {
    uint8_t  cmd;
    uint8_t  len;
    uint16_t delay_ms;
    uint8_t  data[16];
} init_cmd_t;

#if CONFIG_DM32_LCD_ILI9488
static const init_cmd_t s_init[] = {
    { 0xE0, 15, 0, { 0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78, 0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F } },
    { 0xE1, 15, 0, { 0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45, 0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F } },
    { 0xC0, 2, 0, { 0x17, 0x15 } },          /* Power Control 1 */
    { 0xC1, 1, 0, { 0x41 } },                /* Power Control 2 */
    { 0xC5, 3, 0, { 0x00, 0x12, 0x80 } },    /* VCOM */
    { 0x3A, 1, 0, { 0x66 } },                /* 18 bit/piksel */
    { 0xB0, 1, 0, { 0x00 } },                /* Interface Mode */
    { 0xB1, 1, 0, { 0xA0 } },                /* Frame rate 60 Hz */
    { 0xB4, 1, 0, { 0x02 } },                /* Inversion control */
    { 0xB6, 3, 0, { 0x02, 0x02, 0x3B } },    /* Display Function */
    { 0xB7, 1, 0, { 0xC6 } },                /* Entry mode */
    { 0xF7, 4, 0, { 0xA9, 0x51, 0x2C, 0x82 } },
    { 0x11, 0, 120, { 0 } },                 /* Sleep out */
};
#elif CONFIG_DM32_LCD_ILI9486 || CONFIG_DM32_LCD_ILI9486_RPI
static const init_cmd_t s_init[] = {
    { 0x11, 0, 120, { 0 } },
#if RPI_16BIT
    { 0x3A, 1, 0, { 0x55 } },                /* 16 bit/piksel */
#else
    { 0x3A, 1, 0, { 0x66 } },                /* 18 bit/piksel */
#endif
    { 0xC0, 2, 0, { 0x0E, 0x0E } },
    { 0xC1, 2, 0, { 0x41, 0x00 } },
    { 0xC2, 1, 0, { 0x55 } },
    { 0xC5, 4, 0, { 0x00, 0x00, 0x00, 0x00 } },
    { 0xE0, 15, 0, { 0x0F, 0x1F, 0x1C, 0x0C, 0x0F, 0x08, 0x48, 0x98, 0x37, 0x0A, 0x13, 0x04, 0x11, 0x0D, 0x00 } },
    { 0xE1, 15, 0, { 0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75, 0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00 } },
};
#else /* ST7796S */
static const init_cmd_t s_init[] = {
    { 0x11, 0, 120, { 0 } },
    { 0xF0, 1, 0, { 0xC3 } },
    { 0xF0, 1, 0, { 0x96 } },
    { 0x3A, 1, 0, { 0x55 } },                /* 16 bit/piksel */
    { 0xB4, 1, 0, { 0x01 } },
    { 0xB6, 3, 0, { 0x80, 0x02, 0x3B } },
    { 0xE8, 8, 0, { 0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33 } },
    { 0xC1, 1, 0, { 0x06 } },
    { 0xC2, 1, 0, { 0xA7 } },
    { 0xC5, 1, 120, { 0x18 } },
    { 0xE0, 14, 0, { 0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F, 0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B } },
    { 0xE1, 14, 120, { 0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2B, 0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B } },
    { 0xF0, 1, 0, { 0x3C } },
    { 0xF0, 1, 120, { 0x69 } },
};
#endif

static esp_lcd_panel_io_handle_t s_io;
static uint8_t      *s_tx;
static size_t        s_max_px;
static lcd_done_cb_t s_done_cb;
static void         *s_done_arg;

static esp_err_t lcd_cmd(uint8_t cmd, const uint8_t *data, size_t len)
{
#if RPI_16BIT
    /* kazdy bajt idzie jako slowo 16-bit (0x00, bajt) */
    uint8_t buf[32];
    if (len > sizeof(buf) / 2) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t i = 0; i < len; i++) {
        buf[2 * i] = 0x00;
        buf[2 * i + 1] = data[i];
    }
    return esp_lcd_panel_io_tx_param(s_io, cmd, len ? buf : NULL, len * 2);
#else
    return esp_lcd_panel_io_tx_param(s_io, cmd, len ? data : NULL, len);
#endif
}

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ed, void *ctx)
{
    if (s_done_cb) {
        s_done_cb(s_done_arg);
    }
    return false;
}

void lcd_set_done_cb(lcd_done_cb_t cb, void *arg)
{
    s_done_cb = cb;
    s_done_arg = arg;
}

const char *lcd_driver_name(void)
{
    return DRV_NAME;
}

static void backlight_init(void)
{
#if CONFIG_DM32_LCD_PIN_BL >= 0
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&t);
    ledc_channel_config_t c = {
        .gpio_num = CONFIG_DM32_LCD_PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
#if !CONFIG_DM32_LCD_BL_ACTIVE_HIGH
        .flags.output_invert = 1,
#endif
    };
    ledc_channel_config(&c);
#endif
}

void lcd_set_backlight(uint8_t percent)
{
#if CONFIG_DM32_LCD_PIN_BL >= 0
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty = (1023u * percent) / 100u;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
#else
    (void)percent;
#endif
}

esp_err_t lcd_init(size_t max_pixels)
{
    s_max_px = max_pixels;
    size_t tx_bytes = max_pixels * PIX_BYTES;

    spi_bus_config_t bus = {
        .sclk_io_num = CONFIG_DM32_LCD_PIN_SCLK,
        .mosi_io_num = CONFIG_DM32_LCD_PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = tx_bytes + 16,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = CONFIG_DM32_LCD_PIN_DC,
        .cs_gpio_num = CONFIG_DM32_LCD_PIN_CS,
        .pclk_hz = CONFIG_DM32_LCD_SPI_MHZ * 1000 * 1000,
        .lcd_cmd_bits = RPI_16BIT ? 16 : 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_trans_done,
        .user_ctx = NULL,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io),
                        TAG, "panel io");

    s_tx = heap_caps_malloc(tx_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_tx, ESP_ERR_NO_MEM, TAG, "bufor DMA");

    backlight_init();
    lcd_set_backlight(0);

#if CONFIG_DM32_LCD_PIN_RST >= 0
    gpio_reset_pin(CONFIG_DM32_LCD_PIN_RST);
    gpio_set_direction(CONFIG_DM32_LCD_PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(CONFIG_DM32_LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(CONFIG_DM32_LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
#endif
    lcd_cmd(0x01, NULL, 0);                  /* reset programowy */
    vTaskDelay(pdMS_TO_TICKS(150));

    for (size_t i = 0; i < sizeof(s_init) / sizeof(s_init[0]); i++) {
        ESP_RETURN_ON_ERROR(lcd_cmd(s_init[i].cmd, s_init[i].data, s_init[i].len), TAG, "init");
        if (s_init[i].delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(s_init[i].delay_ms));
        }
    }

    /* orientacja pozioma 480x320 */
    uint8_t mad = MAD_MV;
#if CONFIG_DM32_LCD_FLIP
    mad |= MAD_MX | MAD_MY;
#endif
#if CONFIG_DM32_LCD_BGR
    mad |= MAD_BGR;
#endif
    lcd_cmd(0x36, &mad, 1);
#if CONFIG_DM32_LCD_INVERT
    lcd_cmd(0x21, NULL, 0);                  /* inwersja wlaczona */
#else
    lcd_cmd(0x20, NULL, 0);
#endif

    /* wyczysc ekran na czarno przed wlaczeniem */
    memset(s_tx, 0, tx_bytes);
    int lines = max_pixels / LCD_H_RES;
    for (int y = 0; y < LCD_V_RES; y += lines) {
        int y2 = y + lines - 1;
        if (y2 >= LCD_V_RES) {
            y2 = LCD_V_RES - 1;
        }
        uint8_t ca[4] = { 0, 0, (LCD_H_RES - 1) >> 8, (LCD_H_RES - 1) & 0xFF };
        uint8_t ra[4] = { y >> 8, y & 0xFF, y2 >> 8, y2 & 0xFF };
        lcd_cmd(0x2A, ca, 4);
        lcd_cmd(0x2B, ra, 4);
        esp_lcd_panel_io_tx_color(s_io, 0x2C, s_tx, (size_t)LCD_H_RES * (y2 - y + 1) * PIX_BYTES);
    }
    lcd_cmd(0x29, NULL, 0);                  /* display on */
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_LOGI(TAG, "%s %dx%d, SPI %d MHz", DRV_NAME, LCD_H_RES, LCD_V_RES, CONFIG_DM32_LCD_SPI_MHZ);
    return ESP_OK;
}

esp_err_t lcd_draw(int x1, int y1, int x2, int y2, const uint16_t *src)
{
    size_t npx = (size_t)(x2 - x1 + 1) * (y2 - y1 + 1);
    if (npx > s_max_px) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *d = s_tx;
#if PIX_BYTES == 3
    for (size_t i = 0; i < npx; i++) {
        uint16_t c = src[i];
        *d++ = (c >> 8) & 0xF8;          /* R5 -> górne bity */
        *d++ = (c >> 3) & 0xFC;          /* G6 */
        *d++ = (c << 3) & 0xF8;          /* B5 */
    }
#else
    for (size_t i = 0; i < npx; i++) {
        uint16_t c = src[i];
        *d++ = c >> 8;
        *d++ = c & 0xFF;
    }
#endif

    uint8_t ca[4] = { x1 >> 8, x1 & 0xFF, x2 >> 8, x2 & 0xFF };
    uint8_t ra[4] = { y1 >> 8, y1 & 0xFF, y2 >> 8, y2 & 0xFF };
    lcd_cmd(0x2A, ca, 4);
    lcd_cmd(0x2B, ra, 4);
    return esp_lcd_panel_io_tx_color(s_io, 0x2C, s_tx, npx * PIX_BYTES);
}
