idf_component_register(SRCS "ac_meter.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_adc log driver esp_timer)