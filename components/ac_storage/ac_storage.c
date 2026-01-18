#include "ac_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

void storage_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void storage_save(sys_config_t *cfg) {
    nvs_handle_t h;
    if (nvs_open("ac_storage", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "config", cfg, sizeof(sys_config_t));
        nvs_commit(h);
        nvs_close(h);
    }
}

bool storage_load(sys_config_t *cfg) {
    nvs_handle_t h;
    if (nvs_open("ac_storage", NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = sizeof(sys_config_t);
    esp_err_t err = nvs_get_blob(h, "config", cfg, &len);
    nvs_close(h);
    return (err == ESP_OK);
}
