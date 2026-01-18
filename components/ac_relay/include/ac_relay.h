#pragma once
#include <stdbool.h>
#include "ac_config.h"

void relay_init(void);
void relay_set_fan(int speed);
void relay_set_compressor(bool state);
