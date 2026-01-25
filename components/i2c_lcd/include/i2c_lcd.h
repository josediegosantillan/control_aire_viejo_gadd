#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Dirección I2C (Cambiá a 0x3F si no anda con 0x27)
#define I2C_lcd_addr 0x27 

/**
 * @brief Inicializa el LCD.
 */
void i2c_lcd_init(uint8_t addr);

/**
 * @brief Borra la pantalla.
 */
void i2c_lcd_clear(void);

/**
 * @brief Escribe texto.
 */
void i2c_lcd_write_text(uint8_t row, uint8_t col, const char *text);

/**
 * @brief Verifica si el LCD responde al bus I2C (Ping).
 * @return ESP_OK si responde (ACK), ESP_FAIL si no (NACK/Timeout).
 */
esp_err_t i2c_lcd_is_alive(void);

/**
 * @brief Re-inicializa el LCD para recuperar el modo si se corrompe.
 */
void i2c_lcd_reinit(void);

/**
 * @brief Enciende o apaga el backlight del LCD.
 */
void i2c_lcd_set_backlight(bool on);

#ifdef __cplusplus
}
#endif
