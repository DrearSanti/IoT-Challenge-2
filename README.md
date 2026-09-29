# IoT-Challenge-2 · AERIS

Nodo IoT de bajo costo para el monitoreo de disponibilidad de agua y alerta temprana por escasez hídrica en la Sabana Centro (Cundinamarca). Mide nivel de agua y clima, fusiona las señales en un índice de riesgo y alerta en el propio dispositivo y en un **tablero de control servido por el Arduino UNO R4 WiFi** dentro de la WLAN de la zona.

**Documentación completa:** [Wiki del Challenge #2](https://github.com/DrearSanti/IoT-Challenge-2/wiki)

## Contenido del repositorio

| Ruta | Contenido |
|---|---|
| `firmware/MonitorHidrico_LOCAL/MonitorHidrico_LOCAL.ino` | Firmware del Arduino UNO R4 WiFi |
| `firmware/MonitorHidrico_LOCAL/dashboard.html` | Fuente del tablero de control |
| `firmware/MonitorHidrico_LOCAL/dashboard_page.h` | Tablero embebido en la flash |
| `docs/DOCUMENTACION_VERSION_LOCAL.md` | Documentación técnica ampliada |

Para compilar: abrir la carpeta `firmware/MonitorHidrico_LOCAL/` en Arduino IDE 2.x con el core *Arduino UNO R4 Boards* y las librerías Adafruit BME280, Adafruit Unified Sensor y LiquidCrystal_I2C.

## Resumen del Challenge #1 y su relación con este challenge

El [Challenge #1](https://github.com/DrearSanti/IoT-Challenge-1/wiki) construyó un nodo autónomo con Arduino UNO R3 que medía nivel de agua (AJ-SR04M), temperatura, humedad y presión (BME280) y luz (TCS230), y alertaba solo en sitio (LCD, semáforo LED y buzzer), **sin ninguna red de comunicaciones**. Sus aportes principales fueron:

- **Fusión de señales heterogéneas** en un índice de riesgo por variación respecto a una línea base, con pesos justificados en el Índice de Vulnerabilidad Hídrica del IDEAM.
- **Déficit de Presión de Vapor (VPD)** como fundamento físico de la demanda evaporativa.
- **Índice de sequía por periodo** guardado en EEPROM para distinguir un evento puntual de una tendencia.
- **Muestreo no bloqueante**, que mostró que en un microcontrolador de un solo hilo la concurrencia cooperativa es un requisito funcional.

El Challenge #2 conserva esa base de sensado y fusión y agrega lo que el Challenge #1 dejó como trabajo futuro: **conectividad como capa adicional**. Cambia a un **Arduino UNO R4 WiFi**, mide desde **rutinas de interrupción**, sirve un **tablero web embebido** (sin MQTT) accesible solo desde la WLAN de la zona, guarda un **histórico de 160 registros** con hora NTP y permite **apagar la alarma física desde el tablero**. La alerta local sigue funcionando aunque no haya red.

| | Challenge #1 | Challenge #2 |
|---|---|---|
| Microcontrolador | Arduino UNO R3 (ATmega328P) | Arduino UNO R4 WiFi (RA4M1 + radio Wi-Fi) |
| Medición | Temporizadores de software en el `loop()` | Interrupciones (ISR) + tareas cooperativas |
| Notificación | Solo in situ | In situ + tablero web en la WLAN |
| Histórico | 4 periodos promediados | 4 semanas + anillo de 160 registros con hora |
| Red | Ninguna (restricción del reto) | Wi-Fi local, HTTP embebido, sin MQTT |

## Equipo

Esteban Sequeda · Santiago Pulido · Santiago Escobar

Universidad de La Sabana · Internet de las Cosas 2026-2
