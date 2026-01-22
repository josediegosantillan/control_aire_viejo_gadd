#pragma once
#include <stdbool.h>

// Modos de operación
#define MODE_OFF  0
#define MODE_COOL 1
#define MODE_FAN  2

typedef struct {
    float setpoint;
    int fan_speed;
    bool system_on;
    int mode;  // 0=OFF, 1=FRIO, 2=VENTILACION
} sys_config_t;

void storage_init(void);
void storage_save(sys_config_t *cfg);
bool storage_load(sys_config_t *cfg);
