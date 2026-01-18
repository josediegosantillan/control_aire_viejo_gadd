#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Definición del "Manejador" (Handle) del sensor. 
// Es un puntero oculto para manejar varias instancias.
typedef struct ds18b20_context_t* ds18b20_device_handle_t;

/**
 * @brief Inicializa un nuevo sensor DS18B20 en un pin específico.
 * * @param pin El GPIO donde conectaste el sensor (ej: GPIO_NUM_4).
 * @param handle Puntero donde se guardará la referencia al sensor creado.
 * @return esp_err_t ESP_OK si salió todo bien.
 */
esp_err_t ds18b20_new_device(gpio_num_t pin, ds18b20_device_handle_t *handle);

/**
 * @brief Realiza la conversión y lectura de temperatura.
 * * NOTA: Esta función es BLOQUEANTE. Tarda aprox 750ms en retornar 
 * porque espera a que el sensor termine de convertir. 
 * Durante esa espera, cede el control al RTOS (vTaskDelay), no clava la CPU.
 * * @param handle El manejador del sensor que querés leer.
 * @param temp Puntero donde se guardará el valor flotante (Grados Celsius).
 * @return esp_err_t ESP_OK si la lectura fue exitosa (CRC ok).
 */
esp_err_t ds18b20_read_temp(ds18b20_device_handle_t handle, float *temp);

/**
 * @brief Libera la memoria del sensor (si alguna vez necesitaras borrarlo).
 */
void ds18b20_delete_device(ds18b20_device_handle_t handle);

#ifdef __cplusplus
}
#endif
