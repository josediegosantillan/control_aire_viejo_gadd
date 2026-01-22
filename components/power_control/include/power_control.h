#pragma once
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa los pines de botón y LEDs
 * @return ESP_OK si todo fue bien
 */
esp_err_t power_control_init(void);

/**
 * @brief Obtiene el estado actual del botón (presionado o no)
 * @return true si está presionado, false si no
 */
bool power_control_button_pressed(void);

/**
 * @brief Controla el LED rojo (encendido por LOW)
 * @param on true para encender (LOW), false para apagar (HIGH)
 */
void power_control_led_red(bool on);

/**
 * @brief Controla el LED verde (encendido por LOW)
 * @param on true para encender (LOW), false para apagar (HIGH)
 */
void power_control_led_green(bool on);

/**
 * @brief Actualiza el estado de los LEDs según el estado del sistema
 * @param system_on true si el sistema está encendido, false si está apagado
 */
void power_control_update_leds(bool system_on);

#ifdef __cplusplus
}
#endif
