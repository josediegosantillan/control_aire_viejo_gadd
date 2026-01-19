#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h" 
#include "driver/gpio.h"
#include "driver/i2c.h"

// Librerías de WiFi y Sistema
#include "esp_wifi.h"       
#include "esp_netif.h"      
#include "nvs_flash.h"       
#include "wifi_portal.h"    

// Componentes propios
#include "ac_config.h"      
#include "ac_meter.h"       
#include "ds18b20.h"        
#include "i2c_lcd.h"        

static const char *TAG = "MAIN_SYSTEM";

// --- PINES ---
#define PIN_ONEWIRE GPIO_NUM_4  

// --- IDs SENSORES (Asegurate que sean los que escaneaste) ---
// Reemplaza estos valores si el escáner te dio otros distintos
const ds18b20_addr_t ID_COIL = { {0x28, 0xF4, 0xD6, 0x57, 0x04, 0xE1, 0x3C, 0x1E} };
const ds18b20_addr_t ID_AMB  = { {0x28, 0xB5, 0x6C, 0x54, 0x00, 0x00, 0x00, 0x14} };
const ds18b20_addr_t ID_OUT  = { {0x28, 0xB9, 0x31, 0x55, 0x00, 0x00, 0x00, 0x9F} };

// --- ESTADO GLOBAL ---
struct SystemState {
    float t_amb;    
    float t_out;    
    float t_coil;   
    float volt, amp, watt;
    
    struct {
        float setpoint;
        int fan_speed;
        bool system_on; 
    } cfg;

    bool comp_active;       
    bool freeze_mode;       
    bool protection_wait;
} sys;

int64_t last_comp_stop_time = - (SAFETY_DELAY_MIN * 60 * 1000000LL);

// --- I2C CONFIG ---
static esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_I2C_SDA, 
        .scl_io_num = PIN_I2C_SCL, 
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 10000, // 10kHz para máxima estabilidad
    };
    i2c_param_config(I2C_NUM_0, &conf);
    return i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
}

// --- AUXILIARES ---
bool is_safe_to_start() {
    int64_t now = esp_timer_get_time();
    int64_t safe_time = SAFETY_DELAY_MIN * 60 * 1000000LL;
    if (now < safe_time) return false;
    if ((now - last_comp_stop_time) < safe_time) return false;
    return true;
}

void set_relays(bool comp, int fan_speed) {
    gpio_set_level(PIN_COMPRESOR, comp ? 0 : 1);
    gpio_set_level(PIN_FAN_L, (fan_speed == 1) ? 0 : 1);
    gpio_set_level(PIN_FAN_M, (fan_speed == 2) ? 0 : 1);
    gpio_set_level(PIN_FAN_H, (fan_speed == 3) ? 0 : 1);
}

// --- HELPER: DETECTAR ESTADO WIFI ---
int get_wifi_status() {
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK) return 0; 

    if (mode == WIFI_MODE_APSTA) return 2; // Modo Configuración

    if (mode == WIFI_MODE_STA) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                if (ip_info.ip.addr != 0) return 1; // Conectado OK
            }
        }
    }
    return 0; 
}

