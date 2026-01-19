#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Dirección común de displays I2C (PCF8574)
// Si no te anda, probá cambiar a 0x3F
#define I2C_lcd_addr 0x27 

/**
 * @brief Inicializa el LCD.
 * @param addr Dirección I2C (usualmente I2C_lcd_addr)
 */
void i2c_lcd_init(uint8_t addr);

/**
 * @brief Borra todo el contenido de la pantalla.
 */
void i2c_lcd_clear(void);

/**
 * @brief Escribe texto en una posición específica.
 * @param row Fila (0 a 3)
 * @param col Columna (0 a 19)
 * @param text Cadena de texto a mostrar
 */
void i2c_lcd_write_text(uint8_t row, uint8_t col, const char *text);

#ifdef __cplusplus
}
#endif