#include "bm24_display.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"

#include "bm24_media.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

#define LCD_WIDTH       320
#define LCD_HEIGHT      170
#define LCD_BAND_HEIGHT 24
#define LCD_PIXEL_CLOCK (10 * 1000 * 1000)
#define BACKLIGHT_DUTY   130u   /* Vorgabe; ueberschreibbar aus der Konfiguration */

#define PIN_POWER 15
#define PIN_BL    38
#define PIN_D0    39
#define PIN_D1    40
#define PIN_D2    41
#define PIN_D3    42
#define PIN_D4    45
#define PIN_D5    46
#define PIN_D6    47
#define PIN_D7    48
#define PIN_RST    5
#define PIN_CS     6
#define PIN_DC     7
#define PIN_WR     8
#define PIN_RD     9

#define RGB565(r, g, b) \
    (uint16_t)((((r) & 0xf8u) << 8) | (((g) & 0xfcu) << 3) | ((b) >> 3))
#define COLOR_BG     RGB565(7, 16, 24)
#define COLOR_ORANGE RGB565(247, 147, 26)
#define COLOR_WHITE  RGB565(235, 242, 250)
#define COLOR_BLUE   RGB565(90, 190, 255)
#define COLOR_GREEN  RGB565(80, 220, 130)

typedef struct {
    uint8_t command;
    uint8_t data[14];
    uint8_t length;
    uint16_t delay_ms;
} lcd_command;

static const lcd_command INIT_SEQUENCE[] = {
    {0x11, {0}, 0, 120},
    {0x3A, {0x05}, 1, 0},
    {0xB2, {0x0B, 0x0B, 0x00, 0x33, 0x33}, 5, 0},
    {0xB7, {0x75}, 1, 0},
    {0xBB, {0x28}, 1, 0},
    {0xC0, {0x2C}, 1, 0},
    {0xC2, {0x01}, 1, 0},
    {0xC3, {0x1F}, 1, 0},
    {0xC6, {0x13}, 1, 0},
    {0xD0, {0xA7}, 1, 0},
    {0xD0, {0xA4, 0xA1}, 2, 0},
    {0xD6, {0xA1}, 1, 0},
    {0xE0, {0xF0, 0x05, 0x0A, 0x06, 0x06, 0x03, 0x2B,
            0x32, 0x43, 0x36, 0x11, 0x10, 0x2B, 0x32}, 14, 0},
    {0xE1, {0xF0, 0x08, 0x0C, 0x0B, 0x09, 0x24, 0x2B,
            0x22, 0x43, 0x38, 0x15, 0x16, 0x2F, 0x37}, 14, 0},
};

/* 5x7-Zeichen, je Zeile fuenf Bits. Die Produktanzeige braucht bewusst
   nur ASCII; Statusdaten bleiben damit klein und deterministisch. */
static const uint8_t FONT_DIGITS[10][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31},     {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2},     {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
};

static const uint8_t FONT_LETTERS[26][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},      {7,2,2,2,2,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},   {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},     {31,1,2,4,8,16,31},
};

static const char *TAG = "bm24_lcd";
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_tx_done;
static SemaphoreHandle_t s_frame_lock;
static SemaphoreHandle_t s_panel_lock;
static uint16_t s_pixels[LCD_WIDTH * LCD_BAND_HEIGHT];
static const uint16_t *s_background;   /* aktuelle Hintergrundgrafik, im Flash */
static bm24_display_frame s_frame;
static TaskHandle_t s_task;
static bool s_backlight_enabled = true;
static uint8_t s_brightness = BACKLIGHT_DUTY;
static bool s_invert;
static bool s_flipped;

static bool backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_channel_config_t channel = {
        .gpio_num = PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    return ledc_timer_config(&timer) == ESP_OK &&
           ledc_channel_config(&channel) == ESP_OK;
}

