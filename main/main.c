#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" // 👈 Mutex para seguridad
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h" 
#include "driver/gpio.h"
#include "driver/i2c.h"

// Librerías
#include "esp_wifi.h"       
#include "esp_netif.h"      
#include "nvs_flash.h"       
#include "cJSON.h"           // 👈 Para leer las órdenes de Node-RED

// Componentes
#include "wifi_portal.h"    
#include "ac_config.h"      
#include "ac_meter.h" 
#include "ac_storage.h"      // 👈 Para guardar config (Persistence)      
#include "ds18b20.h"        
#include "i2c_lcd.h"
#include "mqtt_connector.h" 

static const char *TAG = "MAIN_SYSTEM";

// --- PINES ---
#define PIN_ONEWIRE GPIO_NUM_4  
#define PIN_ZMPT    GPIO_NUM_34 
#define PIN_SCT     GPIO_NUM_35 

// IDs Sensores (Hardcodeados)
const ds18b20_addr_t ID_COIL = { {0x28, 0xF4, 0xD6, 0x57, 0x04, 0xE1, 0x3C, 0x1E} };
const ds18b20_addr_t ID_AMB  = { {0x28, 0xB5, 0x6C, 0x54, 0x00, 0x00, 0x00, 0x14} };
const ds18b20_addr_t ID_OUT  = { {0x28, 0xB9, 0x31, 0x55, 0x00, 0x00, 0x00, 0x9F} };

// Mutex para proteger la variable sys
static SemaphoreHandle_t xMutexSys = NULL;

struct SystemState {
    float t_amb, t_out, t_coil;
    float volt, amp, watt;
    
    // Configuración persistente (coincide con ac_storage.h)
    sys_config_t cfg; 
    
    bool comp_active, freeze_mode, protection_wait;
} sys;

int64_t last_comp_stop_time = - (SAFETY_DELAY_MIN * 60 * 1000000LL);

// --- AUXILIARES ---
static esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_I2C_SDA, 
        .scl_io_num = PIN_I2C_SCL, 
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 10000, 
    };
    i2c_param_config(I2C_NUM_0, &conf);
    return i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
}

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

int get_wifi_status() {
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK) return 0; 
    if (mode == WIFI_MODE_APSTA) return 2; 
    if (mode == WIFI_MODE_STA) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) return 1;
        }
    }
    return 0; 
}

// --- 📡 CALLBACK DE RECEPCIÓN MQTT (El cerebro que faltaba) ---
void mqtt_data_handler(const char *topic, int topic_len, const char *data, int data_len) {
    // Verificar tópico
    if (strncmp(topic, MQTT_TOPIC_CONFIG, topic_len) == 0) {
        ESP_LOGI(TAG, "📩 Orden recibida: %.*s", data_len, data);

        cJSON *root = cJSON_ParseWithLength(data, data_len);
        if (root) {
            // Extraer datos del JSON de Node-RED
            cJSON *j_on = cJSON_GetObjectItem(root, "on");
            cJSON *j_fan = cJSON_GetObjectItem(root, "fan");
            cJSON *j_sp = cJSON_GetObjectItem(root, "sp");

            // 🛡️ ZONA SEGURA (MUTEX)
            if (xSemaphoreTake(xMutexSys, pdMS_TO_TICKS(200)) == pdTRUE) {
                
                // Actualizar variables globales
                if (j_on) {
                    sys.cfg.system_on = cJSON_IsTrue(j_on);
                    ESP_LOGI(TAG, "CMD: Sistema %s", sys.cfg.system_on ? "ON" : "OFF");
                }
                
                if (j_fan) {
                    int speed = j_fan->valueint;
                    if (speed >= 0 && speed <= 3) sys.cfg.fan_speed = speed;
                }

                if (j_sp) {
                    float sp = j_sp->valuedouble;
                    if (sp >= 16.0 && sp <= 30.0) sys.cfg.setpoint = sp;
                }

                // Guardar en Flash para que no se borre al reiniciar
                storage_save(&sys.cfg);

                // Copiar estados para actuar YA (sin bloquear mutex)
                bool current_on = sys.cfg.system_on;
                int current_fan = sys.cfg.fan_speed;
                bool comp_state = sys.comp_active;

                xSemaphoreGive(xMutexSys); // 🔓 Liberar

                // Aplicar cambios físicos inmediatos
                if (!current_on) {
                    set_relays(false, 0); // Apagar todo
                } else {
                    set_relays(comp_state, current_fan); // Actualizar ventilador, mantener compresor
                }

            } else {
                ESP_LOGW(TAG, "⚠️ Sistema ocupado, ignorando comando MQTT");
            }
            cJSON_Delete(root);
        }
    }
}

