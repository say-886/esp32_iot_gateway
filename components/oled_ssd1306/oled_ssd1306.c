#include "oled_ssd1306.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_state.h"
#include "board.h"
#include "driver/i2c.h"
#include "esp_log.h"

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_PAGE_COUNT (OLED_HEIGHT / 8)
#define OLED_BUFFER_SIZE (OLED_WIDTH * OLED_PAGE_COUNT)
#define OLED_TIMEOUT_MS 1000

static const char *TAG = "oled_ssd1306";
static uint8_t s_oled_buffer[OLED_BUFFER_SIZE];
static uint8_t s_oled_addr;
static bool s_oled_ready;

static const uint8_t *oled_get_glyph(char c)
{
    static const uint8_t glyph_space[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t glyph_dot[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
    static const uint8_t glyph_colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
    static const uint8_t glyph_dash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t glyph_slash[5] = {0x20, 0x10, 0x08, 0x04, 0x02};
    static const uint8_t glyph_percent[5] = {0x63, 0x13, 0x08, 0x64, 0x63};
    static const uint8_t glyph_0[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
    static const uint8_t glyph_1[5] = {0x00, 0x42, 0x7F, 0x40, 0x00};
    static const uint8_t glyph_2[5] = {0x42, 0x61, 0x51, 0x49, 0x46};
    static const uint8_t glyph_3[5] = {0x21, 0x41, 0x45, 0x4B, 0x31};
    static const uint8_t glyph_4[5] = {0x18, 0x14, 0x12, 0x7F, 0x10};
    static const uint8_t glyph_5[5] = {0x27, 0x45, 0x45, 0x45, 0x39};
    static const uint8_t glyph_6[5] = {0x3C, 0x4A, 0x49, 0x49, 0x30};
    static const uint8_t glyph_7[5] = {0x01, 0x71, 0x09, 0x05, 0x03};
    static const uint8_t glyph_8[5] = {0x36, 0x49, 0x49, 0x49, 0x36};
    static const uint8_t glyph_9[5] = {0x06, 0x49, 0x49, 0x29, 0x1E};
    static const uint8_t glyph_A[5] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
    static const uint8_t glyph_B[5] = {0x7F, 0x49, 0x49, 0x49, 0x36};
    static const uint8_t glyph_C[5] = {0x3E, 0x41, 0x41, 0x41, 0x22};
    static const uint8_t glyph_D[5] = {0x7F, 0x41, 0x41, 0x22, 0x1C};
    static const uint8_t glyph_E[5] = {0x7F, 0x49, 0x49, 0x49, 0x41};
    static const uint8_t glyph_F[5] = {0x7F, 0x09, 0x09, 0x09, 0x01};
    static const uint8_t glyph_G[5] = {0x3E, 0x41, 0x49, 0x49, 0x7A};
    static const uint8_t glyph_H[5] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
    static const uint8_t glyph_I[5] = {0x00, 0x41, 0x7F, 0x41, 0x00};
    static const uint8_t glyph_J[5] = {0x20, 0x40, 0x41, 0x3F, 0x01};
    static const uint8_t glyph_K[5] = {0x7F, 0x08, 0x14, 0x22, 0x41};
    static const uint8_t glyph_L[5] = {0x7F, 0x40, 0x40, 0x40, 0x40};
    static const uint8_t glyph_M[5] = {0x7F, 0x02, 0x0C, 0x02, 0x7F};
    static const uint8_t glyph_N[5] = {0x7F, 0x04, 0x08, 0x10, 0x7F};
    static const uint8_t glyph_O[5] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
    static const uint8_t glyph_P[5] = {0x7F, 0x09, 0x09, 0x09, 0x06};
    static const uint8_t glyph_Q[5] = {0x3E, 0x41, 0x51, 0x21, 0x5E};
    static const uint8_t glyph_R[5] = {0x7F, 0x09, 0x19, 0x29, 0x46};
    static const uint8_t glyph_S[5] = {0x46, 0x49, 0x49, 0x49, 0x31};
    static const uint8_t glyph_T[5] = {0x01, 0x01, 0x7F, 0x01, 0x01};
    static const uint8_t glyph_U[5] = {0x3F, 0x40, 0x40, 0x40, 0x3F};
    static const uint8_t glyph_V[5] = {0x1F, 0x20, 0x40, 0x20, 0x1F};
    static const uint8_t glyph_W[5] = {0x3F, 0x40, 0x38, 0x40, 0x3F};
    static const uint8_t glyph_X[5] = {0x63, 0x14, 0x08, 0x14, 0x63};
    static const uint8_t glyph_Y[5] = {0x07, 0x08, 0x70, 0x08, 0x07};
    static const uint8_t glyph_Z[5] = {0x61, 0x51, 0x49, 0x45, 0x43};

    switch (c) {
    case '.': return glyph_dot;
    case ':': return glyph_colon;
    case '-': return glyph_dash;
    case '/': return glyph_slash;
    case '%': return glyph_percent;
    case '0': return glyph_0;
    case '1': return glyph_1;
    case '2': return glyph_2;
    case '3': return glyph_3;
    case '4': return glyph_4;
    case '5': return glyph_5;
    case '6': return glyph_6;
    case '7': return glyph_7;
    case '8': return glyph_8;
    case '9': return glyph_9;
    case 'A': return glyph_A;
    case 'B': return glyph_B;
    case 'C': return glyph_C;
    case 'D': return glyph_D;
    case 'E': return glyph_E;
    case 'F': return glyph_F;
    case 'G': return glyph_G;
    case 'H': return glyph_H;
    case 'I': return glyph_I;
    case 'J': return glyph_J;
    case 'K': return glyph_K;
    case 'L': return glyph_L;
    case 'M': return glyph_M;
    case 'N': return glyph_N;
    case 'O': return glyph_O;
    case 'P': return glyph_P;
    case 'Q': return glyph_Q;
    case 'R': return glyph_R;
    case 'S': return glyph_S;
    case 'T': return glyph_T;
    case 'U': return glyph_U;
    case 'V': return glyph_V;
    case 'W': return glyph_W;
    case 'X': return glyph_X;
    case 'Y': return glyph_Y;
    case 'Z': return glyph_Z;
    default: return glyph_space;
    }
}

static esp_err_t oled_probe(uint8_t address)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        return ESP_ERR_NO_MEM;
    }

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)((address << 1) | I2C_MASTER_WRITE), true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(BOARD_I2C_PORT,
                                         cmd,
                                         pdMS_TO_TICKS(OLED_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t oled_send_command(uint8_t command)
{
    uint8_t payload[2] = {0x00, command};
    return i2c_master_write_to_device(BOARD_I2C_PORT,
                                      s_oled_addr,
                                      payload,
                                      sizeof(payload),
                                      pdMS_TO_TICKS(OLED_TIMEOUT_MS));
}

static esp_err_t oled_send_buffer(void)
{
    uint8_t payload[17] = {0};
    payload[0] = 0x40;

    for (uint8_t page = 0; page < OLED_PAGE_COUNT; page++) {
        ESP_RETURN_ON_ERROR(oled_send_command((uint8_t)(0xB0 + page)), TAG, "set page failed");
        ESP_RETURN_ON_ERROR(oled_send_command(0x00), TAG, "set low column failed");
        ESP_RETURN_ON_ERROR(oled_send_command(0x10), TAG, "set high column failed");

        for (int col = 0; col < OLED_WIDTH; col += 16) {
            memcpy(&payload[1], &s_oled_buffer[page * OLED_WIDTH + col], 16);
            esp_err_t ret = i2c_master_write_to_device(BOARD_I2C_PORT,
                                                       s_oled_addr,
                                                       payload,
                                                       sizeof(payload),
                                                       pdMS_TO_TICKS(OLED_TIMEOUT_MS));
            if (ret != ESP_OK) {
                return ret;
            }
        }
    }

    return ESP_OK;
}

static void oled_draw_char(int x, int page, char c)
{
    if (page < 0 || page >= OLED_PAGE_COUNT || x < 0 || x >= OLED_WIDTH) {
        return;
    }

    const uint8_t *glyph = oled_get_glyph(c);
    int base = page * OLED_WIDTH + x;

    for (int i = 0; i < 5 && (x + i) < OLED_WIDTH; i++) {
        s_oled_buffer[base + i] = glyph[i];
    }
    if ((x + 5) < OLED_WIDTH) {
        s_oled_buffer[base + 5] = 0x00;
    }
}

static void oled_draw_text(int page, const char *text)
{
    if (text == NULL || page < 0 || page >= OLED_PAGE_COUNT) {
        return;
    }

    int x = 0;
    while (*text != '\0' && x <= (OLED_WIDTH - 6)) {
        oled_draw_char(x, page, *text);
        x += 6;
        text++;
    }
}

esp_err_t oled_init(void)
{
    static const uint8_t init_sequence[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
        0x2E, 0xAF
    };

    if (s_oled_ready) {
        return ESP_OK;
    }

    if (oled_probe(BOARD_OLED_I2C_ADDR_PRIMARY) == ESP_OK) {
        s_oled_addr = BOARD_OLED_I2C_ADDR_PRIMARY;
    } else if (oled_probe(BOARD_OLED_I2C_ADDR_SECONDARY) == ESP_OK) {
        s_oled_addr = BOARD_OLED_I2C_ADDR_SECONDARY;
    } else {
        return ESP_ERR_NOT_FOUND;
    }

    for (size_t i = 0; i < sizeof(init_sequence); i++) {
        esp_err_t ret = oled_send_command(init_sequence[i]);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    memset(s_oled_buffer, 0, sizeof(s_oled_buffer));
    s_oled_ready = true;
    ESP_LOGI(TAG, "OLED init OK, addr=0x%02X", s_oled_addr);
    return oled_send_buffer();
}

void oled_clear(void)
{
    if (!s_oled_ready) {
        return;
    }

    memset(s_oled_buffer, 0, sizeof(s_oled_buffer));
    if (oled_send_buffer() != ESP_OK) {
        s_oled_ready = false;
    }
}

void oled_show_status(const device_status_t *status)
{
    char line[22];

    if (!s_oled_ready || status == NULL) {
        return;
    }

    memset(s_oled_buffer, 0, sizeof(s_oled_buffer));

    snprintf(line, sizeof(line), "TEMP %5.1fC", status->temperature);
    oled_draw_text(0, line);

    snprintf(line, sizeof(line), "HUM  %5.1f%%", status->humidity);
    oled_draw_text(1, line);

    snprintf(line, sizeof(line), "LUX  %5.1f", status->light);
    oled_draw_text(2, line);

    snprintf(line, sizeof(line), "WIFI%d MQTT%d", status->wifi_connected ? 1 : 0, status->mqtt_connected ? 1 : 0);
    oled_draw_text(3, line);

    snprintf(line, sizeof(line), "LED%d BUZ%d", status->led_on ? 1 : 0, status->buzzer_on ? 1 : 0);
    oled_draw_text(4, line);

    snprintf(line, sizeof(line), "RLY%d UP%lus", status->relay_on ? 1 : 0, (unsigned long)status->uptime_sec);
    oled_draw_text(5, line);

    snprintf(line, sizeof(line), "ERR %lu", (unsigned long)status->error_code);
    oled_draw_text(6, line);

    snprintf(line, sizeof(line), "%s", app_state_to_string(status->state));
    oled_draw_text(7, line);

    if (oled_send_buffer() != ESP_OK) {
        ESP_LOGW(TAG, "OLED update failed, disable display output");
        s_oled_ready = false;
    }
}
