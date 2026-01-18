#include <math.h>
#include "esp_adc/adc_oneshot.h"
#include "ac_meter.h"
#include "esp_log.h"

static const char *TAG = "AC_METER";

// Factores de calibración (AJUSTAR CON MULTÍMETRO REAL)
#define CAL_V 0.75 
#define CAL_I 0.04
#define OFFSET_ADC 1850 

void ac_meter_init(void) {
    // Aquí iría la inicialización del ADC OneShot (ver código anterior)
    ESP_LOGI(TAG, "Inicializado AC Meter (Simulado por ahora)");
}

void ac_meter_read_rms(float *v, float *i, float *w) {
    // Lectura simulada para probar lógica sin hardware de 220V conectado
    // En producción, descomentar la lectura real del ADC
    *v = 220.5; 
    *i = 5.4;
    *w = (*v) * (*i) * 0.85; 
}