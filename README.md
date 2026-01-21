# 🌡️ GADD CLIMA v7.1 - Control de Aire Acondicionado

Sistema embebido de control inteligente para aire acondicionado basado en **ESP32** con ESP-IDF v5.5.1.

---

## 📋 Descripción General

Sistema de control de climatización con las siguientes características:

- ✅ Control de compresor con histéresis (±1°C del setpoint)
- ✅ Control de ventilador de 3 velocidades
- ✅ Monitoreo de tensión, corriente y potencia eléctrica
- ✅ 3 sensores de temperatura DS18B20 (ambiente, cañería, exterior)
- ✅ Protección anti-congelamiento automática
- ✅ Delay de seguridad para el compresor
- ✅ Conectividad WiFi con portal cautivo para configuración
- ✅ Control remoto vía MQTT (integración con Node-RED)
- ✅ Persistencia de configuración en Flash (NVS)
- ✅ Display LCD 20x4 para monitoreo local
- ✅ Watchdog Timer para recuperación ante fallos

---

## ⚡ Pinout del ESP32

### Salidas (Actuadores)
| Pin | GPIO | Función |
|-----|------|---------|
| PIN_COMPRESOR | GPIO_NUM_25 | Relé compresor (activo bajo) |
| PIN_FAN_L | GPIO_NUM_26 | Ventilador velocidad baja |
| PIN_FAN_M | GPIO_NUM_27 | Ventilador velocidad media |
| PIN_FAN_H | GPIO_NUM_14 | Ventilador velocidad alta |

### Entradas (Sensores)
| Pin | GPIO | Función |
|-----|------|---------|
| PIN_ONEWIRE | GPIO_NUM_4 | Bus OneWire (3x DS18B20) |
| PIN_ZMPT | GPIO_NUM_34 | Sensor de voltaje ZMPT101B |
| PIN_SCT | GPIO_NUM_35 | Sensor de corriente SCT-013 |

### I2C (LCD 20x4)
| Pin | GPIO | Función |
|-----|------|---------|
| PIN_I2C_SDA | GPIO_NUM_21 | Datos I2C |
| PIN_I2C_SCL | GPIO_NUM_22 | Reloj I2C |

---

## 🌡️ Sensores de Temperatura DS18B20

Se utilizan 3 sensores con direcciones ROM hardcodeadas:

| Sensor | ID ROM | Función |
|--------|--------|---------|
| ID_AMB | `28:B5:6C:54:00:00:00:14` | Temperatura ambiente |
| ID_COIL | `28:F4:D6:57:04:E1:3C:1E` | Temperatura cañería (protección hielo) |
| ID_OUT | `28:B9:31:55:00:00:00:9F` | Temperatura exterior |

---

## 🔒 Parámetros de Seguridad

| Parámetro | Valor | Descripción |
|-----------|-------|-------------|
| `WDT_TIMEOUT_MS` | 5000 ms | Tiempo de Watchdog |
| `SAFETY_DELAY_MIN` | 0 min | Delay mínimo entre arranques del compresor |
| `FREEZE_LIMIT_C` | 0.0°C | Temperatura de corte por congelamiento |
| `FREEZE_RESET_C` | 10.0°C | Temperatura para reactivar tras congelamiento |

---

## 📡 Comunicación MQTT

### Tópicos
| Tópico | Dirección | Descripción |
|--------|-----------|-------------|
| `aire_lennox/telemetria` | ESP32 → Broker | Datos de sensores en tiempo real |
| `aire_lennox/config` | Broker → ESP32 | Comandos de control desde Node-RED |
| `aire_lennox/estado` | ESP32 → Broker | Estado del sistema |

### Formato JSON de Telemetría (Salida)
```json
{
  "v": 220.5,      // Tensión (V)
  "a": 3.25,       // Intensidad (A)
  "amb": 24.50,    // Temp. Ambiente (°C)
  "out": 32.00,    // Temp. Exterior (°C)
  "coil": 8.50,    // Temp. Cañería (°C)
  "on": 1,         // Compresor activo (0/1)
  "fan": 2         // Velocidad ventilador (0-3)
}
```

### Formato JSON de Configuración (Entrada desde Node-RED)
```json
{
  "on": true,      // Encender/Apagar sistema
  "fan": 2,        // Velocidad ventilador (0=auto, 1=low, 2=med, 3=high)
  "sp": 22.0       // Setpoint temperatura (16.0 - 30.0°C)
}
```

---

## 🧠 Funciones Principales

### `main.c`

#### `app_main(void)`
Punto de entrada del sistema. Inicializa todos los subsistemas:
1. NVS Flash para persistencia
2. Mutex para protección de datos compartidos
3. GPIOs de actuadores
4. Carga de configuración guardada
5. WiFi y MQTT
6. Creación de tareas FreeRTOS

