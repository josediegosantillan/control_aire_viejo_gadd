#include "i2c_lcd.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h" // Vital para retardos precisos en microsegundos

static const char *TAG = "LCD";
#define I2C_NUM I2C_NUM_0

// Almacena la dirección I2C (0x27 o 0x3F)
static uint8_t _addr;

// Máscaras de bits estándar para el chip PCF8574
// P0=RS, P1=RW, P2=EN, P3=Backlight, P4-P7=Datos
#define LCD_RS_CMD 0x00
#define LCD_RS_DATA 0x01
#define LCD_RW     0x02
#define LCD_EN     0x04
#define LCD_BL     0x08 // Backlight ON

// --- FUNCIONES PRIVADAS (Low Level) ---

/**
 * @brief Envía 4 bits al LCD manejando el pulso de Enable manualmente.
 * Esta versión es "lenta" a propósito para evitar errores por ruido o cables largos.
 */
static void lcd_write_nibble(uint8_t nibble, uint8_t mode) {
    uint8_t data = (nibble & 0xF0) | mode | LCD_BL;
    uint8_t packet[1];
    
    // 1. Preparar datos en el bus (Enable LOW)
    packet[0] = data;
    i2c_master_write_to_device(I2C_NUM, _addr, packet, 1, 100);
    esp_rom_delay_us(50); // Tiempo de setup

    // 2. Pulso Enable HIGH
    packet[0] = data | LCD_EN;
    i2c_master_write_to_device(I2C_NUM, _addr, packet, 1, 100);
    esp_rom_delay_us(600); // Mantenemos el pulso 600us (El LCD requiere >450ns, exageramos por seguridad)

    // 3. Pulso Enable LOW (Latch)
    packet[0] = data; 
    i2c_master_write_to_device(I2C_NUM, _addr, packet, 1, 100);
    esp_rom_delay_us(50); // Tiempo de hold
}

/**
 * @brief Envía un byte completo dividiéndolo en dos nibbles (Alto y Bajo)
 */
static void lcd_send_byte(uint8_t val, uint8_t mode) {
    lcd_write_nibble(val & 0xF0, mode); // Enviar 4 bits altos
    lcd_write_nibble(val << 4, mode);   // Enviar 4 bits bajos
}

// --- FUNCIONES PÚBLICAS (API) ---

/**
 * @brief Verifica si el LCD responde al bus (Ping)
 */
esp_err_t i2c_lcd_is_alive(void) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    
    // Intentamos escribir la dirección. Si el chip existe, devuelve ACK.
    i2c_master_write_byte(cmd, (_addr << 1) | I2C_MASTER_WRITE, true);
    
    i2c_master_stop(cmd);
    
    // Ejecutamos con timeout corto (50ms)
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM, cmd, 50 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    
    return ret;
}

/**
 * @brief Inicialización completa con secuencia de Reset de fábrica
 */
void i2c_lcd_init(uint8_t addr) {
    _addr = addr;
    
    // Espera inicial para que se estabilice el voltaje del LCD
    vTaskDelay(pdMS_TO_TICKS(100));

    // --- SECUENCIA DE RESET MÁGICA (Datasheet HD44780) ---
    // Esto descongela al LCD si quedó en un estado intermedio
    lcd_write_nibble(0x30, LCD_RS_CMD);
    vTaskDelay(pdMS_TO_TICKS(10)); // Esperar >4.1ms
    
    lcd_write_nibble(0x30, LCD_RS_CMD);
    vTaskDelay(pdMS_TO_TICKS(1));  // Esperar >100us
    
    lcd_write_nibble(0x30, LCD_RS_CMD);
    vTaskDelay(pdMS_TO_TICKS(1));

    // Pasar a modo 4-bits
    lcd_write_nibble(0x20, LCD_RS_CMD);
    vTaskDelay(pdMS_TO_TICKS(1));

    // --- CONFIGURACIÓN DE PANTALLA ---
    // Function Set: 4-bit, 2 lineas, fuente 5x8
    lcd_send_byte(0x28, LCD_RS_CMD);
    
    // Display Control: Apagar pantalla
    lcd_send_byte(0x08, LCD_RS_CMD);
    
    // Clear Display: Borrar todo
    lcd_send_byte(0x01, LCD_RS_CMD);
    vTaskDelay(pdMS_TO_TICKS(10)); // Clear tarda bastante, damos 10ms
    
    // Entry Mode: Incrementar cursor a la derecha
    lcd_send_byte(0x06, LCD_RS_CMD);

    // Display Control: Prender pantalla, sin cursor, sin parpadeo
    lcd_send_byte(0x0C, LCD_RS_CMD);
    
    ESP_LOGI(TAG, "LCD Inicializado OK (Dir: 0x%02X)", addr);
}

/**
 * @brief Borra la pantalla
 */
void i2c_lcd_clear(void) {
    lcd_send_byte(0x01, LCD_RS_CMD);
    vTaskDelay(pdMS_TO_TICKS(5)); // Espera obligatoria
}

/**
 * @brief Escribe texto en la posición indicada
 */
void i2c_lcd_write_text(uint8_t row, uint8_t col, const char *text) {
    // Mapa de memoria para LCDs 16x2 y 20x4
    int row_offsets[] = { 0x00, 0x40, 0x14, 0x54 };
    
    // Protección contra filas inexistentes
    if (row > 3) row = 0;
    
    // 0x80 es el comando para "Set DDRAM Address"
    uint8_t pos_addr = 0x80 | (col + row_offsets[row]);
    
    // Mover cursor
    lcd_send_byte(pos_addr, LCD_RS_CMD);

    // Escribir caracter por caracter
    while (*text) {
        lcd_send_byte((uint8_t)(*text), LCD_RS_DATA);
        text++;
    }
}