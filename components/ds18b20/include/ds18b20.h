#pragma once
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Estructura para almacenar la dirección ROM de 8 bytes
typedef struct {
    uint8_t addr[8];
} ds18b20_addr_t;

/**
 * @brief Inicializa el bus OneWire en el pin especificado.
 */
void ds18b20_init_bus(gpio_num_t pin);

/**
 * @brief Manda a convertir temperatura a TODOS los sensores (Skip ROM).
 * IMPORTANTE: Esperar 750ms después de llamar a esto antes de leer.
 */
esp_err_t ds18b20_convert_all(gpio_num_t pin);

/**
 * @brief Lee la temperatura de UN sensor específico usando su ID (Match ROM).
 * Evita colisiones en el bus.
 */
esp_err_t ds18b20_read_one(gpio_num_t pin, ds18b20_addr_t address, float *temp);

#ifdef __cplusplus
}
#endif