#### `task_climate(void *pv)`
Tarea principal de control climático (prioridad 5):
- Lee sensores DS18B20 cada segundo
- Implementa lógica de termostato con histéresis ±1°C
- Detecta condición de congelamiento y activa protección
- Publica telemetría vía MQTT
- Registra estado completo en monitor serial

#### `task_meter(void *pv)`
Tarea de medición eléctrica (prioridad 3):
- Muestrea tensión y corriente cada 200ms
- Calcula valores RMS
- Actualiza variables globales de forma thread-safe

#### `task_ui(void *pv)`
Tarea de interfaz de usuario (prioridad 2):
- Actualiza LCD 20x4 cada segundo
- Muestra estados, alertas y conexiones
- Detecta y reconecta LCD ante fallos de comunicación

#### `mqtt_data_handler(...)`
Callback para comandos MQTT entrantes:
- Parsea JSON de Node-RED
- Actualiza configuración del sistema
- Guarda cambios en Flash
- Aplica cambios físicos inmediatamente

#### `set_relays(bool comp, int fan_speed)`
Controla los relés de salida (lógica invertida - activo bajo).

#### `is_safe_to_start()`
Verifica si han pasado los minutos de seguridad desde la última parada del compresor.

#### `get_wifi_status()`
Retorna el estado de conexión WiFi:
- `0`: Desconectado
- `1`: Conectado a red
- `2`: Modo AP (Portal cautivo)

---

## 📦 Componentes Personalizados

### `ac_meter`
Medición de corriente alterna con sensores ZMPT101B y SCT-013.

| Función | Descripción |
|---------|-------------|
| `ac_meter_init(pin_v, pin_i)` | Inicializa ADC en los pines indicados |
| `ac_meter_read_rms(v, i, w)` | Calcula tensión, corriente y potencia RMS |

### `ac_storage`
Persistencia de configuración en NVS Flash.

| Función | Descripción |
|---------|-------------|
| `storage_init()` | Inicializa el namespace NVS |
| `storage_save(cfg)` | Guarda configuración en Flash |
| `storage_load(cfg)` | Carga configuración de Flash |

**Estructura `sys_config_t`:**
```c
typedef struct {
    float setpoint;    // Temperatura objetivo
    int fan_speed;     // Velocidad ventilador (0-3)
    bool system_on;    // Sistema encendido/apagado
} sys_config_t;
```

### `connectivity` (wifi_portal)
Portal cautivo para configuración WiFi.

| Función | Descripción |
|---------|-------------|
| `wifi_portal_init()` | Inicia conexión WiFi o levanta portal AP |

### `mqtt_connector`
Conexión MQTT sobre WebSocket Secure (WSS).

| Función | Descripción |
|---------|-------------|
| `mqtt_app_start()` | Inicia cliente MQTT |
| `mqtt_app_publish(topic, data)` | Publica mensaje JSON |
| `mqtt_app_is_connected()` | Verifica conexión activa |
| `mqtt_app_set_rx_callback(cb)` | Registra callback para recepción |

### `ds18b20`
Driver para sensores de temperatura DS18B20 (OneWire).

| Función | Descripción |
|---------|-------------|
| `ds18b20_init_bus(pin)` | Inicializa bus OneWire |
| `ds18b20_convert_all(pin)` | Ordena conversión a todos los sensores |
| `ds18b20_read_one(pin, addr, temp)` | Lee temperatura de un sensor específico |

### `i2c_lcd`
Driver para LCD 20x4 con módulo I2C (PCF8574).

| Función | Descripción |
|---------|-------------|
| `i2c_lcd_init(addr)` | Inicializa LCD en dirección I2C |
| `i2c_lcd_clear()` | Limpia pantalla |
| `i2c_lcd_write_text(row, col, text)` | Escribe texto en posición |
| `i2c_lcd_is_alive()` | Verifica comunicación con LCD |

---

## 🔄 Diagrama de Flujo del Sistema