// --- TAREA UI (PANTALLA) ---
void task_ui(void *pv) {
    char buffer[21]; 
    int error_count = 0;
    int refresh_counter = 0; 
    
    i2c_master_init();
    i2c_lcd_init(I2C_lcd_addr);
    i2c_lcd_clear();
    
    i2c_lcd_write_text(0, 0, "   GADD CLIMA v5.0  ");
    vTaskDelay(pdMS_TO_TICKS(1500));
    i2c_lcd_clear();

    while(1) {
        // 1. Auto-Curación LCD
        if (i2c_lcd_is_alive() != ESP_OK) {
            error_count++;
            if (error_count >= 3) {
                i2c_driver_delete(I2C_NUM_0);
                vTaskDelay(pdMS_TO_TICKS(100));
                i2c_master_init();
                i2c_lcd_init(I2C_lcd_addr);
                error_count = 0;
            }
        } else error_count = 0;

        refresh_counter++;
        if (refresh_counter > 60) {
            i2c_lcd_init(I2C_lcd_addr); 
            refresh_counter = 0;
        }

        int wifi_state = get_wifi_status();

        // 2. DIBUJAR PANTALLA
        
        // --- FILA 0 ---
        if (wifi_state == 2) {
            // Nombre de la red para configurar
            i2c_lcd_write_text(0, 0, "Aire_Lennox_GaddBar "); 
        } 
        else if (sys.freeze_mode) {
            i2c_lcd_write_text(0, 0, "ALERTA: CONGELADO!  ");
        } 
        else if (sys.protection_wait) {
            i2c_lcd_write_text(0, 0, "ESPERA SEGURIDAD... ");
        } 
        else if (sys.cfg.system_on) {
            if (sys.comp_active) i2c_lcd_write_text(0, 0, "FRIO: ON  | FAN: ");
            else                 i2c_lcd_write_text(0, 0, "FRIO: OFF | FAN: ");
            
            char fan_char = '0' + sys.cfg.fan_speed;
            char fan_str[2] = {fan_char, 0};
            i2c_lcd_write_text(0, 17, fan_str);
        } else {
            i2c_lcd_write_text(0, 0, "SISTEMA APAGADO     ");
        }

        // --- FILA 1 ---
        char wifi_icon = '!';
        if (wifi_state == 1) wifi_icon = '*'; 
        else if (wifi_state == 2) wifi_icon = 'C'; 

        snprintf(buffer, 21, "A:%.1f C:%.1f WiFi:%c", sys.t_amb, sys.t_coil, wifi_icon);
        i2c_lcd_write_text(1, 0, buffer);
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// --- TAREA CLIMA (CONTROL + LECTURA SEGURA) ---
void task_climate(void *pv) {
    ds18b20_init_bus(PIN_ONEWIRE);
    esp_task_wdt_add(NULL);
    
    float temp_reading = 0.0; // Variable temporal para validar

    while(1) {
        // 1. LEER SENSORES
        if (ds18b20_convert_all(PIN_ONEWIRE) == ESP_OK) {
            
            // --- AMBIENTE ---
            if (ds18b20_read_one(PIN_ONEWIRE, ID_AMB, &temp_reading) == ESP_OK) {
                // Filtro: Si da -0.0625 aprox, ignorar.
                if (temp_reading > -0.07 && temp_reading < -0.05) {
                    ESP_LOGW(TAG, "Glitch (-0.06) en AMB ignorado.");
                } else {
                    sys.t_amb = temp_reading;
                }
            }

            // --- SALIDA ---
            if (ds18b20_read_one(PIN_ONEWIRE, ID_OUT, &temp_reading) == ESP_OK) {
                if (temp_reading > -0.07 && temp_reading < -0.05) {
                    ESP_LOGW(TAG, "Glitch (-0.06) en OUT ignorado.");
                } else {
                    sys.t_out = temp_reading;
                }
            }

            // --- COIL (CRÍTICO) ---
            if (ds18b20_read_one(PIN_ONEWIRE, ID_COIL, &temp_reading) == ESP_OK) {
                if (temp_reading > -0.07 && temp_reading < -0.05) {
                    ESP_LOGW(TAG, ">>> GLITCH (-0.06) EN COIL - IGNORADO <<<");
                    // NO actualizamos sys.t_coil, mantenemos el valor viejo
                } else {
                    sys.t_coil = temp_reading;
                }
            }
            
            // LOG DE CONTROL
            ESP_LOGI(TAG, "Sensores -> Amb: %.2f | Coil: %.2f | Out: %.2f", 
                     sys.t_amb, sys.t_coil, sys.t_out);

        } else {
            ESP_LOGE(TAG, "Error Bus OneWire (Fallo Convert All)");
        }

        // 2. LÓGICA DE CONTROL
        
        // A. Protección Anti-Hielo
        if (!sys.freeze_mode && sys.t_coil < FREEZE_LIMIT_C) {
            ESP_LOGW(TAG, ">>> ALERTA DE HIELO REAL (%.2f C) <<<", sys.t_coil);
            sys.freeze_mode = true;
            sys.comp_active = false; 
            set_relays(false, 3); // Apagar Compresor, Fan al MAX
            last_comp_stop_time = esp_timer_get_time();
        }
        else if (sys.freeze_mode) {
            if (sys.t_coil > FREEZE_RESET_C) {
                ESP_LOGI(TAG, "Hielo despejado. Sistema normalizado.");
                sys.freeze_mode = false;
            }
        }
        
        // B. Termostato Normal
        else if (sys.cfg.system_on) {
            if (sys.t_amb > (sys.cfg.setpoint + 1.0) && !sys.comp_active) {
                if (is_safe_to_start()) {
                    sys.comp_active = true;
                    set_relays(true, sys.cfg.fan_speed);
                    sys.protection_wait = false;
                } else {
                    sys.protection_wait = true;
                }
            } else if (sys.t_amb < (sys.cfg.setpoint - 1.0) && sys.comp_active) {
                sys.comp_active = false;
                set_relays(false, sys.cfg.fan_speed);
                last_comp_stop_time = esp_timer_get_time();
                sys.protection_wait = false;
            }
        } else {
            // Sistema Apagado
            sys.comp_active = false;
            set_relays(false, 0);
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// --- TAREA MEDICIÓN ENERGÍA ---
void task_meter(void *pv) {
    while(1) {
        ac_meter_read_rms(&sys.volt, &sys.amp, &sys.watt);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// --- MAIN APP ---
void app_main(void) {
    // 1. Iniciar NVS (Obligatorio)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Hardware
    gpio_install_isr_service(0);
    ac_meter_init();
    
    gpio_reset_pin(PIN_COMPRESOR); gpio_set_direction(PIN_COMPRESOR, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_FAN_L); gpio_set_direction(PIN_FAN_L, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_FAN_M); gpio_set_direction(PIN_FAN_M, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_FAN_H); gpio_set_direction(PIN_FAN_H, GPIO_MODE_OUTPUT);
    set_relays(false, 0);

    // Configuración Inicial
    sys.cfg.setpoint = 10.0;
    sys.cfg.fan_speed = 1;
    sys.cfg.system_on = true;
    // Inicializamos temperaturas en valores seguros para que no dispare alarmas al arrancar
    sys.t_amb = 25.0; 
    sys.t_coil = 20.0;
    sys.t_out = 20.0;

    // 3. INICIAR PORTAL WIFI
    wifi_portal_init(); 
    
    // Watchdog
    esp_task_wdt_config_t wdt_conf = { .timeout_ms = WDT_TIMEOUT_MS, .trigger_panic = true };
    if (esp_task_wdt_status(NULL) != ESP_OK) esp_task_wdt_init(&wdt_conf);
    
    // Tareas
    xTaskCreate(task_climate, "Climate", 4096, NULL, 5, NULL);
    xTaskCreate(task_meter, "Meter", 4096, NULL, 3, NULL);
    xTaskCreate(task_ui, "UI", 4096, NULL, 2, NULL);
    
    ESP_LOGI(TAG, "Sistema Arrancado. Version 5.0 Stable");
}