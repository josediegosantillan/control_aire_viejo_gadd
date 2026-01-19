#include "ds18b20.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Spinlock para proteger la comunicación de interrupciones
static portMUX_TYPE ds18b20_spinlock = portMUX_INITIALIZER_UNLOCKED;

// Tiempos OneWire estándar
#define DELAY_RESET 480
#define DELAY_WRITE_1 6
#define DELAY_WRITE_0 60
#define DELAY_READ_SAMPLE 9

static void _delay_us(uint32_t us) {
    esp_rom_delay_us(us);
}

void ds18b20_init_bus(gpio_num_t pin) {
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ENABLE);
}

// Reset del bus: Devuelve true si alguien responde (Presence Pulse)
static bool _onewire_reset(gpio_num_t pin) {
    gpio_set_level(pin, 0);
    _delay_us(DELAY_RESET);
    gpio_set_level(pin, 1);
    _delay_us(70);
    int presence = gpio_get_level(pin);
    _delay_us(410);
    return (presence == 0);
}

static void _onewire_write_bit(gpio_num_t pin, int bit) {
    gpio_set_level(pin, 0);
    if (bit) {
        _delay_us(DELAY_WRITE_1);
        gpio_set_level(pin, 1);
        _delay_us(64);
    } else {
        _delay_us(DELAY_WRITE_0);
        gpio_set_level(pin, 1);
        _delay_us(10);
    }
}

static int _onewire_read_bit(gpio_num_t pin) {
    int bit = 0;
    gpio_set_level(pin, 0);
    _delay_us(6);
    gpio_set_level(pin, 1);
    _delay_us(DELAY_READ_SAMPLE);
    bit = gpio_get_level(pin);
    _delay_us(55);
    return bit;
}

static void _onewire_write_byte(gpio_num_t pin, uint8_t data) {
    for (int i = 0; i < 8; i++) {
        _onewire_write_bit(pin, (data >> i) & 1);
    }
}

static uint8_t _onewire_read_byte(gpio_num_t pin) {
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        data |= (_onewire_read_bit(pin) << i);
    }
    return data;
}

// Manda a convertir a TODOS (aquí sí usamos Skip ROM porque no leemos nada de vuelta)
esp_err_t ds18b20_convert_all(gpio_num_t pin) {
    portENTER_CRITICAL_SAFE(&ds18b20_spinlock);
    if (!_onewire_reset(pin)) {
        portEXIT_CRITICAL_SAFE(&ds18b20_spinlock);
        return ESP_FAIL; // Nadie conectado
    }
    _onewire_write_byte(pin, 0xCC); // Skip ROM
    _onewire_write_byte(pin, 0x44); // Convert T
    portEXIT_CRITICAL_SAFE(&ds18b20_spinlock);
    
    // IMPORTANTE: El usuario debe esperar 750ms después de esto
    return ESP_OK; 
}

// Lee UNO solo usando su dirección
esp_err_t ds18b20_read_one(gpio_num_t pin, ds18b20_addr_t address, float *temp) {
    portENTER_CRITICAL_SAFE(&ds18b20_spinlock);
    
    if (!_onewire_reset(pin)) {
        portEXIT_CRITICAL_SAFE(&ds18b20_spinlock);
        return ESP_ERR_TIMEOUT;
    }

    // AQUI ESTÁ LA CLAVE: Usamos Match ROM (0x55) en vez de Skip ROM
    _onewire_write_byte(pin, 0x55); 
    
    // Enviamos los 8 bytes de la dirección del sensor
    for (int i = 0; i < 8; i++) {
        _onewire_write_byte(pin, address.addr[i]);
    }

    _onewire_write_byte(pin, 0xBE); // Read Scratchpad
    
    uint8_t low = _onewire_read_byte(pin);
    uint8_t high = _onewire_read_byte(pin);
    
    // Leemos el resto (opcional, pero buena práctica para limpiar el bus)
    for(int i=0; i<7; i++) _onewire_read_byte(pin);

    portEXIT_CRITICAL_SAFE(&ds18b20_spinlock);

    int16_t raw = (high << 8) | low;
    
    // Si leemos 0xFFFF (-1), es error de desconexión
    if (raw == -1) return ESP_ERR_TIMEOUT;

    float t = (float)raw / 16.0f;
    
    // Filtro básico de valores imposibles
    if (t < -55.0 || t > 125.0) return ESP_FAIL;

    *temp = t;
    return ESP_OK;
}