// --- UI 20x4 ---
void task_ui(void *pv) {
    char buffer[32];
    struct SystemState sys_copy; // Copia local para no trabar el sistema

    i2c_master_init();
    i2c_lcd_init(I2C_lcd_addr);
    i2c_lcd_clear();
    i2c_lcd_write_text(0, 0, "   GADD CLIMA v7.0  ");
    vTaskDelay(pdMS_TO_TICKS(1500));
    i2c_lcd_clear();

    while(1) {
        if (i2c_lcd_is_alive() != ESP_OK) {
            i2c_driver_delete(I2C_NUM_0);
            vTaskDelay(pdMS_TO_TICKS(100));
            i2c_master_init();
            i2c_lcd_init(I2C_lcd_addr);
        }

        // 📸 FOTO INSTANTÁNEA DE LOS DATOS (Thread-Safe)
        if (xSemaphoreTake(xMutexSys, pdMS_TO_TICKS(100)) == pdTRUE) {
            memcpy(&sys_copy, &sys, sizeof(struct SystemState));
            xSemaphoreGive(xMutexSys);
        }

        int wifi_state = get_wifi_status();
        bool mqtt_ok = mqtt_app_is_connected(); 

        // Renglón 0
        if (sys_copy.freeze_mode) i2c_lcd_write_text(0, 0, "ALERTA: CONGELADO!  ");
        else if (sys_copy.protection_wait) {
             int wait = (SAFETY_DELAY_MIN * 60) - ((esp_timer_get_time() - last_comp_stop_time) / 1000000);
             if (wait < 0) wait = 0;
             snprintf(buffer, 32, "ESPERA: %ds          ", wait);
             i2c_lcd_write_text(0, 0, buffer);
        } else if (sys_copy.cfg.system_on) {
             snprintf(buffer, 32, "FRIO:%s FAN:%d SP:%.0f", sys_copy.comp_active?"ON ":"OFF", sys_copy.cfg.fan_speed, sys_copy.cfg.setpoint);
             i2c_lcd_write_text(0, 0, buffer);
        } else i2c_lcd_write_text(0, 0, "SISTEMA APAGADO     ");

        // Renglón 1
        snprintf(buffer, 32, "Amb:%.1f  Coil:%.1f ", sys_copy.t_amb, sys_copy.t_coil);
        i2c_lcd_write_text(1, 0, buffer);

        // Renglón 2
        snprintf(buffer, 32, "%.1fV %.1fA %.0fW    ", sys_copy.volt, sys_copy.amp, sys_copy.watt);
        i2c_lcd_write_text(2, 0, buffer);

        // Renglón 3
        char w = (wifi_state==1)?'*':'!'; if(wifi_state==2) w='C';
        snprintf(buffer, 32, "WiFi:%c MQTT:%c OK    ", w, mqtt_ok?'M':' ');
        i2c_lcd_write_text(3, 0, buffer);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// --- CLIMA + MQTT ---
void task_climate(void *pv) {
    ds18b20_init_bus(PIN_ONEWIRE);
    esp_task_wdt_add(NULL);
    float t_read = 0.0;
    char json[150];

    while(1) {
        // 1. Lectura Sensores (Lenta, afuera del mutex)
        if (ds18b20_convert_all(PIN_ONEWIRE) == ESP_OK) {
            float ta=0, to=0, tc=0;
            if (ds18b20_read_one(PIN_ONEWIRE, ID_AMB, &ta) == ESP_OK) t_read=ta; 
            // Truco: leemos en variables locales primero
            if (ds18b20_read_one(PIN_ONEWIRE, ID_AMB, &ta) == ESP_OK) {}; // Dummy read fix
            ds18b20_read_one(PIN_ONEWIRE, ID_AMB, &ta);
            ds18b20_read_one(PIN_ONEWIRE, ID_OUT, &to);
            ds18b20_read_one(PIN_ONEWIRE, ID_COIL, &tc);

            // 2. Lógica de Control (Rápida, con Mutex)
            if (xSemaphoreTake(xMutexSys, pdMS_TO_TICKS(500)) == pdTRUE) {
                sys.t_amb = ta; sys.t_out = to; sys.t_coil = tc;

                // Lógica de termostato y protecciones
                if (!sys.freeze_mode && sys.t_coil < FREEZE_LIMIT_C) {
                    sys.freeze_mode = true;
                    sys.comp_active = false;
                    set_relays(false, 3);
                    last_comp_stop_time = esp_timer_get_time();
                } else if (sys.freeze_mode) {
                    if (sys.t_coil > FREEZE_RESET_C) sys.freeze_mode = false;
                } else if (sys.cfg.system_on) {
                    if (sys.t_amb > (sys.cfg.setpoint + 1.0) && !sys.comp_active) {
                        if (is_safe_to_start()) {
                            sys.comp_active = true;
                            set_relays(true, sys.cfg.fan_speed);
                            sys.protection_wait = false;
                        } else sys.protection_wait = true;
                    } else if (sys.t_amb < (sys.cfg.setpoint - 1.0) && sys.comp_active) {
                        sys.comp_active = false;
                        set_relays(false, sys.cfg.fan_speed);
                        last_comp_stop_time = esp_timer_get_time();
                        sys.protection_wait = false;
                    }
                } else {
                    sys.comp_active = false;
                    set_relays(false, 0);
                }
                
                // Armar JSON para enviar (con datos consistentes)
                snprintf(json, sizeof(json), 
                    "{\"v\":%.1f,\"a\":%.2f,\"amb\":%.2f,\"coil\":%.2f,\"on\":%d,\"fan\":%d}", 
                    sys.volt, sys.amp, sys.t_amb, sys.t_coil, sys.comp_active, sys.cfg.fan_speed);
                
                xSemaphoreGive(xMutexSys); // 🔓
            }
        }

        // 3. Enviar Telemetría
        if (mqtt_app_is_connected()) {
            mqtt_app_publish(MQTT_TOPIC_TELEMETRY, json);
        }
        
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void task_meter(void *pv) {
    float v, i, w;
    while(1) {
        ac_meter_read_rms(&v, &i, &w);
        if (xSemaphoreTake(xMutexSys, pdMS_TO_TICKS(50)) == pdTRUE) {
            sys.volt = v; sys.amp = i; sys.watt = w;
            xSemaphoreGive(xMutexSys);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void app_main(void) {
    // 1. Iniciar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Crear Mutex (CRITICO)
    xMutexSys = xSemaphoreCreateMutex();

    gpio_install_isr_service(0);
    ac_meter_init(PIN_ZMPT, PIN_SCT);
    storage_init(); // Iniciar sistema de guardado

    gpio_reset_pin(PIN_COMPRESOR); gpio_set_direction(PIN_COMPRESOR, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_FAN_L); gpio_set_direction(PIN_FAN_L, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_FAN_M); gpio_set_direction(PIN_FAN_M, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_FAN_H); gpio_set_direction(PIN_FAN_H, GPIO_MODE_OUTPUT);
    set_relays(false, 0);

    // 3. Cargar configuración guardada (o defaults)
    if (!storage_load(&sys.cfg)) {
        sys.cfg.setpoint = 24.0;
        sys.cfg.fan_speed = 1;
        sys.cfg.system_on = true;
    }
    sys.t_amb = 25.0; sys.t_coil = 20.0; sys.t_out = 20.0;

    wifi_portal_init(); 
    
    // 4. Configurar MQTT con el Callback (EL ESLABÓN PERDIDO)
    mqtt_app_set_rx_callback(mqtt_data_handler); 
    mqtt_app_start(); 
    
    esp_task_wdt_config_t wdt_conf = { .timeout_ms = WDT_TIMEOUT_MS, .trigger_panic = true };
    if (esp_task_wdt_status(NULL) != ESP_OK) esp_task_wdt_init(&wdt_conf);
    
    xTaskCreate(task_climate, "Climate", 4096, NULL, 5, NULL);
    xTaskCreate(task_meter, "Meter", 4096, NULL, 3, NULL);
    xTaskCreate(task_ui, "UI", 4096, NULL, 2, NULL);
    
    ESP_LOGI(TAG, "Sistema v7.1 (Full Control + Persistence) INICIADO");
}