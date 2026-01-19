#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Definición de una dirección ROM (8 bytes) para identificar cada sensor
typedef struct {
    uint8_t addr[8];
} ds18b20_addr_t;

/**
 * @brief Inicializa el pin GPIO para el bus OneWire.
 * Llamar UNA sola vez al principio (en task_climate).
 */
void ds18b20_init_bus(gpio_num_t pin);

/**
 * @brief Envía la orden de conversión a TODOS los sensores del bus.
 * Esta función es BLOQUEANTE: espera los 750ms necesarios (vTaskDelay) 
 * antes de retornar, para asegurar que la conversión terminó.
 * @param pin El GPIO del bus.
 */
esp_err_t ds18b20_convert_all(gpio_num_t pin);

/**
 * @brief Lee la temperatura de un sensor específico usando su ID (Match ROM).
 * NO espera conversión (asume que ya llamaste a convert_all antes).
 * La lectura es rápida (~15-20ms).
 * * @param pin El GPIO del bus.
 * @param address La dirección de 8 bytes del sensor (ej: 0x28, 0xBB...).
 * @param temp Puntero donde guardar el valor flotante.
 */
esp_err_t ds18b20_read_one(gpio_num_t pin, ds18b20_addr_t address, float *temp);

#ifdef __cplusplus
}
#endif
