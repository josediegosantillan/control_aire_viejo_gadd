#pragma once
#include "driver/gpio.h"

// --- PINES DE ACTUADORES (Salidas) ---
#define PIN_COMPRESOR   GPIO_NUM_25
#define PIN_FAN_L       GPIO_NUM_26
#define PIN_FAN_M       GPIO_NUM_27
#define PIN_FAN_H       GPIO_NUM_14

// --- PINES DE SENSORES (Entradas) ---
#define PIN_DS18B20_AMB  GPIO_NUM_4   // Sensor Ambiente (Original)
#define PIN_DS18B20_COIL GPIO_NUM_18  // NUEVO: Sensor Cañería (Protección Hielo)
#define PIN_PIR          GPIO_NUM_13
#define PIN_ZMPT         GPIO_NUM_34  // ADC1 CH6
#define PIN_SCT          GPIO_NUM_35  // ADC1 CH7

// --- I2C ---
#define PIN_I2C_SDA      GPIO_NUM_21
#define PIN_I2C_SCL      GPIO_NUM_22

// --- SEGURIDAD ---
#define WDT_TIMEOUT_MS      5000  // 5 Segundos para Watchdog
#define SAFETY_DELAY_MIN    0     // 3 Minutos de espera compresor

// --- PROTECCIÓN ANTI-CONGELAMIENTO ---
#define FREEZE_LIMIT_C      0.0   // Cortar si baja de 0°C
#define FREEZE_RESET_C      10.0  // Habilitar cuando suba de 10°C