static void backlight_apply(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                  s_backlight_enabled ? s_brightness : 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static bool color_done(esp_lcd_panel_io_handle_t io,
                       esp_lcd_panel_io_event_data_t *event, void *arg)
{
    (void)io;
    (void)event;
    (void)arg;
    BaseType_t awakened = pdFALSE;
    xSemaphoreGiveFromISR(s_tx_done, &awakened);
    return awakened == pdTRUE;
}

static const uint8_t *glyph(char c, uint8_t scratch[7])
{
    if (c >= '0' && c <= '9')
        return FONT_DIGITS[c - '0'];
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z')
        return FONT_LETTERS[c - 'A'];

    memset(scratch, 0, 7);
    switch (c) {
    case '-': scratch[3] = 14; break;
    case '_': scratch[6] = 31; break;
    case '.': scratch[6] = 4; break;
    case ':': scratch[2] = 4; scratch[5] = 4; break;
    case '/': scratch[0] = 1; scratch[1] = 2; scratch[2] = 2;
              scratch[3] = 4; scratch[4] = 8; scratch[5] = 8;
              scratch[6] = 16; break;
    case '%': scratch[0] = 17; scratch[1] = 2; scratch[2] = 4;
              scratch[3] = 4; scratch[4] = 8; scratch[5] = 16;
              scratch[6] = 17; break;
    case '+': scratch[2] = 4; scratch[3] = 14; scratch[4] = 4; break;
    case ',': scratch[5] = 4; scratch[6] = 8; break;
    case '=': scratch[2] = 14; scratch[4] = 14; break;
    case '#': scratch[1] = 10; scratch[2] = 31; scratch[3] = 10;
              scratch[4] = 31; scratch[5] = 10; break;
    case '|': scratch[0] = 4; scratch[1] = 4; scratch[2] = 4;
              scratch[3] = 4; scratch[4] = 4; scratch[5] = 4;
              scratch[6] = 4; break;
    case '$': scratch[0] = 4; scratch[1] = 15; scratch[2] = 20;
              scratch[3] = 14; scratch[4] = 5; scratch[5] = 30;
              scratch[6] = 4; break;
    case '(': scratch[1] = 2; scratch[2] = 4; scratch[3] = 4;
              scratch[4] = 4; scratch[5] = 2; break;
    case ')': scratch[1] = 8; scratch[2] = 4; scratch[3] = 4;
              scratch[4] = 4; scratch[5] = 8; break;
    default: break;
    }
    return scratch;
}

static void fill(uint16_t color, size_t pixels)
{
    for (size_t i = 0; i < pixels; ++i)
        s_pixels[i] = color;
}

/* Zeilenband aus der Hintergrundgrafik in den Sendepuffer holen. Ohne
   gesetztes Bild bleibt es bei der bisherigen Vollfarbe. Damit steht die
   1.x-Optik wieder hinter den Werten, ohne dass ein kompletter
   Bildspeicher im RAM gehalten werden muss: es liegt immer nur ein Band
   von LCD_BAND_HEIGHT Zeilen im Speicher, das Bild selbst bleibt im Flash. */
static void fill_background(int y, int height)
{
    if (!s_background) {
        fill(COLOR_BG, (size_t)LCD_WIDTH * height);
        return;
    }
    memcpy(s_pixels, s_background + (size_t)y * LCD_WIDTH,
           (size_t)LCD_WIDTH * height * sizeof(uint16_t));
}

static bool push_band(int y, int height)
{
    if (esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_WIDTH, y + height,
                                  s_pixels) != ESP_OK)
        return false;
    return xSemaphoreTake(s_tx_done, pdMS_TO_TICKS(500)) == pdTRUE;
}

static void clear_screen(void)
{
    for (int y = 0; y < LCD_HEIGHT; y += LCD_BAND_HEIGHT) {
        int height = LCD_HEIGHT - y;
        if (height > LCD_BAND_HEIGHT)
            height = LCD_BAND_HEIGHT;
        fill_background(y, height);
        if (!push_band(y, height))
            return;
    }
}

