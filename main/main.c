#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h" 
#include "driver/gpio.h"

// Componentes propios
#include "ac_config.h"      // Pines y Configuración
#include "ac_meter.h"       // Medición eléctrica
#include "ds18b20.h"        // Driver de temperatura

static const char *TAG = "MAIN_SYSTEM";

// --- CONFIGURACIÓN DE PINES Y SENSORES ---
#define PIN_ONEWIRE GPIO_NUM_4  // ¡UN SOLO CABLE PARA LOS 3 SENSORES!

// DIRECCIONES ROM (IDs) - TUS SENSORES REALES
// 1. Caño de baja presión (Protección Hielo)
const ds18b20_addr_t ID_COIL = { {0x28, 0xBB, 0x88, 0xAF, 0x00, 0x00, 0x00, 0x27} };

// 2. Temperatura Ambiente (Termostato)
const ds18b20_addr_t ID_AMB  = { {0x28, 0xB5, 0x6C, 0x54, 0x00, 0x00, 0x00, 0x14} };

// 3. Salida Evaporador (Para calcular Delta T / Rendimiento)
const ds18b20_addr_t ID_OUT  = { {0x28, 0xB9, 0x31, 0x55, 0x00, 0x00, 0x00, 0x9F} };

// --- ESTRUCTURA GLOBAL DEL SISTEMA ---
struct SystemState {
    // Temperaturas
    float t_amb;    // Entrada de aire
    float t_out;    // Salida de aire
    float t_coil;   // Cañería (Hielo)
    
    // Electricidad
    float volt, amp, watt;
    
    // Configuración
    struct {
        float setpoint;
        int fan_speed;
        bool system_on; // ON/OFF general
    } cfg;

    // Estados Internos
    bool comp_active;       // Relé Compresor
    bool freeze_mode;       // Modo Protección Hielo
    bool protection_wait;   // Modo Espera 3 min
} sys;

// Timer para protección de ciclo corto
int64_t last_comp_stop_time = - (SAFETY_DELAY_MIN * 60 * 1000000LL);


// --- FUNCIONES AUXILIARES ---

// Chequeo de seguridad (3 minutos)
bool is_safe_to_start() {
    int64_t now = esp_timer_get_time();
    int64_t safe_time = SAFETY_DELAY_MIN * 60 * 1000000LL;

    // 1. Post-Corte de Luz
    if (now < safe_time) return false;
    // 2. Ciclo Corto
    if ((now - last_comp_stop_time) < safe_time) return false;

    return true;
}

// Control de Relés (Activos en BAJO = 0)
void set_relays(bool comp, int fan_speed) {
    gpio_set_level(PIN_COMPRESOR, comp ? 0 : 1);
    
    // Interlock simple de ventilador
    gpio_set_level(PIN_FAN_L, (fan_speed == 1) ? 0 : 1);
    gpio_set_level(PIN_FAN_M, (fan_speed == 2) ? 0 : 1);
    gpio_set_level(PIN_FAN_H, (fan_speed == 3) ? 0 : 1);
}

