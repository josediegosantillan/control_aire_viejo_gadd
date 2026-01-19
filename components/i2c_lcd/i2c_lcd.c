#include "i2c_lcd.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h" // Para usar retardos en microsegundos si hace falta

static const char *TAG = "LCD";
#define I2C_NUM I2C_NUM_0

static uint8_t _addr;

// Máscaras de bits comunes para backpacks PCF8574 genéricos
// P0=RS, P1=RW, P2=EN, P3=Backlight, P4-P7=Data
#define LCD_RS_CMD 0x00
#define LCD_RS_DATA 0x01
#define LCD_RW 0x02
#define LCD_EN 0x04
#define LCD_BL 0x08 // Backlight ON

// Función para escribir 4 bits al expansor
static void lcd_write_nibble(uint8_t nibble, uint8_t mode) {
    uint8_t data = (nibble & 0xF0) | mode | LCD_BL; // Mantener Backlight ON
    uint8_t packet[1];
    
    // 1. Poner datos en el bus (Enable LOW)
    packet[0] = data;
    i2c_master_write_to_device(I2C_NUM, _addr, packet, 1, 100);
    esp_rom_delay_us(50); // Pequeña espera para estabilizar

    // 2. Pulse ENABLE HIGH (Latch)
    packet[0] = data | LCD_EN;
    i2c_master_write_to_device(I2C_NUM, _addr, packet, 1, 100);
    esp_rom_delay_us(600); // El LCD necesita >450ns, le damos 600us por seguridad

    // 3. Pulse ENABLE LOW
    packet[0] = data; // EN=0
    i2c_master_write_to_device(I2C_NUM, _addr, packet, 1, 100);
    esp_rom_delay_us(100); 
}

static void lcd_send_byte(uint8_t val, uint8_t mode) {
    // Enviar parte ALTA (High Nibble)
    lcd_write_nibble(val & 0xF0, mode);
    // Enviar parte BAJA (Low Nibble)
    lcd_write_nibble(val << 4, mode);
}

void i2c_lcd_init(uint8_t addr) {
    _addr = addr;
    
    // Espera inicial fuerte para que suba el voltaje del LCD
    vTaskDelay(pdMS_TO_TICKS(100));

    // --- SECUENCIA DE RESET MÁGICA (Datasheet HD44780) ---
    // Hay que enviarle 0x03 tres veces "a ciegas" para asegurar modo 8 bits
    // antes de pasar a 4 bits.
    
    lcd_write_nibble(0x30, LCD_RS_CMD);
    vTaskDelay(pdMS_TO_TICKS(10)); // Esperar >4.1ms
    
    lcd_write_nibble(0x30, LCD_RS_CMD);
    vTaskDelay(pdMS_TO_TICKS(1));  // Esperar >100us
    
    lcd_write_nibble(0x30, LCD_RS_CMD);
    vTaskDelay(pdMS_TO_TICKS(1));

    // Ahora sí, pasar a modo 4-bits
    lcd_write_nibble(0x20, LCD_RS_CMD);
    vTaskDelay(pdMS_TO_TICKS(1));

    // --- CONFIGURACIÓN FINAL ---
    // Function Set: 4-bit, 2 lines, 5x8 font
    lcd_send_byte(0x28, LCD_RS_CMD);
    
    // Display Control: Display OFF, Cursor OFF, Blink OFF
    lcd_send_byte(0x08, LCD_RS_CMD);
    
    // Clear Display
    lcd_send_byte(0x01, LCD_RS_CMD);
    vTaskDelay(pdMS_TO_TICKS(5)); // Clear tarda mucho (>2ms)

    // Entry Mode: Increment cursor
    lcd_send_byte(0x06, LCD_RS_CMD);

    // Display ON
    lcd_send_byte(0x0C, LCD_RS_CMD);
    
    ESP_LOGI(TAG, "LCD Inicializado (Modo Lento)");
}

void i2c_lcd_clear(void) {
    lcd_send_byte(0x01, LCD_RS_CMD);
    vTaskDelay(pdMS_TO_TICKS(5)); // Importante esperar acá
}

void i2c_lcd_write_text(uint8_t row, uint8_t col, const char *text) {
    // Mover cursor
    uint8_t pos_addr;
    // Mapeo para LCD 20x4 y 16x2 estándar
    int row_offsets[] = { 0x00, 0x40, 0x14, 0x54 };
    if (row > 3) row = 0;
    
    pos_addr = 0x80 | (col + row_offsets[row]);
    
    lcd_send_byte(pos_addr, LCD_RS_CMD);

    // Escribir cadena
    while (*text) {
        lcd_send_byte((uint8_t)(*text), LCD_RS_DATA);
        text++;
    }
}
