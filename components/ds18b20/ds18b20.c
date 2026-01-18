#include "ds18b20.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h" // Para ets_delay_us

static const char *TAG = "DS18B20";

// --- VARIABLES ESTÁTICAS (Ahora arriba para evitar error 'undeclared') ---
// Candado para secciones críticas (evita que una interrupción rompa el timing)
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Estructura real del sensor (privada)
struct ds18b20_context_t {
    gpio_num_t pin;
};

// --- FUNCIONES DE BAJO NIVEL (OneWire Bit-Banging) ---

// Espera en microsegundos precisa
static inline void delay_us(uint32_t us) {
    esp_rom_delay_us(us);
}

// Reset del bus: Devuelve true si detecta presencia del sensor
static bool onewire_reset(gpio_num_t pin) {
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
    delay_us(480); // Reset pulse
    gpio_set_direction(pin, GPIO_MODE_INPUT); // Release line
    delay_us(70);
    
    int presence = gpio_get_level(pin); // Debe ser 0 si hay sensor
    delay_us(410);
    
    return (presence == 0);
}

// Escribir un bit (0 o 1)
static void onewire_write_bit(gpio_num_t pin, int bit) {
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
    
    if (bit) {
        delay_us(6); // Breve pulso bajo para escribir 1
        gpio_set_direction(pin, GPIO_MODE_INPUT); // Soltar
        delay_us(64);
    } else {
        delay_us(60); // Pulso largo bajo para escribir 0
        gpio_set_direction(pin, GPIO_MODE_INPUT); // Soltar
        delay_us(10);
    }
}

// Leer un bit
static int onewire_read_bit(gpio_num_t pin) {
    int bit = 0;
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
    delay_us(6);
    gpio_set_direction(pin, GPIO_MODE_INPUT); // Soltar
    delay_us(9);
    
    if (gpio_get_level(pin)) bit = 1;
    
    delay_us(55);
    return bit;
}

// Escribir Byte
static void onewire_write_byte(gpio_num_t pin, uint8_t data) {
    for (int i = 0; i < 8; i++) {
        // Bloquear interrupciones brevemente para asegurar timing
        portENTER_CRITICAL_SAFE(&mux); 
        onewire_write_bit(pin, data & 0x01);
        portEXIT_CRITICAL_SAFE(&mux);
        data >>= 1;
    }
}

// Leer Byte
static uint8_t onewire_read_byte(gpio_num_t pin) {
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        portENTER_CRITICAL_SAFE(&mux);
        if (onewire_read_bit(pin)) data |= (1 << i);
        portEXIT_CRITICAL_SAFE(&mux);
    }
    return data;
}

// --- FUNCIONES PÚBLICAS (API) ---

esp_err_t ds18b20_new_device(gpio_num_t pin, ds18b20_device_handle_t *handle) {
    // 1. Configurar GPIO
    gpio_reset_pin(pin);
    // IMPORTANTE: Resistencia externa de 4.7k recomendada.
    
    // Asignar memoria para el "objeto" sensor
    struct ds18b20_context_t *ctx = (struct ds18b20_context_t *)malloc(sizeof(struct ds18b20_context_t));
    if (!ctx) return ESP_ERR_NO_MEM;
    
    ctx->pin = pin;
    *handle = ctx;
    
    ESP_LOGI(TAG, "Sensor inicializado en GPIO %d", pin);
    return ESP_OK;
}

esp_err_t ds18b20_read_temp(ds18b20_device_handle_t handle, float *temp) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    gpio_num_t pin = handle->pin;

    // 1. Iniciar Conversión
    if (!onewire_reset(pin)) {
        ESP_LOGE(TAG, "Sensor no detectado en GPIO %d", pin);
        return ESP_ERR_NOT_FOUND;
    }
    onewire_write_byte(pin, 0xCC); // Skip ROM
    onewire_write_byte(pin, 0x44); // Convert T

    // 2. Esperar conversión (750ms max para 12 bits)
    vTaskDelay(pdMS_TO_TICKS(750));

    // 3. Leer Scratchpad
    if (!onewire_reset(pin)) return ESP_ERR_NOT_FOUND;
    onewire_write_byte(pin, 0xCC); // Skip ROM
    onewire_write_byte(pin, 0xBE); // Read Scratchpad

    uint8_t lsb = onewire_read_byte(pin);
    uint8_t msb = onewire_read_byte(pin);
    
    // 4. Calcular Temperatura
    int16_t raw = (msb << 8) | lsb;
    float t = (float)raw / 16.0;
    
    // Filtrado de errores
    if (t < -55.0 || t > 125.0) return ESP_FAIL;

    *temp = t;
    return ESP_OK;
}

void ds18b20_delete_device(ds18b20_device_handle_t handle) {
    if (handle) free(handle);
}