static void draw_text(int y, const char *text, uint8_t scale,
                      uint16_t color)
{
    if (!text || scale == 0)
        return;
    int height = 7 * scale;
    if (height > LCD_BAND_HEIGHT || y < 0 || y + height > LCD_HEIGHT)
        return;
    fill_background(y, height);

    int x = 3;
    for (const char *p = text; *p && x + 5 * scale <= LCD_WIDTH; ++p) {
        uint8_t scratch[7];
        const uint8_t *rows = glyph(*p, scratch);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (!(rows[row] & (1u << (4 - col))))
                    continue;
                for (int sy = 0; sy < scale; ++sy) {
                    uint16_t *dest = s_pixels +
                        (row * scale + sy) * LCD_WIDTH + x + col * scale;
                    for (int sx = 0; sx < scale; ++sx)
                        dest[sx] = color;
                }
            }
        }
        x += 6 * scale;
    }
    push_band(y, height);
}

static void render_task(void *arg)
{
    (void)arg;
    bm24_display_frame frame;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (xSemaphoreTake(s_frame_lock, pdMS_TO_TICKS(100)) != pdTRUE)
            continue;
        frame = s_frame;
        xSemaphoreGive(s_frame_lock);

        if (xSemaphoreTake(s_panel_lock, pdMS_TO_TICKS(500)) != pdTRUE)
            continue;
        s_background = frame.background;
        clear_screen();
        if (frame.style == BM24_DISPLAY_STYLE_BIG_VALUE) {
            draw_text(4, frame.line[0], 2, COLOR_ORANGE);
            draw_text(34, frame.line[1], 4, COLOR_WHITE);
            draw_text(75, frame.line[2], 2, COLOR_BLUE);
            draw_text(99, frame.line[3], 2, COLOR_WHITE);
            draw_text(123, frame.line[4], 2, COLOR_GREEN);
            draw_text(153, frame.line[5], 1, COLOR_WHITE);
        } else if (frame.style == BM24_DISPLAY_STYLE_DASHBOARD) {
            draw_text(4, frame.line[0], 2, COLOR_ORANGE);
            draw_text(29, frame.line[1], 2, COLOR_WHITE);
            draw_text(53, frame.line[2], 2, COLOR_BLUE);
            draw_text(77, frame.line[3], 2, COLOR_WHITE);
            draw_text(101, frame.line[4], 2, COLOR_GREEN);
            draw_text(139, frame.line[5], 1, COLOR_WHITE);
        } else {
            draw_text(4, frame.line[0], 3, COLOR_ORANGE);
            draw_text(31, frame.line[1], 2, COLOR_WHITE);
            draw_text(55, frame.line[2], 2, COLOR_BLUE);
            draw_text(79, frame.line[3], 2, COLOR_WHITE);
            draw_text(103, frame.line[4], 2, COLOR_GREEN);
            draw_text(139, frame.line[5], 1, COLOR_WHITE);
        }
        xSemaphoreGive(s_panel_lock);
    }
}

