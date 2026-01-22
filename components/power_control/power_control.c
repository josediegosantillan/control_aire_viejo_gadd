/**
 * @file power_control.c
 * @brief Control de botón on/off y LEDs de estado
 * @author Diego
 */

#include "power_control.h"
#include "ac_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "POWER_CONTROL";

esp_err_t power_control_init(void) {
    // Configurar botón como entrada con pull-up
    gpio_config_t btn_config = {
        .pin_bit_mask = (1ULL << PIN_BUTTON_POWER),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    if (gpio_config(&btn_config) != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando botón");
        return ESP_FAIL;
    }

    // Configurar LEDs como salidas
    gpio_config_t led_config = {
        .pin_bit_mask = (1ULL << PIN_LED_RED) | (1ULL << PIN_LED_GREEN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    if (gpio_config(&led_config) != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando LEDs");
        return ESP_FAIL;
    }

    // Estado inicial: LED rojo encendido (sistema apagado)
    // Recuerda: Activo por LOW (0), Inactivo por HIGH (1)
    gpio_set_level(PIN_LED_RED, 0);     // Encender rojo
    gpio_set_level(PIN_LED_GREEN, 1);   // Apagar verde

    ESP_LOGI(TAG, "Botón y LEDs inicializados correctamente");
    return ESP_OK;
}

bool power_control_button_pressed(void) {
    // El botón está conectado con pull-up, así que LOW = presionado
    int level = gpio_get_level(PIN_BUTTON_POWER);
    ESP_LOGD(TAG, "Botón GPIO_%d nivel: %d", PIN_BUTTON_POWER, level);
    return (level == 0);
}

void power_control_led_red(bool on) {
    // Activo por LOW (0), Inactivo por HIGH (1)
    gpio_set_level(PIN_LED_RED, on ? 0 : 1);
    if (on) {
        ESP_LOGD(TAG, "LED Rojo encendido");
    } else {
        ESP_LOGD(TAG, "LED Rojo apagado");
    }
}

void power_control_led_green(bool on) {
    // Activo por LOW (0), Inactivo por HIGH (1)
    gpio_set_level(PIN_LED_GREEN, on ? 0 : 1);
    if (on) {
        ESP_LOGD(TAG, "LED Verde encendido");
    } else {
        ESP_LOGD(TAG, "LED Verde apagado");
    }
}

void power_control_update_leds(bool system_on) {
    if (system_on) {
        // Sistema ON: Verde encendido, Rojo apagado
        power_control_led_green(true);
        power_control_led_red(false);
    } else {
        // Sistema OFF: Rojo encendido, Verde apagado
        power_control_led_red(true);
        power_control_led_green(false);
    }
}
