#include "i2c_lcd.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c.h"
#include <unistd.h>

static const char *TAG = "I2C_LCD";

#define LCD_RECOVERY_THRESHOLD 3
#define LCD_RECOVERY_BASE_MS 1000
#define LCD_RECOVERY_MAX_MS 60000

static uint8_t _addr;
static bool s_backlight_on = true;
static uint8_t s_write_failures = 0;
static bool s_in_recovery = false;
static uint32_t s_recovery_backoff_ms = LCD_RECOVERY_BASE_MS;
static int64_t s_next_recovery_us = 0;

static void lcd_recovery_reset(void) {
    s_recovery_backoff_ms = LCD_RECOVERY_BASE_MS;
    s_next_recovery_us = 0;
}

// Comandos LCD
#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME 0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_CURSORSHIFT 0x10
#define LCD_FUNCTIONSET 0x20
#define LCD_SETCGRAMADDR 0x40
#define LCD_SETDDRAMADDR 0x80

// Flags display
#define LCD_DISPLAYON 0x04
#define LCD_BACKLIGHT 0x08
#define LCD_ENABLE_BIT 0x04
#define LCD_RW_BIT 0x02 
#define LCD_RS_BIT 0x01 

static esp_err_t i2c_lcd_write_byte(uint8_t val) {
    uint8_t out = val;
    if (s_backlight_on) {
        out |= LCD_BACKLIGHT;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, out, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        if (++s_write_failures >= LCD_RECOVERY_THRESHOLD) {
            s_write_failures = 0;
            i2c_lcd_reinit();
        }
    } else {
        if (s_write_failures) {
            s_write_failures = 0;
        }
        lcd_recovery_reset();
    }
    return ret;
}

static void i2c_lcd_pulse_enable(uint8_t val) {
    i2c_lcd_write_byte(val | LCD_ENABLE_BIT);
    usleep(1);
    i2c_lcd_write_byte(val & ~LCD_ENABLE_BIT);
    usleep(50);
}

static void i2c_lcd_write_nibble(uint8_t val, uint8_t mode) {
    uint8_t high = val & 0xF0;
    i2c_lcd_write_byte(high | mode);
    i2c_lcd_pulse_enable(high | mode);
}

static void i2c_lcd_send_byte(uint8_t val, uint8_t mode) {
    uint8_t high = val & 0xF0;
    uint8_t low = (val << 4) & 0xF0;
    i2c_lcd_write_byte(high | mode);
    i2c_lcd_pulse_enable(high | mode);
    i2c_lcd_write_byte(low | mode);
    i2c_lcd_pulse_enable(low | mode);
}

void i2c_lcd_init(uint8_t addr) {
    _addr = addr;
    s_write_failures = 0;
    usleep(50000);
    
    i2c_lcd_write_nibble(0x30, 0); usleep(4500);
    i2c_lcd_write_nibble(0x30, 0); usleep(4500);
    i2c_lcd_write_nibble(0x30, 0); usleep(150);
    i2c_lcd_write_nibble(0x20, 0); 

    i2c_lcd_send_byte(0x28, 0); 
    i2c_lcd_send_byte(0x0C, 0); 
    i2c_lcd_send_byte(0x06, 0); 
    i2c_lcd_send_byte(0x01, 0); 
    usleep(2000);
}

void i2c_lcd_reinit(void) {
    int64_t now = esp_timer_get_time();
    if (s_in_recovery) {
        return;
    }
    if (s_next_recovery_us != 0 && now < s_next_recovery_us) {
        return;
    }
    s_in_recovery = true;
    bool backlight = s_backlight_on;
    ESP_LOGW(TAG, "LCD reinitializing");
    i2c_lcd_init(_addr);
    if (s_backlight_on != backlight) {
        i2c_lcd_set_backlight(backlight);
    }
    if (i2c_lcd_is_alive() == ESP_OK) {
        lcd_recovery_reset();
    } else {
        if (s_recovery_backoff_ms < LCD_RECOVERY_MAX_MS) {
            s_recovery_backoff_ms *= 2;
            if (s_recovery_backoff_ms > LCD_RECOVERY_MAX_MS) {
                s_recovery_backoff_ms = LCD_RECOVERY_MAX_MS;
            }
        }
        s_next_recovery_us = now + (int64_t)s_recovery_backoff_ms * 1000;
    }
    s_in_recovery = false;
}

// --- CORRECCIÓN AQUÍ: uint8_t en lugar de int ---
void i2c_lcd_write_text(uint8_t row, uint8_t col, const char *text) {
    // Direcciones para 20x4
    int row_offsets[] = { 0x00, 0x40, 0x14, 0x54 };
    
    if (row > 3) row = 3;
    if (col > 19) col = 19;

    i2c_lcd_send_byte(LCD_SETDDRAMADDR | (col + row_offsets[row]), 0);
    
    while (*text) {
        i2c_lcd_send_byte((uint8_t)(*text), LCD_RS_BIT);
        text++;
    }
}

void i2c_lcd_clear(void) {
    i2c_lcd_send_byte(LCD_CLEARDISPLAY, 0);
    usleep(2000);
}

esp_err_t i2c_lcd_is_alive(void) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret;
}

void i2c_lcd_set_backlight(bool on) {
    s_backlight_on = on;
    i2c_lcd_write_byte(0x00);
}
