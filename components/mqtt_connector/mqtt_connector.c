/**
 * @file mqtt_connector.c
 * @brief Cliente MQTT Seguro sobre WSS - Versión Industrial v4 (Race-Condition Free)
 * @author Arq. Gadd / Diego
 */

#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "esp_log.h"
#include "esp_err.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h" // Necesario para SSL/WSS automático
#include "mqtt_connector.h"

static const char *TAG = "MQTT_WSS";

// --- VARIABLES GLOBALES (Thread-Safe) ---
// Usamos _Atomic para evitar que dos núcleos del ESP32 lean basura si se reinicia el cliente
static _Atomic esp_mqtt_client_handle_t g_client = ATOMIC_VAR_INIT(NULL);
static atomic_bool is_connected = ATOMIC_VAR_INIT(false);
static mqtt_rx_cb_t s_rx_cb = NULL;

// --- MANEJADOR DE EVENTOS ---
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    
    // Validamos que el evento sea del cliente actual (evitar eventos "zombies" de conexiones viejas)
    esp_mqtt_client_handle_t current_client = atomic_load(&g_client);
    if (event->client != current_client) {
        return; 
    }

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "✅ MQTT Conectado (WSS)");
        atomic_store(&is_connected, true);
        
        // Al conectar, nos suscribimos a comandos y avisamos que estamos ONLINE
        esp_mqtt_client_publish(event->client, MQTT_TOPIC_STATUS, "ONLINE", 0, 1, 1);
        esp_mqtt_client_subscribe(event->client, MQTT_TOPIC_CONFIG, 1); // Descomentar si recibes configuración
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "⚠️ MQTT Desconectado");
        atomic_store(&is_connected, false);
        break;

    case MQTT_EVENT_DATA:
        // Si llegó un dato y tenemos callback configurado, lo pasamos
        if (s_rx_cb && event->topic && event->data) {
            s_rx_cb(event->topic, event->topic_len, event->data, event->data_len);
        }
        break;

    case MQTT_EVENT_ERROR:
        if (event->error_handle) {
            ESP_LOGE(TAG, "MQTT Error tipo: %d", event->error_handle->error_type);
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "TLS Err: %d", event->error_handle->esp_tls_last_esp_err);
            }
        }
        break;

    default:
        break;
    }
}

// --- FUNCIONES PÚBLICAS ---

void mqtt_app_set_rx_callback(mqtt_rx_cb_t cb) {
    s_rx_cb = cb;
}

void mqtt_app_start(void) {
    // 1. Limpieza preventiva (si ya había un cliente, lo matamos antes de crear otro)
    esp_mqtt_client_handle_t old_client = atomic_exchange(&g_client, NULL);
    if (old_client != NULL) {
        ESP_LOGW(TAG, "Reiniciando cliente MQTT...");
        esp_mqtt_client_stop(old_client);
        esp_mqtt_client_destroy(old_client);
    }

    // 2. Configuración del Broker (WSS)
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = "wss://thebaltoteam.com.ar/mqtt", // Tu servidor real
                .port = 443,
            },
            .verification = {
                .crt_bundle_attach = esp_crt_bundle_attach, // Magia: Usa certificados integrados en ESP-IDF
            },
        },
        .credentials = {
            .username = "esp32_heladera",
            .authentication = {
                .password = "291289", 
            },
        },
        .session = {
            .keepalive = 30,
            .last_will = {
                .topic = MQTT_TOPIC_STATUS,
                .msg = "OFFLINE",
                .qos = 1,
                .retain = 1,
            },
        },
    };

    // 3. Inicialización
    esp_mqtt_client_handle_t new_client = esp_mqtt_client_init(&mqtt_cfg);
    if (new_client == NULL) {
        ESP_LOGE(TAG, "Error crítico: No se pudo asignar memoria para MQTT");
        return;
    }
    
    // 4. Registro de Eventos
    if (esp_mqtt_client_register_event(new_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Error registrando eventos");
        esp_mqtt_client_destroy(new_client);
        return;
    }
    
    // 5. Guardar cliente globalmente y Arrancar
    atomic_store(&g_client, new_client);
    
    if (esp_mqtt_client_start(new_client) != ESP_OK) {
        ESP_LOGE(TAG, "Error arrancando cliente");
        atomic_store(&g_client, NULL);
        esp_mqtt_client_destroy(new_client);
        return;
    }

    ESP_LOGI(TAG, "Cliente MQTT Iniciado.");
}

bool mqtt_app_publish(const char *topic, const char *data) {
    if (topic == NULL || data == NULL) return false;

    esp_mqtt_client_handle_t client = atomic_load(&g_client);
    bool connected = atomic_load(&is_connected);

    if (client != NULL && connected) {
        int msg_id = esp_mqtt_client_publish(client, topic, data, 0, 0, 0);
        return (msg_id >= 0);
    }
    return false;
}

bool mqtt_app_is_connected(void) {
    return atomic_load(&is_connected);
}
