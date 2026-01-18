#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa el hardware de medición (ADC OneShot)
 */
void ac_meter_init(void);

/**
 * @brief Lee los valores RMS calculados
 * @param v Puntero para devolver Voltaje (V)
 * @param i Puntero para devolver Corriente (A)
 * @param w Puntero para devolver Potencia (W)
 */
void ac_meter_read_rms(float *v, float *i, float *w);

#ifdef __cplusplus
}
#endif
