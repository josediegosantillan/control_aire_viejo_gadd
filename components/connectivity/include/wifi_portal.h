#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicia la lógica de conexión WiFi.
 * - Si hay credenciales guardadas en NVS: Intenta conectar a la red.
 * - Si falla o no hay datos: Levanta el Portal Cautivo (AP: HELADERA_SETUP).
 */
void wifi_portal_init(void);

#ifdef __cplusplus
}
#endif