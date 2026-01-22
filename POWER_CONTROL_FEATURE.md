# 🔘 Control de Potencia - Funcionalidad de Botón y LEDs

## Descripción
Se ha agregado una funcionalidad completa de control on/off con un botón físico y dos LEDs indicadores de estado:
- **Botón**: Presionar para encender/apagar el sistema
- **LED Rojo**: Encendido cuando el sistema está OFF (lógica activa por LOW)
- **LED Verde**: Encendido cuando el sistema está ON (lógica activa por LOW)

## Cambios Realizados

### 1. Nuevo Componente: `power_control`
Se creó un componente modular localizado en `components/power_control/`:

#### Archivos:
- **include/power_control.h** - Header con API pública
- **power_control.c** - Implementación de funciones GPIO
- **CMakeLists.txt** - Configuración de compilación

#### Funciones Principales:
```c
esp_err_t power_control_init(void);              // Inicializa pines
bool power_control_button_pressed(void);         // Lee estado del botón
void power_control_led_red(bool on);             // Controla LED rojo
void power_control_led_green(bool on);           // Controla LED verde
void power_control_update_leds(bool system_on);  // Actualiza LEDs según estado
```

### 2. Configuración de Pines en `ac_config.h`
Se agregaron tres definiciones de pines GPIO:

```c
#define PIN_BUTTON_POWER GPIO_NUM_13  // Botón on/off (pull-up, activo bajo)
#define PIN_LED_RED      GPIO_NUM_32  // LED rojo (encendido por LOW)
#define PIN_LED_GREEN    GPIO_NUM_33  // LED verde (encendido por LOW)
```

**Nota**: Estos pines pueden ajustarse según tu hardware (GPIO15, GPIO32, GPIO33 son recomendados para ESP32).

### 3. Integración en `main.c`

#### a) Inclusión del componente:
```c
#include "power_control.h"   // Control de botón y LEDs
```

#### b) Variables de estado:
```c
static bool button_last_state = true;      // true = no presionado (pull-up)
static uint32_t button_press_debounce = 0; // Contador de debounce
```

#### c) Inicialización en `app_main()`:
```c
ESP_ERROR_CHECK(power_control_init());
```

#### d) Nueva tarea `task_power_button()`:
- Muestrea el botón cada 50ms
- Implementa debounce de 150ms para evitar rebotes
- Detecta transiciones de botón presionado
- Toggle del estado del sistema (ON/OFF)
- Guarda configuración en Flash
- Actualiza LEDs inmediatamente
- Si se apaga el sistema, detiene todos los actuadores

#### e) Actualización de LEDs en `task_climate()`:
```c
power_control_update_leds(sys.cfg.system_on);
```
Los LEDs se actualizan cada segundo con el estado del sistema.

#### f) Creación de tarea en `app_main()`:
```c
xTaskCreate(task_power_button, "PowerBtn", 2048, NULL, 4, NULL);
```

### 4. Actualización de CMakeLists.txt

#### `main/CMakeLists.txt`:
Se agregó `power_control` a la lista de REQUIRES:
```cmake
REQUIRES ac_meter ds18b20 connectivity mqtt_connector i2c_lcd ac_storage power_control nvs_flash esp_event esp_adc esp_timer driver json
```

#### `CMakeLists.txt` (raíz):
Se agregó `power_control` a EXTRA_COMPONENT_DIRS:
```cmake
set(EXTRA_COMPONENT_DIRS
    "components/ac_meter"
    "components/ac_relay"
    "components/ac_storage"
    "components/ds18b20"
    "components/power_control"
)
```

## Comportamiento del Sistema

### Flujo del Botón:
1. Usuario presiona botón → Detectado con debounce (150ms)
2. Sistema obtiene mutex
3. Invierte estado de `sys.cfg.system_on`
4. Guarda configuración en Flash (persiste en reinicios)
5. Actualiza LEDs inmediatamente:
   - ✅ Sistema ON → LED Verde encendido, LED Rojo apagado
   - ❌ Sistema OFF → LED Rojo encendido, LED Verde apagado
6. Si sistema se apaga → Detiene compresor y ventilador
7. Libera mutex y espera 500ms (protección contra rebotes)

### Indicadores LED:
```
Sistema ENCENDIDO (ON):
├── LED Verde: ENCENDIDO (LOW)
└── LED Rojo: APAGADO (HIGH)

Sistema APAGADO (OFF):
├── LED Rojo: ENCENDIDO (LOW)
└── LED Verde: APAGADO (HIGH)
```

## Características de Seguridad

✅ **Debounce de 150ms**: Evita falsos positivos por rebotes del botón
✅ **Mutex protegido**: Acceso thread-safe a variables globales
✅ **Persistencia**: Estado guardado en Flash (resiste reinicios)
✅ **Parada inmediata**: Apaga actuadores si se presiona OFF
✅ **Prioridad de tarea**: 4 (entre climate=5 y meter=3)

## Esquema de Conexión Recomendado

```
GPIO_15 (Button) ───────┬─────── GND
                        │
                   [Push Button]
                        │
                   [Pull-up 10K]
                        │
                       3V3

GPIO_32 (LED Red) ────┬─── 330Ω ───┬─── GND
                      │
                   [LED Red ▼]

GPIO_33 (LED Green) ──┬─── 330Ω ───┬─── GND
                      │
                   [LED Green ▼]
```

## Log de Salida
```
I (XXX) POWER_CONTROL: Botón y LEDs inicializados correctamente
I (XXX) MAIN_SYSTEM: 🔘 Tarea de botón iniciada
I (XXX) MAIN_SYSTEM: 🔘 Botón presionado - Sistema ON ✅
I (XXX) MAIN_SYSTEM: 🔘 Botón presionado - Sistema OFF ❌
```

## Prueba de Integración

1. **Compilación**: ✅ Exitosa sin errores
2. **Tamaño del firmware**: 1,051,233 bytes (33% del espacio de app)
3. **Memoria DRAM**: 19.09% utilizada
4. **Memoria IRAM**: 79.29% utilizada

## Próximos Pasos Opcionales

- Agregar buzzer sonoro al presionar botón
- Implementar LED parpadeante durante espera de seguridad (freeze)
- Agregar comando MQTT para control remoto del botón
- Integrar en el display LCD para mostrar estado de LEDs

---

**Versión**: Sistema v7.1 + Power Control
**Fecha**: 21 de enero de 2026
**Estado**: Compilado y listo para flashear
