#pragma once
#include <stdbool.h>

typedef struct {
    float setpoint;
    int fan_speed;
    bool system_on;
} sys_config_t;

void storage_init(void);
void storage_save(sys_config_t *cfg);
bool storage_load(sys_config_t *cfg);
