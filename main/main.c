#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h" 
#include "driver/gpio.h"
#include "driver/i2c.h"

// Componentes propios
#include "ac_config.h"      
#include "ac_meter.h"       
#include "ds18b20.h"        
#include "i2c_lcd.h"        

static const char *TAG = "MAIN_SYSTEM";

// --- PINES ---
#define PIN_ONEWIRE GPIO_NUM_4  

// --- IDs SENSORES (TUS VALORES REALES) ---
const ds18b20_addr_t ID_COIL = { {0x28, 0xBB, 0x88, 0xAF, 0x00, 0x00, 0x00, 0x27} };
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

// --- I2C LENTO Y SEGURO ---
static esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_I2C_SDA, 
        .scl_io_num = PIN_I2C_SCL, 
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        // ¡CAMBIO CLAVE! 10kHz para evitar ruido en cables largos
        .master.clk_speed = 10000, 
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

// --- TAREA UI ---
void task_ui(void *pv) {
    char buffer[21]; // Buffer para 20 chars + null
    
    // Iniciar I2C
    i2c_master_init();
    
    // Iniciar LCD
    ESP_LOGI(TAG, "Iniciando LCD...");
    i2c_lcd_init(I2C_lcd_addr);
    i2c_lcd_clear();
    
    // Mensaje de bienvenida
    i2c_lcd_write_text(0, 0, "   GADD CLIMA v4.0  ");
    i2c_lcd_write_text(1, 0, "   Iniciando...     ");
    vTaskDelay(pdMS_TO_TICKS(2000));
    i2c_lcd_clear();

    while(1) {
        // FILA 0: ESTADO
        if (sys.freeze_mode) {
            i2c_lcd_write_text(0, 0, "ALERTA: CONGELADO!  ");
        } else if (sys.protection_wait) {
            i2c_lcd_write_text(0, 0, "ESPERA SEGURIDAD... ");
        } else if (sys.cfg.system_on) {
            if (sys.comp_active) i2c_lcd_write_text(0, 0, "FRIO: ON  | FAN: ");
            else                 i2c_lcd_write_text(0, 0, "FRIO: OFF | FAN: ");
            
            // Dibujar velocidad Fan
            char fan_char = '1' + (sys.cfg.fan_speed - 1);
            char fan_str[2] = {fan_char, 0};
            i2c_lcd_write_text(0, 17, fan_str);
        } else {
            i2c_lcd_write_text(0, 0, "SISTEMA APAGADO     ");
        }

        // FILA 1: TEMPERATURAS
        // A: Ambiente, C: Cañería
        snprintf(buffer, 21, "Amb:%.1f  Coil:%.1f", sys.t_amb, sys.t_coil);
        i2c_lcd_write_text(1, 0, buffer);

        // FILA 2: ELECTRICO (Si es 20x4)
        // snprintf(buffer, 21, "%.1fV  %.1fA  %.0fW", sys.volt, sys.amp, sys.watt);
        // i2c_lcd_write_text(2, 0, buffer);
        
        // FILA 3: SALIDA
        // snprintf(buffer, 21, "Salida Aire: %.1f", sys.t_out);
        // i2c_lcd_write_text(3, 0, buffer);
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// --- TAREA CLIMA ---
void task_climate(void *pv) {
    ds18b20_init_bus(PIN_ONEWIRE);
    esp_task_wdt_add(NULL);

    while(1) {
        // 1. Leer Sensores
        if (ds18b20_convert_all(PIN_ONEWIRE) == ESP_OK) {
            ds18b20_read_one(PIN_ONEWIRE, ID_AMB, &sys.t_amb);
            ds18b20_read_one(PIN_ONEWIRE, ID_OUT, &sys.t_out);
            ds18b20_read_one(PIN_ONEWIRE, ID_COIL, &sys.t_coil);
            
            ESP_LOGI(TAG, "T.Amb: %.1f | T.Coil: %.1f", sys.t_amb, sys.t_coil);
        } else {
            ESP_LOGE(TAG, "Error Sensores");
        }

        // 2. Control Anti-Hielo
        if (!sys.freeze_mode && sys.t_coil < FREEZE_LIMIT_C) {
            sys.freeze_mode = true;
            sys.comp_active = false; 
            set_relays(false, 3);
            last_comp_stop_time = esp_timer_get_time();
        }
        else if (sys.freeze_mode) {
            if (sys.t_coil > FREEZE_RESET_C) sys.freeze_mode = false;
        }
        // 3. Control Normal
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
            sys.comp_active = false;
            set_relays(false, 0);
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// --- TAREA METER ---
void task_meter(void *pv) {
    while(1) {
        ac_meter_read_rms(&sys.volt, &sys.amp, &sys.watt);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void) {
    gpio_install_isr_service(0);
    ac_meter_init();
    
    gpio_reset_pin(PIN_COMPRESOR); gpio_set_direction(PIN_COMPRESOR, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_FAN_L); gpio_set_direction(PIN_FAN_L, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_FAN_M); gpio_set_direction(PIN_FAN_M, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_FAN_H); gpio_set_direction(PIN_FAN_H, GPIO_MODE_OUTPUT);
    set_relays(false, 0);

    sys.cfg.setpoint = 24.0;
    sys.cfg.fan_speed = 1;
    sys.cfg.system_on = true;

    esp_task_wdt_config_t wdt_conf = { .timeout_ms = WDT_TIMEOUT_MS, .trigger_panic = true };
    if (esp_task_wdt_status(NULL) != ESP_OK) esp_task_wdt_init(&wdt_conf);

    xTaskCreate(task_climate, "Climate", 4096, NULL, 5, NULL);
    xTaskCreate(task_meter, "Meter", 4096, NULL, 3, NULL);
    xTaskCreate(task_ui, "UI", 4096, NULL, 2, NULL);
}