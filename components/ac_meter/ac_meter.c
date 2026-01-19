#include <math.h>
#include "esp_log.h"
#include "ac_meter.h"
// #include "esp_adc/adc_oneshot.h" // Descomentar cuando implementes lectura real ADC

static const char *TAG = "AC_METER";

// Factores de calibración (Ajustar con multímetro patrón)
#define CAL_V 0.75 
#define CAL_I 0.04

void ac_meter_init(void) {
    // Aquí iría la configuración del ADC OneShot (adc_oneshot_new_unit...)
    // Por ahora, solo logueamos que inició correctamente.
    ESP_LOGI(TAG, "Hardware de medición (ADC) Inicializado");
}

void ac_meter_read_rms(float *v, float *i, float *w) {
    // --- SIMULACIÓN PARA PRUEBAS SIN 220V ---
    // En el futuro, aquí ponés el bucle for() leyendo el ADC real.
    
    *v = 221.5; // Simula 220V estables
    *i = 5.2;   // Simula consumo normal de un aire
    
    // Potencia Activa estimada (PF 0.85 para motor de compresor)
    *w = (*v) * (*i) * 0.85; 
}
