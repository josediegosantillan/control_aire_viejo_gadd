#include "ds18b20.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h" 

static const char *TAG = "DS18B20";

// Candado para proteger el timing (Debe estar declarado ANTES de usarse)
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// --- FUNCIONES DE BAJO NIVEL (Privadas) ---

static inline void delay_us(uint32_t us) {
    esp_rom_delay_us(us);
}

static bool onewire_reset(gpio_num_t pin) {
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
    delay_us(480);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    delay_us(70);
    int presence = gpio_get_level(pin);
    delay_us(410);
    return (presence == 0);
}

static void onewire_write_bit(gpio_num_t pin, int bit) {
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
    if (bit) {
        delay_us(6);
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        delay_us(64);
    } else {
        delay_us(60);
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        delay_us(10);
    }
}

static int onewire_read_bit(gpio_num_t pin) {
    int bit = 0;
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
    delay_us(6);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    delay_us(9);
    if (gpio_get_level(pin)) bit = 1;
    delay_us(55);
    return bit;
}

static void onewire_write_byte(gpio_num_t pin, uint8_t data) {
    for (int i = 0; i < 8; i++) {
        portENTER_CRITICAL_SAFE(&mux);
        onewire_write_bit(pin, data & 0x01);
        portEXIT_CRITICAL_SAFE(&mux);
        data >>= 1;
    }
}

static uint8_t onewire_read_byte(gpio_num_t pin) {
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        portENTER_CRITICAL_SAFE(&mux);
        if (onewire_read_bit(pin)) data |= (1 << i);
        portEXIT_CRITICAL_SAFE(&mux);
    }
    return data;
}

// --- FUNCIONES PÚBLICAS (API Nueva) ---

void ds18b20_init_bus(gpio_num_t pin) {
    gpio_reset_pin(pin);
    // Dejamos el pin flotante/input por defecto
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    ESP_LOGI(TAG, "Bus OneWire inicializado en GPIO %d", pin);
}

esp_err_t ds18b20_convert_all(gpio_num_t pin) {
    // 1. Reset
    if (!onewire_reset(pin)) {
        return ESP_ERR_NOT_FOUND;
    }
    // 2. Comandos Globales
    onewire_write_byte(pin, 0xCC); // SKIP ROM (A todos)
    onewire_write_byte(pin, 0x44); // CONVERT T
    
    // 3. Espera Bloqueante (Simplifica la vida)
    vTaskDelay(pdMS_TO_TICKS(750)); 
    
    return ESP_OK;
}

esp_err_t ds18b20_read_one(gpio_num_t pin, ds18b20_addr_t address, float *temp) {
    // 1. Reset
    if (!onewire_reset(pin)) return ESP_ERR_NOT_FOUND;
    
    // 2. Seleccionar Sensor Específico (MATCH ROM)
    onewire_write_byte(pin, 0x55); 
    for (int i = 0; i < 8; i++) {
        onewire_write_byte(pin, address.addr[i]);
    }
    
    // 3. Pedir Lectura
    onewire_write_byte(pin, 0xBE); // READ SCRATCHPAD

    // 4. Leer 2 bytes (Temperatura LSB y MSB)
    uint8_t lsb = onewire_read_byte(pin);
    uint8_t msb = onewire_read_byte(pin);
    
    // Cortamos la comunicación acá (Reset) para no leer el resto
    onewire_reset(pin); 

    // 5. Calcular
    int16_t raw = (msb << 8) | lsb;
    float t = (float)raw / 16.0;

    if (t < -55.0 || t > 125.0) return ESP_FAIL;
    
    *temp = t;
    return ESP_OK;
}