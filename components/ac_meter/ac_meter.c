#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h" 
#include "ac_meter.h"
#include "esp_adc/adc_oneshot.h"

static const char *TAG = "AC_METER";

// Configuración ADC
#define ADC_ATTEN ADC_ATTEN_DB_12 
#define ADC_WIDTH ADC_BITWIDTH_DEFAULT

// CALIBRACIÓN (Ajustar con Multímetro)
// Si mide de más, bajar este número. Si mide de menos, subirlo.
#define CAL_V 0.773
#define CAL_I 0.024

static adc_oneshot_unit_handle_t adc1_handle;
static adc_channel_t chan_v;
static adc_channel_t chan_i;

void ac_meter_init(int pin_v, int pin_i) {
    adc_unit_t unit;
    adc_channel_t chan;

    // 1. Iniciar Unidad ADC1
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // 2. Configurar Voltaje (GPIO 34)
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(pin_v, &unit, &chan));
    chan_v = chan;
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            
    // 3. Configurar Corriente (GPIO 35)
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(pin_i, &unit, &chan));
    chan_i = chan;

    // 4. Configurar Canales
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_WIDTH,
        .atten = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, chan_v, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, chan_i, &config));

    ESP_LOGI(TAG, "ADC Iniciado. V:Ch%d I:Ch%d", chan_v, chan_i);
}

void ac_meter_read_rms(float *v, float *i, float *w) {
    int raw_v = 0;
    int raw_i = 0;
    double sum_sq_v = 0.0;
    double sum_v = 0.0;
    double sum_sq_i = 0.0;
    double sum_i = 0.0;
    int samples = 0;

    int64_t start_time = esp_timer_get_time();
    
    // Muestreo de 200ms (10 ciclos de red)
    while ((esp_timer_get_time() - start_time) < 200000) {
        if (adc_oneshot_read(adc1_handle, chan_v, &raw_v) == ESP_OK &&
            adc_oneshot_read(adc1_handle, chan_i, &raw_i) == ESP_OK) {
            sum_v += raw_v;
            sum_sq_v += (raw_v * raw_v);
            
            sum_i += raw_i;
            sum_sq_i += (raw_i * raw_i);
            samples++;
        }
    }

    if (samples > 0) {
        // Cálculo RMS eliminando Offset DC
        double mean_v = sum_v / samples;
        double variance_v = (sum_sq_v / samples) - (mean_v * mean_v);
        if (variance_v < 0) variance_v = 0;
        
        float rms_adc = sqrt(variance_v);
        
        // Filtro de ruido base
        if (rms_adc < 8.0) rms_adc = 0.0;

        *v = rms_adc * CAL_V;

        // Cálculo RMS Corriente
        double mean_i = sum_i / samples;
        double variance_i = (sum_sq_i / samples) - (mean_i * mean_i);
        if (variance_i < 0) variance_i = 0;

        float rms_i_adc = sqrt(variance_i);
        // Filtro de ruido más agresivo para corriente (umbral mayor para eliminar ruido sin carga)
        if (rms_i_adc < 35.0) rms_i_adc = 0.0;

        *i = rms_i_adc * CAL_I;
    } else {
        *v = 0.0;
        *i = 0.0;
    }

    *w = (*v) * (*i); 
}
