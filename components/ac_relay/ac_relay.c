#include "ac_relay.h"
#include "ac_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "RELAY";

void relay_init(void) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL<<PIN_COMPRESOR) | (1ULL<<PIN_FAN_L) | (1ULL<<PIN_FAN_M) | (1ULL<<PIN_FAN_H),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&cfg);
    
    // ESTADO SEGURO: Todo apagado (Lógica: 1=Apagado, 0=Encendido para módulos chinos comunes)
    // Ajustar según tu módulo relé. Asumimos Activo en LOW.
    gpio_set_level(PIN_COMPRESOR, 1);
    gpio_set_level(PIN_FAN_L, 1);
    gpio_set_level(PIN_FAN_M, 1);
    gpio_set_level(PIN_FAN_H, 1);
    ESP_LOGI(TAG, "Relés inicializados en OFF");
}

void relay_set_fan(int speed) { // 0:Off, 1:Low, 2:Med, 3:High
    // 1. SEGURIDAD: Apagar todo primero (Break-before-Make)
    gpio_set_level(PIN_FAN_L, 1);
    gpio_set_level(PIN_FAN_M, 1);
    gpio_set_level(PIN_FAN_H, 1);
    
    if (speed == 0) return;

    // 2. Pequeño delay para evitar chispazo en cambio rápido
    vTaskDelay(pdMS_TO_TICKS(50));

    // 3. Activar solo la solicitada
    switch(speed) {
        case 1: gpio_set_level(PIN_FAN_L, 0); break;
        case 2: gpio_set_level(PIN_FAN_M, 0); break;
        case 3: gpio_set_level(PIN_FAN_H, 0); break;
        default: break;
    }
}

void relay_set_compressor(bool state) {
    // True = Encender (Level 0), False = Apagar (Level 1)
    gpio_set_level(PIN_COMPRESOR, state ? 0 : 1);
}