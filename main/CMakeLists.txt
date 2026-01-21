idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "include"
                       REQUIRES ac_meter ds18b20 connectivity mqtt_connector i2c_lcd ac_storage nvs_flash esp_event esp_adc esp_timer driver json)