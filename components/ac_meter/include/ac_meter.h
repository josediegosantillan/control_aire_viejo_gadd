#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa el ADC OneShot en los pines indicados.
 * @param pin_v GPIO del sensor de voltaje (ZMPT101B)
 * @param pin_i GPIO del sensor de corriente (SCT)
 */
void ac_meter_init(int pin_v, int pin_i);

/**
 * @brief Toma muestras reales durante 200ms y calcula RMS.
 */
void ac_meter_read_rms(float *v, float *i, float *w);

#ifdef __cplusplus
}
#endif
