#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h" // Perro Guardián
#include "driver/gpio.h"

// Componentes propios
#include "ac_config.h"
#include "ac_meter.h"
// #include "wifi_portal.h"    // Tu componente de WiFi
// #include "mqtt_connector.h" // Tu componente MQTT
#include "ds18b20.h"        // Librería del sensor

static const char *TAG = "MAIN_CTRL";

// Estado Global del Sistema
struct {
    float temp_amb;
    float temp_coil;      // Temp Cañería
    float volt, amp, watt;
    bool comp_active;     // Estado real del compresor
    bool system_on;       // Encendido/Apagado por usuario
    bool freeze_mode;     // Modo Protección Hielo
    bool protection_wait; // Modo Espera 3 min
} sys;

// Variable de tiempo para ciclo corto
int64_t last_comp_stop_time = - (SAFETY_DELAY_MIN * 60 * 1000000LL);

// --- FUNCIONES AUXILIARES ---

// Chequeo de seguridad de 3 minutos
bool is_safe_to_start() {
    int64_t now = esp_timer_get_time();
    int64_t safe_time = SAFETY_DELAY_MIN * 60 * 1000000LL;

    // 1. Protección Post-Corte de Luz (Boot time)
    if (now < safe_time) return false;
    
    // 2. Protección Ciclo Corto (Desde último stop)
    if ((now - last_comp_stop_time) < safe_time) return false;

    return true;
}

// Wrapper para relés (simplificado)
void set_relays(bool comp, int fan_speed) {
    // Lógica de relés aquí (ver código anterior de ac_relay)
    // GPIO_SET_LEVEL... recordá la lógica inversa si usás módulos chinos
    gpio_set_level(PIN_COMPRESOR, comp ? 0 : 1);
    
    // Fan logic (Interlock por software básico)
    gpio_set_level(PIN_FAN_L, (fan_speed == 1) ? 0 : 1);
    gpio_set_level(PIN_FAN_M, (fan_speed == 2) ? 0 : 1);
    gpio_set_level(PIN_FAN_H, (fan_speed == 3) ? 0 : 1);
}

// --- TAREA CLIMATIZACIÓN (CORE 1) ---
void task_climate(void *pv) {
    // 1. Inicializar Sensores DS18B20
    ds18b20_device_handle_t sensor_amb, sensor_coil;
    ds18b20_new_device(PIN_DS18B20_AMB, &sensor_amb);   // Ambiente
    ds18b20_new_device(PIN_DS18B20_COIL, &sensor_coil); // Cañería

    // 2. Suscribir al Watchdog
    esp_task_wdt_add(NULL);

    while(1) {
        // A. Leer Temperaturas
        ds18b20_read_temp(sensor_amb, &sys.temp_amb);
        ds18b20_read_temp(sensor_coil, &sys.temp_coil);

        // B. Lógica de Protección Anti-Hielo
        if (!sys.freeze_mode && sys.temp_coil < FREEZE_LIMIT_C) {
            ESP_LOGE(TAG, "🧊 HIELO DETECTADO (%.1f C)! Activando Deshielo.", sys.temp_coil);
            sys.freeze_mode = true;
            sys.comp_active = false; // Cortar bocha ya
            set_relays(false, 3);    // Fan al MÁXIMO para descongelar
        }
        else if (sys.freeze_mode) {
            if (sys.temp_coil > FREEZE_RESET_C) {
                ESP_LOGI(TAG, "💧 Descongelado (%.1f C). Volviendo a normalidad.", sys.temp_coil);
                sys.freeze_mode = false;
                last_comp_stop_time = esp_timer_get_time(); // Reiniciar timer 3 min
            } else {
                // Sigo congelado...
                set_relays(false, 3);
            }
        }
        // C. Lógica Normal (Termostato)
        else if (sys.system_on) {
            // Lógica simple de termostato
            bool request_cool = (sys.temp_amb > 25.0); // Ejemplo Setpoint 24

            if (request_cool && !sys.comp_active) {
                if (is_safe_to_start()) {
                    sys.comp_active = true;
                    set_relays(true, 1); // Fan Bajo + Compresor
                    sys.protection_wait = false;
                } else {
                    sys.protection_wait = true; // Avisar "ESPERA"
                }
            } else if (!request_cool && sys.comp_active) {
                sys.comp_active = false;
                set_relays(false, 1); // Solo ventilar
                last_comp_stop_time = esp_timer_get_time();
            }
        } 
        else {
            // Standby total
            set_relays(false, 0);
        }

        // Alimentar al perro
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "--- INICIO SISTEMA GADD V3.0 (ANTI-HIELO) ---");

    // 1. Inits de Hardware
    gpio_install_isr_service(0);
    ac_meter_init();
    
    // Configurar pines de relés como salida...
    gpio_reset_pin(PIN_COMPRESOR); gpio_set_direction(PIN_COMPRESOR, GPIO_MODE_OUTPUT);
    // (Repetir para FANs...)

    // 2. WiFi y MQTT (Tus componentes existentes)
    // wifi_portal_init(); 

    // 3. Configurar Watchdog (5 segundos)
    esp_task_wdt_config_t wdt_conf = {
        .timeout_ms = WDT_TIMEOUT_MS,
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_conf);

    // 4. Lanzar Tareas
    xTaskCreate(task_climate, "Climate", 4096, NULL, 5, NULL);
    
    // Sys On por defecto para prueba
    sys.system_on = true;
}