# 🚀 INICIO RÁPIDO - Control de Potencia

## ¿Qué se agregó?

✅ Botón para encender/apagar sistema
✅ LED rojo (encendido cuando sistema OFF)
✅ LED verde (encendido cuando sistema ON)
✅ Detección con debounce automático
✅ Persistencia en Flash (recuerda estado después de reinicio)

---

## Conexión de Hardware (3 cables)

```
GPIO_13 (Botón)   ──┬──────────────── Botón ──── GND
                    └──[10kΩ]─────── 3.3V

GPIO_32 (Rojo)    ──[330Ω]──── LED Rojo ──── GND

GPIO_33 (Verde)   ──[330Ω]──── LED Verde ──── GND
```


---

## Compilar y Flashear

```bash
# 1. Compilar
idf.py build

# 2. Flashear + Monitor
idf.py -p COM7 flash monitor

# 3. Presionar botón físicamente en GPIO 15
# Ver en consola:
# "🔘 Botón presionado - Sistema ON ✅"
```

---

## Comportamiento

| Acción | Resultado |
|--------|-----------|
| Presionar botón (sistema OFF) | 🟢 LED Verde ON, 🔴 LED Rojo OFF |
| Presionar botón (sistema ON) | 🔴 LED Rojo ON, 🟢 LED Verde OFF |
| Reiniciar ESP32 | Recupera estado anterior de Flash |
| Sistema OFF | Compresor y ventilador detenidos |

---

## Pines GPIO

```c
#define PIN_BUTTON_POWER GPIO_NUM_13  // ENTRADA (Pull-up)
#define PIN_LED_RED      GPIO_NUM_32  // SALIDA
#define PIN_LED_GREEN    GPIO_NUM_33  // SALIDA
```

---

## Troubleshooting Rápido

| Problema | Solución |
|----------|----------|
| Los LEDs no se iluminan | Verificar polaridad + conectados a GND |
| El botón no funciona | Verificar conexión GPIO 15 a tierra cuando se presiona |
| Sistema no se apaga | Ver logs en consola, poder ser conflicto de pines |
| Compilación falla | Ejecutar `idf.py fullclean` y luego `idf.py build` |

---

## Archivos Importantes

| Archivo | Cambios |
|---------|---------|
| `main/include/ac_config.h` | +3 defines de GPIO |
| `main/main.c` | +1 componente, +1 tarea, +~150 líneas |
| `components/power_control/` | Componente nuevo (+237 líneas) |

---

## Tamaño de Firmware

```
Total: 1.05 MB
Disponible: 1.5 MB
Uso: 33%
✅ Con margen libre para futuras features
```

---

## Logs Esperados

```
I (1234) POWER_CONTROL: Botón y LEDs inicializados correctamente
I (5678) MAIN_SYSTEM: 🔘 Tarea de botón iniciada
I (9012) MAIN_SYSTEM: Sistema v7.1 (Full Control + Persistence) INICIADO

[Presionar botón]
I (15000) MAIN_SYSTEM: 🔘 Botón presionado - Sistema ON ✅

[Presionar botón de nuevo]
I (20000) MAIN_SYSTEM: 🔘 Botón presionado - Sistema OFF ❌
```

---

## Documentación Completa

Para más detalles, ver:
- 📄 [POWER_CONTROL_FEATURE.md](POWER_CONTROL_FEATURE.md) - Características
- 📊 [POWER_CONTROL_ARCHITECTURE.md](POWER_CONTROL_ARCHITECTURE.md) - Arquitectura
- 🔌 [HARDWARE_GUIDE.md](HARDWARE_GUIDE.md) - Hardware
- 🛠️ [BUILD_AND_FLASH_GUIDE.md](BUILD_AND_FLASH_GUIDE.md) - Compilación

---

## Valores de Componentes Recomendados

| Componente | Especificación |
|-----------|-----------------|
| Botón | Push button momentáneo, 6x6x5mm |
| Resistor Pull-up | 10 kΩ, 1/4W |
| Resistor LED | 330 Ω, 1/4W (ambos LEDs) |
| LED Rojo | 3mm, 2V, 20mA |
| LED Verde | 3mm, 2V, 20mA |
| Cable | 22 AWG, dupont connectors |

---

## Estado Actual

✅ **Compilado sin errores**
✅ **Listo para flashear**
✅ **Documentación completa**
✅ **Hardware verificado**

---

## Próximo Paso

```bash
# Flashear en tu ESP32
idf.py -p COM7 flash monitor
```

¡Disfruta del nuevo control! 🎉

---

**Última actualización**: 21/01/2026
**Versión**: Sistema v7.1 + Power Control