```
┌─────────────────────────────────────────────────────────────┐
│                        app_main()                           │
│  1. Init NVS → 2. Create Mutex → 3. Init GPIO/Sensors      │
│  4. Load Config → 5. Init WiFi → 6. Start MQTT             │
│  7. Create Tasks                                            │
└─────────────────────────────────────────────────────────────┘
                              │
         ┌────────────────────┼────────────────────┐
         ▼                    ▼                    ▼
   ┌──────────┐        ┌──────────┐         ┌──────────┐
   │task_climate│        │task_meter│         │ task_ui │
   │ (Pri: 5) │        │ (Pri: 3) │         │ (Pri: 2) │
   │ 1000ms   │        │  200ms   │         │ 1000ms   │
   └──────────┘        └──────────┘         └──────────┘
         │                    │                    │
         ▼                    ▼                    ▼
   ┌──────────┐        ┌──────────┐         ┌──────────┐
   │ DS18B20  │        │ ZMPT/SCT │         │ LCD 20x4 │
   │ (3 sens) │        │  (ADC)   │         │  (I2C)   │
   └──────────┘        └──────────┘         └──────────┘
         │
         ▼
   ┌──────────────────────────────────────────┐
   │         LÓGICA DE CONTROL                │
   │  • Histéresis ±1°C del setpoint          │
   │  • Protección anti-congelamiento         │
   │  • Delay de seguridad compresor          │
   └──────────────────────────────────────────┘
         │
    ┌────┴────┐
    ▼         ▼
 ┌──────┐  ┌──────┐
 │Relays│  │ MQTT │
 │(GPIO)│  │Publish│
 └──────┘  └──────┘
```

---

## 📁 Estructura del Proyecto

```
control_aire_acondicinado/
│
├── 📄 CMakeLists.txt              # Configuración principal de CMake
├── 📄 partitions.csv              # Tabla de particiones (1.5MB app)
├── 📄 sdkconfig                   # Configuración ESP-IDF
├── 📄 README.md                   # Este archivo
│
├── 📂 main/
│   ├── 📄 CMakeLists.txt          # Build del componente main
│   ├── 📄 main.c                  # Código principal del sistema
│   └── 📂 include/
│       └── 📄 ac_config.h         # Definiciones de pines y constantes
│
├── 📂 components/
│   │
│   ├── 📂 ac_meter/               # Medición de tensión/corriente AC
│   │   ├── 📄 CMakeLists.txt
│   │   ├── 📄 ac_meter.c
│   │   └── 📂 include/
│   │       └── 📄 ac_meter.h
│   │
│   ├── 📂 ac_storage/             # Persistencia en NVS Flash
│   │   ├── 📄 CMakeLists.txt
│   │   ├── 📄 ac_storage.c
│   │   └── 📂 include/
│   │       └── 📄 ac_storage.h
│   │
│   ├── 📂 connectivity/           # WiFi + Portal Cautivo
│   │   ├── 📄 CMakeLists.txt
│   │   ├── 📄 wifi_portal.c
│   │   └── 📂 include/
│   │       └── 📄 wifi_portal.h
│   │
│   ├── 📂 mqtt_connector/         # Cliente MQTT sobre WSS
│   │   ├── 📄 CMakeLists.txt
│   │   ├── 📄 mqtt_connector.c
│   │   └── 📂 include/
│   │       └── 📄 mqtt_connector.h
│   │
│   ├── 📂 ds18b20/                # Driver sensores temperatura
│   │   ├── 📄 CMakeLists.txt
│   │   ├── 📄 ds18b20.c
│   │   ├── 📄 direccione_rom      # Archivo con direcciones ROM
│   │   └── 📂 include/
│   │       └── 📄 ds18b20.h
│   │
│   ├── 📂 i2c_lcd/                # Driver LCD 20x4 I2C
│   │   ├── 📄 CMakeLists.txt
│   │   ├── 📄 i2c_lcd.c
│   │   └── 📂 include/
│   │       └── 📄 i2c_lcd.h
│   │
│   ├── 📂 ac_relay/               # (Reservado para expansión)
│   └── 📂 temp_sensors/           # (Sin CMakeLists - no compilado)
│
└── 📂 build/                      # Directorio de compilación (generado)
    ├── 📄 control_aire_acondicinado.bin
    ├── 📄 bootloader.bin
    └── 📄 partition-table.bin
```

---

## 🛠️ Compilación y Flasheo

### Requisitos
- ESP-IDF v5.5.1
- VS Code + Extensión ESP-IDF

### Comandos

```bash
# Compilar
idf.py build

# Flashear
idf.py -p COMx flash

# Monitor serial
idf.py -p COMx monitor

# Todo en uno
idf.py -p COMx flash monitor
```

---

## 📊 Salida del Monitor Serial

```
═══════════════════════════════════════════════════════════
⚡ Tensión: 220.5V | Intensidad: 3.25A | Potencia: 716W
🌡️  T.Ambiente: 24.5°C | T.Cañería: 8.5°C | T.Exterior: 32.0°C
🎯 Objetivo: 22.0°C | Fan: 2 | Compresor: ON
═══════════════════════════════════════════════════════════
```

---

## 📜 Licencia

Proyecto desarrollado por **GADD CLIMA** - 2026

---

## 🔗 Integración con Node-RED

El sistema se integra con Node-RED para control remoto a través de MQTT. Los comandos se envían al tópico `aire_lennox/config` y la telemetría se recibe en `aire_lennox/telemetria`.