// --- TAREA 1: CLIMATIZACIÓN (CORE 1) ---
void task_climate(void *pv) {
    // Inicializar Bus OneWire
    ds18b20_init_bus(PIN_ONEWIRE);
    
    // Suscribir al Watchdog
    esp_task_wdt_add(NULL);

    while(1) {
        // ---------------------------------------------------------
        // 1. LECTURA DE SENSORES (Bus Compartido)
        // ---------------------------------------------------------
        
        // A. Mandar a convertir a TODOS (Bloquea 750ms)
        esp_err_t err = ds18b20_convert_all(PIN_ONEWIRE);
        
        if (err == ESP_OK) {
            // B. Leer uno por uno
            float t_reading;

            // Leer Ambiente
            if (ds18b20_read_one(PIN_ONEWIRE, ID_AMB, &t_reading) == ESP_OK) 
                sys.t_amb = t_reading;
            
            // Leer Salida (Aire)
            if (ds18b20_read_one(PIN_ONEWIRE, ID_OUT, &t_reading) == ESP_OK) 
                sys.t_out = t_reading;

            // Leer Cañería (Hielo)
            if (ds18b20_read_one(PIN_ONEWIRE, ID_COIL, &t_reading) == ESP_OK) 
                sys.t_coil = t_reading;
        } else {
            ESP_LOGE(TAG, "Error Bus OneWire: Revisar cable GPIO %d", PIN_ONEWIRE);
        }

        // ---------------------------------------------------------
        // 2. LÓGICA DE CONTROL
        // ---------------------------------------------------------

        // A. PROTECCIÓN ANTI-HIELO (Prioridad Absoluta)
        // Si el caño baja de 0°C -> Cortar todo y ventilar
        if (!sys.freeze_mode && sys.t_coil < FREEZE_LIMIT_C) {
            ESP_LOGE(TAG, "🧊 HIELO (%.1f C). Activando Deshielo.", sys.t_coil);
            sys.freeze_mode = true;
            sys.comp_active = false; 
            set_relays(false, 3); // Fan al MÁXIMO
            last_comp_stop_time = esp_timer_get_time();
        }
        else if (sys.freeze_mode) {
            // Salir solo si sube de 10°C
            if (sys.t_coil > FREEZE_RESET_C) {
                ESP_LOGI(TAG, "💧 Descongelado (%.1f C).", sys.t_coil);
                sys.freeze_mode = false;
            } else {
                set_relays(false, 3); // Seguir ventilando
            }
        }
        
        // B. CONTROL NORMAL (Termostato)
        else if (sys.cfg.system_on) {
            // Histéresis de 1 grado
            bool request_cool = false;
            if (sys.t_amb > (sys.cfg.setpoint + 1.0)) request_cool = true;
            else if (sys.t_amb < (sys.cfg.setpoint - 1.0)) request_cool = false;
            else request_cool = sys.comp_active; // Mantener

            if (request_cool) {
                if (!sys.comp_active) {
                    if (is_safe_to_start()) {
                        sys.comp_active = true;
                        set_relays(true, sys.cfg.fan_speed);
                        sys.protection_wait = false;
                        ESP_LOGI(TAG, "Compresor ON. Delta T: %.1f", sys.t_amb - sys.t_out);
                    } else {
                        sys.protection_wait = true;
                    }
                }
            } else {
                // Cortar por frío alcanzado
                if (sys.comp_active) {
                    sys.comp_active = false;
                    set_relays(false, sys.cfg.fan_speed); // Solo ventilar
                    last_comp_stop_time = esp_timer_get_time();
                }
                sys.protection_wait = false;
            }
        } 
        
        // C. STANDBY (Apagado por usuario)
        else {
            if (sys.comp_active) {
                sys.comp_active = false;
                last_comp_stop_time = esp_timer_get_time();
            }
            set_relays(false, 0); // Todo apagado
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000)); // Chequeo cada 1s
    }
}

// --- TAREA 2: MEDICIÓN ELÉCTRICA ---
void task_meter(void *pv) {
    while(1) {
        ac_meter_read_rms(&sys.volt, &sys.amp, &sys.watt);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// --- APP MAIN ---
void app_main(void) {
    ESP_LOGI(TAG, "--- GADD CLIMA v4.0 (3 Sensores) ---");

    // 1. Iniciar Hardware
    gpio_install_isr_service(0);
    ac_meter_init();
    
    // Configurar Relés
    gpio_reset_pin(PIN_COMPRESOR); gpio_set_direction(PIN_COMPRESOR, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_FAN_L); gpio_set_direction(PIN_FAN_L, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_FAN_M); gpio_set_direction(PIN_FAN_M, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_FAN_H); gpio_set_direction(PIN_FAN_H, GPIO_MODE_OUTPUT);
    
    // Apagar todo al inicio
    set_relays(false, 0);

    // 2. Configuración Inicial (Simulada de NVS)
    sys.cfg.setpoint = 24.0;
    sys.cfg.fan_speed = 1;
    sys.cfg.system_on = true; // Arranca prendido para probar

    // 3. Watchdog
    esp_task_wdt_config_t wdt_conf = { .timeout_ms = WDT_TIMEOUT_MS, .trigger_panic = true };
    esp_task_wdt_init(&wdt_conf);

    // 4. Tareas
    xTaskCreate(task_climate, "Climate", 4096, NULL, 5, NULL);
    xTaskCreate(task_meter, "Meter", 4096, NULL, 3, NULL);
    
    ESP_LOGI(TAG, "Sistema Operativo. Esperando 3 min protección...");
}