bool bm24_display_start(void)
{
    if (s_task)
        return true;
    s_tx_done = xSemaphoreCreateBinary();
    s_frame_lock = xSemaphoreCreateMutex();
    s_panel_lock = xSemaphoreCreateMutex();
    if (!s_tx_done || !s_frame_lock || !s_panel_lock)
        return false;

    gpio_config_t output = {
        .pin_bit_mask = (1ULL << PIN_POWER) | (1ULL << PIN_RD),
        .mode = GPIO_MODE_OUTPUT
    };
    if (gpio_config(&output) != ESP_OK || !backlight_init())
        return false;
    gpio_set_level(PIN_POWER, 1);
    gpio_set_level(PIN_RD, 1);

    esp_lcd_i80_bus_handle_t bus;
    esp_lcd_i80_bus_config_t bus_config = {
        .dc_gpio_num = PIN_DC,
        .wr_gpio_num = PIN_WR,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {
            PIN_D0, PIN_D1, PIN_D2, PIN_D3,
            PIN_D4, PIN_D5, PIN_D6, PIN_D7
        },
        .bus_width = 8,
        .max_transfer_bytes = sizeof(s_pixels),
        .psram_trans_align = 64,
        .sram_trans_align = 4
    };
    if (esp_lcd_new_i80_bus(&bus_config, &bus) != ESP_OK)
        return false;

    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = PIN_CS,
        .pclk_hz = LCD_PIXEL_CLOCK,
        .trans_queue_depth = 1,
        .on_color_trans_done = color_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1
        }
    };
    if (esp_lcd_new_panel_io_i80(bus, &io_config, &s_io) != ESP_OK)
        return false;

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16
    };
    if (esp_lcd_new_panel_st7789(s_io, &panel_config, &s_panel) != ESP_OK ||
        esp_lcd_panel_reset(s_panel) != ESP_OK ||
        esp_lcd_panel_init(s_panel) != ESP_OK ||
        esp_lcd_panel_invert_color(s_panel, true) != ESP_OK ||
        esp_lcd_panel_swap_xy(s_panel, true) != ESP_OK ||
        esp_lcd_panel_mirror(s_panel, false, true) != ESP_OK ||
        esp_lcd_panel_set_gap(s_panel, 0, 35) != ESP_OK)
        return false;

    for (size_t i = 0;
         i < sizeof(INIT_SEQUENCE) / sizeof(INIT_SEQUENCE[0]); ++i) {
        const lcd_command *command = &INIT_SEQUENCE[i];
        if (esp_lcd_panel_io_tx_param(s_io, command->command,
                                      command->data,
                                      command->length) != ESP_OK)
            return false;
        if (command->delay_ms)
            vTaskDelay(pdMS_TO_TICKS(command->delay_ms));
    }
    if (esp_lcd_panel_disp_on_off(s_panel, true) != ESP_OK)
        return false;
    backlight_apply();

    strlcpy(s_frame.line[0], "BITMINER24", sizeof(s_frame.line[0]));
    strlcpy(s_frame.line[1], "START IDF 5.5", sizeof(s_frame.line[1]));
    if (xTaskCreatePinnedToCore(render_task, "bm24Lcd", 4096, NULL, 2,
                                &s_task, 1) != pdPASS)
        return false;
    xTaskNotifyGive(s_task);
    ESP_LOGI(TAG, "T-Display S3 ueber nativen I80-Treiber bereit");
    return true;
}

void bm24_display_set(const bm24_display_frame *frame)
{
    if (!frame || !s_task)
        return;
    if (xSemaphoreTake(s_frame_lock, pdMS_TO_TICKS(50)) != pdTRUE)
        return;
    s_frame = *frame;
    for (size_t i = 0; i < BM24_DISPLAY_LINES; ++i)
        s_frame.line[i][BM24_DISPLAY_LINE_MAX] = '\0';
    xSemaphoreGive(s_frame_lock);
    xTaskNotifyGive(s_task);
}

void bm24_display_setup(const char *ssid, const char *password)
{
    /* Die Setup-Grafik traegt die Anleitung bereits im Bild. Zusaetzlicher
       Text legte sich darueber und machte beides unlesbar, deshalb bleibt
       diese Seite rein grafisch. Zugangsdaten stehen im seriellen Log und
       im Beileger; sichtbar bleiben sie ausserdem im WLAN-Namen selbst. */
    (void)ssid;
    (void)password;
    bm24_display_frame frame = {0};
    frame.background = bm24_img_setup;
    bm24_display_set(&frame);
}

void bm24_display_apply_settings(uint8_t brightness, bool invert)
{
    /* Ganz dunkel waere ein Geraet, das kaputt aussieht; nach unten
       deshalb bei 10 begrenzt. */
    s_brightness = brightness < 10u ? 10u : brightness;
    s_invert = invert;
    backlight_apply();
    if (s_panel)
        esp_lcd_panel_invert_color(s_panel, !s_invert);
    if (s_task)
        xTaskNotifyGive(s_task);
}

void bm24_display_toggle_enabled(void)
{
    s_backlight_enabled = !s_backlight_enabled;
    backlight_apply();
}

void bm24_display_toggle_rotation(void)
{
    if (!s_panel ||
        xSemaphoreTake(s_panel_lock, pdMS_TO_TICKS(500)) != pdTRUE)
        return;
    s_flipped = !s_flipped;
    esp_lcd_panel_mirror(s_panel, s_flipped, !s_flipped);
    xSemaphoreGive(s_panel_lock);
    if (s_task)
        xTaskNotifyGive(s_task);
}
