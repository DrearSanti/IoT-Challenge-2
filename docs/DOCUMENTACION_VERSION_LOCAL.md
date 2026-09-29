# AERIS — versión local (misma red o internet del Arduino)

Este documento describe **solo** la copia que no empuja lecturas a internet. El panel y el histórico viven en el Arduino UNO R4 WiFi, o en un navegador del PC que está en la misma red que el Arduino. La hora (NTP) sí puede salir por el WiFi del Arduino, porque el reloj no está en el chip. Las mediciones no se publican en Vercel.

La copia que sí manda datos a `iot-monitor-pro.vercel.app` es otra carpeta (`arduino/MonitorHidrico_SabanaCentro/`) y no forma parte de este documento.

---

## 1. Qué se construyó

Se dejó una segunda instalación, paralela a la de la nube, con tres piezas:

| Pieza | Dónde está | Qué hace |
| --- | --- | --- |
| Sketch local | `local/arduino/MonitorHidrico_LOCAL/MonitorHidrico_LOCAL.ino` | Sensores, LCD, menú, WiFi, EEPROM y un servidor HTTP en el puerto 80. |
| Página dentro del Arduino | `local/arduino/MonitorHidrico_LOCAL/dashboard.html` y `dashboard_page.h` | El HTML/CSS/JS del panel, compilado dentro del programa. Al abrir la IP del Arduino sale ese panel. |
| Panel React en el PC | carpeta `local/` (`package.json`, `vite.config.ts`, `src/`) | Reutiliza las pantallas de `src/` del proyecto, pero cambia la dirección del Arduino para que el navegador nunca llame a `/api/arduino` ni a Vercel. |

Archivos concretos de la copia local:

- `local/README.md` — pasos cortos para encenderla.
- `local/package.json` — scripts `dev` y `preview`. El nombre del paquete es `aeris-local`.
- `local/index.html` — título «AERIS local». Carga `/src/main.tsx`.
- `local/src/main.tsx` — monta el mismo `App` de `../../src/App` y el mismo `styles.css`.
- `local/src/config/arduino.ts` — dirección, `localStorage` y base de las peticiones. Clave distinta a la de la nube.
- `local/src/vite-env.d.ts` — tipo de `VITE_ARDUINO_URL`.
- `local/vite.config.ts` — puerto 5174, proxy `/arduino` y un plugin que obliga a usar el `arduino.ts` local.
- `local/tsconfig.json` — incluye `local/src` y también `../src` (las pantallas compartidas).
- `local/.env` — no va al git (`.gitignore` ignora `.env`). Ahí se escribe la IP.

El sketch de la nube **no se modificó** para hacer esto. Sigue en `arduino/MonitorHidrico_SabanaCentro/monitor_hidrico_AJ_SR04M_CORREGIDO/`. Ese archivo incluye `WiFiSSLClient.h`, tiene `NUBE_HOST = "iot-monitor-pro.vercel.app"`, `NUBE_PATH = "/api/arduino/ingest"`, una clave `X-AERIS-KEY` y la función `enviarTelemetriaNube()`, que hace un `POST` HTTPS cada 10 segundos. **Eso no existe en el sketch local.** El local no incluye `WiFiSSLClient.h` y no tiene `enviarTelemetriaNube()`.

---

## 2. Hilos: no se usaron

El programa no usa FreeRTOS ni `std::thread`. La radio WiFi y el bus I2C no se pueden llamar desde una interrupción ni desde dos hilos a la vez. En su lugar hay interrupciones de verdad y dos tareas que se turnan:

- El eco del AJ-SR04M entra por interrupción en D3. El ancho del pulso de luz entra por interrupción en D6, y esa interrupción solo está enganchada mientras se mide, porque el TCS230 genera una frecuencia continua.
- BTN1 y BTN2 guardan cada flanco en una cola, con la marca del temporizador. Un `delay` de arranque o un escaneo de redes no se traga la pulsación.
- `FspTimer` corre a 1 kHz. Cada 2 ms avisa a la tarea de red (WiFi, DNS y hasta dos clientes HTTP). Cada 5 ms avisa a la tarea de sensores (botones, distancia, luz, LCD, LEDs e histórico).
- La medición, el histórico y la página se escriben por turnos. Un cliente no congela al otro ni a los botones. La simulación manual del menú también vuelve al ciclo entre muestras, en vez de quedarse en un `delay` de 4 s.

`loop()` solo mira esos avisos y llama a la tarea que toca.

El UNO R4 WiFi tiene dos partes de hardware:

1. El microcontrolador principal (Renesas). Ahí corre este sketch: sensores, LCD, EEPROM y el servidor HTTP.
2. El módulo de radio WiFi, al que se habla con la librería `WiFiS3.h` (`WiFiServer`, `WiFiClient`, `WiFiUDP`). La radio no es un hilo escrito en este archivo. El sketch solo llama funciones de esa librería y espera a que terminen.

El modelo es **cooperativo**: cada vuelta de `loop()` hace un poco de cada cosa, en este orden fijo:

```
gestionarWiFi()
atenderServidorWeb()
leerPulsadores()
medir distancia si ya pasaron 350 ms
imprimir diagnóstico Serial si ya pasó 1 s
leer BME280 y TCS230 si ya pasaron 2 s
cerrar semana de sequía si ya pasaron 7 días
evaluarRiesgo()
actualizarLeds()
gestionarHistoricoAutomatico()
actualizar la pantalla según el menú
```

La tarea de sensores y la de red no se pisan: cada una hace un tramo y regresa. El temporizador y los pines sí interrumpen ese trabajo, pero solo para anotar el flanco o subir el reloj. WiFi, I2C, LCD y EEPROM siguen en la tarea, no dentro de la interrupción.

Puntos que bloquean el ciclo (tiempos reales del código):

| Acción | Cuánto puede frenar `loop()` |
| --- | --- |
| Una muestra de distancia | La interrupción de D3 toma el eco. El temporizador corta a los 28 ms si no llega. Entre las 3 muestras hay 70 ms en los que el servidor sigue atendiendo. |
| Medición filtrada (la normal) | 3 muestras. Entre la 1 y la 2, y entre la 2 y la 3, hay `delay(70)`. En el peor caso, cerca de 3 × 28 ms + 140 ms. |
| Arranque del AJ-SR04M | `delay(800)` en `setup()`, antes de la primera prueba. |
| Lectura de una petición HTTP | Hasta 1500 ms esperando que llegue la cabecera, o hasta 1200 caracteres. |
| Enviar el dashboard | El HTML se manda en trozos de 480 bytes con `delay(1)` entre trozos. |
| Cerrar el cliente | `delay(2)` y `client.stop()`. |
| Conectar al WiFi de casa | Hasta 20 s la primera vez y, si falla, otros 12 s. Dentro, `esperarSTA` hace `delay(400)` hasta tener enlace y `delay(200)` hasta tener IP (máximo 8 s extra). |
| Portal AP | `WiFi.disconnect()`, `delay(200)`, `WiFi.beginAP()`, `delay(300)`. |
| Probar internet | `WiFiClient` a `example.com` puerto 80, más `delay(20)`. |
| Sincronizar hora | Hasta 3 intentos de `WiFi.getTime()` con `delay(350)`, y si fallan un NTP manual de hasta 1800 ms con `delay(20)` en el bucle. |
| Escanear redes (`/scan`) | `WiFi.scanNetworks()` se queda ahí hasta que el módulo termina. |
| Menú «Sim. manual» | `esperarBotonPrincipal()` y varios `delay`. El servidor no atiende mientras se simula. |
| Prueba de LEDs al encender | Tres veces `delay(400)`. |
| Pantalla de sensores al encender | `delay(1800)` y luego `delay(5000)` con el estado de arranque. |

El navegador sí usa temporizadores (`setInterval`), que no son hilos del Arduino. Están descritos más abajo.

---

## 3. Las dos formas de ver los datos

Hay **dos paneles**. Los dos leen al mismo Arduino. Ninguno recibe un push del Arduino: los dos **preguntan** (HTTP GET).

### 3.1 Panel que vive en el Arduino

No hace falta PC ni Node.

1. El sketch arranca el `WiFiServer server(80)`.
2. Al pedir `GET /` (o cualquier ruta que no sea una API), `enviarDashboard()` manda el HTML guardado en `dashboard_page.h` (cadena `AERIS_PAGE`, copiada de `dashboard.html`).
3. Ese HTML, ya en el teléfono o el PC, hace `fetch('/history')` y `fetch('/data')` contra **el mismo host** (la IP del Arduino). Cada 10 segundos repite la carga: `setInterval(load, 10000)`.
4. El botón «Tomar medicion» hace `fetch('/measure')` y después vuelve a cargar.

Como la página y la API salen del mismo origen, el navegador no aplica CORS.

Cuándo se usa:

- **Red del propio Arduino (portal).** Si no hay clave WiFi guardada, o la red falla unas 8 veces, el UNO abre un punto de acceso `AERIS-XXXX` (los dos últimos bytes de la MAC). El teléfono se une a esa red y abre `http://192.168.4.1/`. Esa IP es la que pone el modo AP de `WiFiS3` (`WiFi.localIP()` en el LCD). Ahí no hay router de casa: el internet del Arduino no existe y el NTP no va a sincronizar. El panel en vivo sí responde.
- **Misma red de casa o de la universidad.** Cuando el Arduino ya se unió al WiFi (modo estación, STA), el LCD muestra la IP que le dio el router, por ejemplo `192.168.5.247`. Cualquier aparato en esa red abre `http://esa-IP/`. El PC no necesita el programa React.

### 3.2 Panel React en el PC (`npm run dev` en `local/`)

Sirve cuando se quieren las pantallas grandes (analítica, correlaciones, historial filtrado, umbrales). Ese programa **no guarda lecturas**: cada vez que pinta, le pide al Arduino `/data` y `/history`.

Arranque:

```bash
npm install
cd local
npm run dev
```

Vite escucha en `0.0.0.0:5174` (`strictPort: true`). En el navegador: `http://localhost:5174`.

`local/package.json` no tiene Vite propio. Los scripts llaman a `node ../node_modules/vite/bin/vite.js`, o sea el Vite ya instalado en la raíz del proyecto.

---

## 4. Cómo se conecta el panel React (paso a paso)

### 4.1 De dónde sale la IP

Archivo `local/.env` (en la carpeta `local/`, no en la raíz):

```
ARDUINO_URL=http://192.168.5.247
VITE_ARDUINO_URL=http://192.168.5.247
```

Si el archivo no existe, el código usa `http://192.168.5.247`.

- `ARDUINO_URL` la lee **solo Vite**, en el servidor de desarrollo, para saber a qué IP reenviar el proxy.
- `VITE_ARDUINO_URL` la mete Vite dentro del JavaScript del navegador (`import.meta.env.VITE_ARDUINO_URL`). Sirve de texto por defecto y de IP que se muestra en pantalla. El valor por defecto en `local/src/config/arduino.ts` es ese mismo `http://192.168.5.247`.

El PC y el Arduino tienen que estar en la misma red. El navegador del PC no atraviesa internet para llegar al UNO: el paquete va por el WiFi o el cable de esa LAN hasta la IP que el router le dio al Arduino.

### 4.2 Qué dirección usa cada `fetch`

`local/src/config/arduino.ts`:

- Clave de `localStorage`: `aeris.local.arduinoUrl`. La copia de la nube usa otra clave (`aeris.arduinoUrl` en `src/config/arduino.ts`), así que guardar una URL en un panel no pisa al otro.
- `getArduinoHost()`: si hay URL guardada en Ajustes, esa; si no, `VITE_ARDUINO_URL` o `http://192.168.5.247`. Se usa para mensajes («no fue posible contactar…») y para la ficha del dispositivo.
- `getArduinoBase()`: si en Ajustes se guardó una URL (`http://192.168.x.x`), el navegador llama **esa IP directo**. Si Ajustes está vacío, la base es la ruta relativa `/arduino`.

Timeout de cada petición: `ARDUINO_TIMEOUT_MS = 25000` (25 segundos), con `AbortController`.

### 4.3 El proxy de Vite (cuando Ajustes está vacío)

`local/vite.config.ts` define:

```
navegador  →  http://localhost:5174/arduino/data
                Vite quita el prefijo /arduino
             →  http://192.168.5.247/data
```

Opciones del proxy: `changeOrigin: true`, `timeout: 30000`, y lo mismo en `vite preview` pero en el puerto **4174**.

El navegador habla con `localhost`. Vite, en el PC, abre la conexión TCP al Arduino. Por eso no hace falta CORS cuando se usa el proxy. El Arduino igual manda `Access-Control-Allow-Origin: *`, por si el navegador llama la IP directo.

### 4.4 Si en Ajustes se pega la IP

Página Ajustes → campo «URL del Arduino» → Guardar.

`setStoredArduinoUrl` escribe en `localStorage` y a los 400 ms recarga la página. A partir de ahí `getArduinoBase()` devuelve esa URL y el `fetch` va derecho a `http://192.168.x.x/data`, sin pasar por Vite. Hay que dejar la URL vacía y guardar para volver al proxy.

El texto de ayuda de Ajustes, en esta copia, no es el de Vercel. El plugin `forceLocalArduino` reescribe en caliente la frase de `src/pages/Settings.tsx` y pone: «Vacío usa la IP de local/.env. Esta copia solo habla con el Arduino en la red local.»

### 4.5 Por qué el panel local no puede acabar en Vercel

`local/src/main.tsx` importa `../../src/App` y ese `App` importa `../config/arduino`, que en el disco es `src/config/arduino.ts`. Ese archivo, en la copia de la nube, devuelve `/api/arduino` cuando no está en desarrollo.

Para que `npm run dev` dentro de `local/` no use ese archivo, `vite.config.ts` registra el plugin `forceLocalArduino` (`enforce: 'pre'`). Si quien importa está bajo `/src/` y el módulo pedido es `../config/arduino` o termina en `/config/arduino`, Vite resuelve `local/src/config/arduino.ts`.

Ese archivo local **nunca** devuelve `/api/arduino`. O devuelve la URL guardada o devuelve `/arduino` (el proxy).

El mismo plugin, si el CSS aún no tiene `@source`, inserta `@source "./**/*.{tsx,ts}";` después de `@import "tailwindcss";`, para que Tailwind vea las clases de `src/` aunque la raíz de Vite sea la carpeta `local/`.

---

## 5. Qué pide el navegador y cada cuánto

El servicio compartido es `src/services/arduinoSensorService.ts`. `src/services/sensorService.ts` lo reexporta tal cual. La copia local no tiene otro servicio: cambia solo la base de la URL.

Cada GET lleva un query `_=timestamp` para que el navegador no cachee, y `cache: 'no-store'`.

| Método del servicio | HTTP | Cuándo |
| --- | --- | --- |
| `getLatest()` | `GET /data` | Cada refresco automático, cuando ya hay datos en pantalla. |
| `getHistory({ from, to })` | `GET /history` y después `GET /data` | La primera carga. Pide 30 días hacia atrás (`useSensorData`). Luego filtra por la fecha de cada registro. Si `/history` falla, sigue solo con `/data`. |
| `requestMeasurement()` | `GET /measure` y después `GET /data` | Botón de medir de la barra superior. |

Refresco automático (`src/App.tsx`): estado `auto` inicial **10 segundos**. `setInterval(reload, auto * 1000)`. En la barra se puede dejar en 5 s, 10 s, 30 s, 1 min, 5 min o desactivado (`0`).

Ese refresco de fondo **no** vuelve a bajar todo el histórico: solo `getLatest()` → `/data`, y mete o reemplaza esa lectura en la lista (`upsert` por `id`). La primera carga sí baja `/history`.

Si una petición ya está en curso, la siguiente se ignora (`inFlight`). Así no se apilan varios GET si el Arduino va lento.

El panel embebido pide `/history` y `/data` cada 10 s. Además acumula en el navegador las últimas 120 lecturas en vivo, así la gráfica se mueve aunque el guardado en EEPROM sea cada 6 horas. En Dispositivo están guardar base, el intervalo, el corazón y apagar la matriz. Esas mismas rutas responden en el portal `AERIS-XXXX` y en la red de casa.

---

## 6. Servidor HTTP del Arduino

`WiFiServer server(80)`. Se arranca con `server.begin()` al quedar en STA (`activarServidorSTA`) o al abrir el portal (`iniciarPortalAP`).

`atenderServidorWeb()` en cada `loop()`:

1. Si está en portal, también atiende el DNS cautivo.
2. `server.available()`. Si no hay cliente, sale al instante.
3. Lee bytes hasta ver `\r\n\r\n` (fin de cabeceras), o 1200 caracteres, o 1500 ms.
4. Si el método es `OPTIONS`, responde `204` con CORS y cierra. Sirve para el navegador que llama la IP directo.
5. Según la ruta, responde y hace `delay(2)` + `client.stop()`.
6. Cabeceras normales de API: `HTTP/1.1 200 OK`, `Content-Type`, `Cache-Control: no-cache, no-store, must-revalidate`, `Access-Control-Allow-Origin: *`, `Access-Control-Allow-Methods: GET, OPTIONS`, `Connection: close`.

Solo se aceptan GET (y OPTIONS). No hay POST de telemetría.

### 6.1 Rutas cuando ya está unido al WiFi (modo STA)

| Ruta | Efecto |
| --- | --- |
| `GET /data` | JSON de la telemetría en vivo (`imprimirJsonTelemetria`). No mide de nuevo: devuelve las últimas variables del `loop`. |
| `GET /history` | JSON del anillo de hasta 160 registros en EEPROM. |
| `GET /measure` | Mide distancia filtrada, BME280 y luz, recalcula riesgo y LEDs, intenta guardar un registro con flag manual y responde `{"ok":true/false,"timeOK":...,"count":...}`. |
| `GET /savebase` | Igual que medir, y además copia las lecturas como punto de partida del riesgo. Texto: `LINEA BASE GUARDADA`. |
| `GET /setinterval?m=N` | Cambia el guardado automático. `N` solo puede ser `0`, `15`, `60` o `360` (minutos). `0` lo apaga. Otro valor: `INTERVALO NO VALIDO`. |
| `GET /heart` | Dibuja el corazón en la matriz LED. Texto: `CORAZON ENCENDIDO`. |
| `GET /matrixoff` | Apaga la matriz. Texto: `MATRIZ APAGADA`. |
| `GET /wifi` | Formulario HTML para cambiar SSID y clave. |
| `GET /wifi/portal` | Responde `ABRIENDO PORTAL WIFI`, cierra el cliente y llama `iniciarPortalAP()` (el Arduino deja la red de casa y abre `AERIS-XXXX`). |
| Cualquier otro `GET /` | El dashboard HTML embebido. |

Si llega un cliente pero `WiFi.status()` ya no es `WL_CONNECTED`, se cierra sin cuerpo.

### 6.2 Rutas cuando está en modo portal (AP `AERIS-XXXX`)

| Ruta | Efecto |
| --- | --- |
| `GET /data`, `/history`, `/measure` | Igual que en STA. El panel embebido funciona sin router. |
| `GET /scan` | HTML con las redes que ve `WiFi.scanNetworks()` (SSID y dBm). |
| `GET /save?s=SSID&p=CLAVE` | Decodifica la query (`+` → espacio, `%XX` → carácter). Si el SSID viene vacío, pide volver. Si no, avisa «Guardado. Conectando…», cierra el cliente y llama `aplicarCredencialesDesdePortal`: guarda en EEPROM e intenta STA 20 s. Si no entra, vuelve a abrir el AP. |
| `GET /wifi` | El mismo formulario. |
| Cualquier otro `GET` | El dashboard embebido, no solo la página de configuración. El formulario dice que el panel ya está en el Arduino y deja un enlace a `/`. |

En el portal y en la red de casa responden las mismas rutas de datos: `/data`, `/history`, `/measure`, `/savebase`, `/setinterval`, `/heart`, `/matrixoff` y `/wifi`. El escaneo y `/save` siguen siendo solo del portal. `/wifi/portal` sigue siendo solo cuando ya está unido a una red.

### 6.3 JSON de `GET /data`

Lo arma `imprimirJsonTelemetria`. Campos:

| Campo | Significado |
| --- | --- |
| `wifi` | `"Conectado"`, `"Desconectado"` o `"Portal AP"`. |
| `modoWifi` | `"STA"` o `"AP"`. |
| `ssid` | Nombre de la red de casa, o el nombre del AP si está en portal. |
| `internet` | `"Conectado"` o `"Sin acceso"`. Es el resultado de abrir `example.com:80`, no un envío de datos. |
| `ip` | `WiFi.localIP()`. |
| `rssi` | dBm si está conectado; si no, `0`. |
| `bmeOK`, `ajOK`, `tcsOK` | `true` / `false`. |
| `temperatura` | °C, 2 decimales. |
| `humedad` | % HR, 2 decimales. |
| `presion` | hPa, 2 decimales (`readPressure()/100`). |
| `distancia` | cm, 2 decimales. `-1` si no hay eco válido. |
| `luz` | Índice del TCS230: `1000000 / ancho_del_pulso`. `-1` si no hay pulso. |
| `lux` | `luz * 1.5` si el índice es ≥ 0; si no, `0`. |
| `vpd` | kPa, 3 decimales. |
| `evaporacion` | % de probabilidad, 1 decimal. |
| `scoreRiesgo` | 0–100 (el score interno 0–1 multiplicado por 100), 1 decimal. |
| `riesgo` | `"BAJO"`, `"MEDIO"`, `"ALTO"` o `"SIN BASE"`. |
| `baseGuardada` | Si se pulsó «Guardar base» o se llamó `/savebase`. |
| `alertaLargoPlazo` | Sequía por semanas consecutivas. |
| `semanasAltoRiesgo` | Cuántas semanas seguidas superaron el umbral. |
| `autoCantidad`, `scoreAuto`, `estadoAuto` | Simulación automática del menú (`"RECOLECTANDO"`, `"BAJO"`, `"MEDIO"`, `"ALTO"`). |
| `matriz` | `"Corazon"`, `"Sistema OK"` o `"Apagada"`. |
| `historyCount` | Cuántos registros hay en el anillo (máximo 160). |
| `historyAutoMin` | `0`, `15`, `60` o `360`. Al nacer la meta, el valor es **360** (cada 6 horas). |
| `timeOK` | Si el reloj NTP ya sincronizó. |
| `epoch` | Segundos UTC desde 1970, o `0` si no hay hora. |
| `uptime` | Segundos desde el encendido (`millis()/1000`). |

Si el JSON de `/data` llegó, el panel React marca el nodo en línea, también cuando `wifi` es `"Portal AP"`.

El servicio React traduce nombres: `temperatura` → `temperature`, `humedad` → `humidity`, `presion` → `pressure`, `distancia` → `distance`, `luz`/`lux` → `light` y `lightIndex`, `scoreRiesgo` → `score`. Si `epoch > 1700000000` la hora de la lectura es esa; si no, usa la hora del PC.

Validación en el navegador (no en el Arduino): temperatura −20…70 °C, humedad 0…100, presión 500…1100 hPa, distancia 2…100 cm, luz 0…100000, VPD 0…8, evaporación y score 0…100. Un valor negativo se trata como sensor sin lectura.

### 6.4 JSON de `GET /history`

```json
{
  "count": 12,
  "max": 160,
  "autoMin": 360,
  "timeOK": true,
  "records": [
    {
      "t": 1710000000,
      "temp": 24.10,
      "hum": 61.20,
      "pres": 752.30,
      "dist": 42.15,
      "luz": 800,
      "vpd": 1.234,
      "evap": 40.5,
      "score": 12.0,
      "manual": false
    }
  ]
}
```

Orden: del más viejo al más nuevo. `manual` es true si el registro lo creó `GET /measure` (bit `0x08`).

### 6.5 JSON de `GET /measure`

```json
{"ok": true, "timeOK": true, "count": 13}
```

`ok` es false si no hay espacio lógico de histórico, o si no hay hora sincronizada y el NTP falla. Aun así la medición en vivo ya se actualizó; lo que no se guardó es la fila de EEPROM. `guardarRegistroWeb` exige `epoch >= 1700000000`.

---

## 7. WiFi: máquina de estados

No hay SSID ni clave en el código. Van en la EEPROM, al final de la memoria.

### 7.1 Encendido (`iniciarWiFi`)

1. Arma el nombre `AERIS-` + dos bytes de MAC (`WiFi.macAddress`, bytes 4 y 5), por ejemplo `AERIS-A1B2`.
2. Lee credenciales.
3. Si no hay SSID válido, abre el portal AP y termina.
4. Si hay SSID, intenta estación 20 s. Si entra, arranca el servidor, prueba internet y sincroniza la hora.
5. Si no, reintenta 12 s.
6. Si tampoco, abre el portal.

Si al encender **los dos botones están presionados** (`BTN1` y `BTN2` a LOW, porque son `INPUT_PULLUP`), borra las credenciales antes de llamar a `iniciarWiFi`, así que cae al portal.

### 7.2 Mientras corre (`gestionarWiFi`)

**Si está en portal:** solo atiende el DNS cautivo y vuelve. No intenta la red de casa hasta que alguien guarde un SSID en `/save` o se reinicie con credenciales ya guardadas.

**Si está en STA y se cayó el WiFi:**

- Cada 15 s (`INTERVALO_REINTENTO_WIFI_MS`) incrementa `fallosStaSeguidos` y llama otra vez a `WiFi.begin(ssid)` o `WiFi.begin(ssid, pass)`.
- A los **8** fallos seguidos (`MAX_FALLOS_STA_A_PORTAL`) abre el portal. 8 × 15 s ≈ **2 minutos** sin enlace.

**Si está en STA y el enlace sigue:**

- Cada 30 s vuelve a probar `example.com:80` y actualiza `internetOK`.
- Si no hay hora, o pasaron 6 horas (`INTERVALO_SYNC_HORA_MS`), vuelve a sincronizar NTP.

### 7.3 DNS cautivo (solo en el AP)

`WiFiUDP dnsUdp` en el puerto **53**. `atenderCaptiveDNS()`:

- Lee la pregunta DNS (mínimo 12 bytes, máximo 512).
- Marca la respuesta como estándar con un answer (`0x84` en el byte de flags, `ANCOUNT = 1`).
- Añade un registro A que comprime el nombre (`0xC0 0x0C`), clase IN, TTL 30 s, y la IP del AP (los 4 bytes de `WiFi.localIP()`).
- La manda al IP y puerto de quien preguntó.

Cualquier nombre que el teléfono resuelva mientras está unido a `AERIS-XXXX` recibe la IP del Arduino. Por eso al abrir el navegador suele aparecer el portal aunque se pida otra dirección. El sketch no implementa los chequeos de portal cautivo de Apple/Android (`/generate_204`, etc.) como rutas aparte: lo que no coincida con `/data`, `/history`, `/measure`, `/scan`, `/save` o `/wifi` recibe el dashboard.

### 7.4 Credenciales en EEPROM

Estructura `WifiCreds`, escrita con `EEPROM.put` en `EEPROM.length() - sizeof(WifiCreds)` (el final de la EEPROM del UNO R4):

| Campo | Valor |
| --- | --- |
| `magic` | `0xAE51C0F1` |
| `version` | `1` |
| `ssid` | 33 bytes (32 útiles + fin de cadena) |
| `pass` | 65 bytes (64 útiles + fin). Puede ir vacía (red abierta: `WiFi.begin(ssid)` sin clave). |
| `crc` | XOR de todos los bytes anteriores de la estructura. |

Si el magic, la versión, el CRC o el SSID no cuadran, se consideran inválidas y se abre el portal.

Borrar:

- Mantener BTN1 y BTN2 juntos **4 segundos** en cualquier momento del `loop`.
- Menú principal, opción 7 «Config WiFi», pulsación larga de BTN1.
- Encender con los dos botones ya presionados.

Después de borrar se abre el AP.

### 7.5 Formulario

`GET` a `/save` con query `s` y `p` (no es POST). El HTML del formulario está armado a mano en `enviarPaginaPortal` (no es el dashboard grande). Textos: SSID, contraseña, «Guardar y conectar», enlace a `/scan` y al panel.

---

## 8. Hora e «internet del Arduino»

El panel local **no sube mediciones**. El WiFi del Arduino sí puede salir a internet, y solo para dos cosas:

1. **Saber si hay salida.** `comprobarInternet()` abre un cliente TCP a `example.com` puerto 80, manda un `GET /` mínimo y cierra. El resultado es el campo `internet` del JSON. Si falla, el resto del monitor sigue.
2. **Poner en hora los registros.** Sin epoch no se guarda histórico (`guardarRegistroWeb` devuelve false).

Sincronización (`sincronizarHoraInternet`):

1. Hasta 3 veces `WiFi.getTime()`. Vale si el número es ≥ `1700000000` (un instante de 2023 en adelante; descarta 0 y relojes absurdos).
2. Si no alcanza, NTP manual: UDP local en el puerto **2390** hacia `162.159.200.123:123` (servidor de tiempo de Cloudflare). Paquete NTP de 48 bytes, LI/VN/Mode `0b11100011`, espera hasta 1,8 s. Resta `2208988800` para pasar de 1900 a 1970.

El sketch guarda `epochBaseUTC` y `millisBaseEpoch`. La hora actual es `epochBaseUTC + (millis() - millisBaseEpoch) / 1000`. No hay RTC con pila. Si se reinicia el Arduino, hay que volver a sincronizar. En modo portal se pone `horaSincronizada = false` a propósito.

Reintento automático: al conectar, y luego cada 6 horas, o en cada guardado si todavía no hay hora.

Consecuencia práctica:

- En la red de casa, con internet en el Arduino, el histórico tiene fecha real.
- En el AP `AERIS-XXXX`, `/data` funciona y el panel se ve, pero `/measure` casi seguro responde `ok: false` hasta que el Arduino esté en un WiFi con salida NTP. El React, si `epoch` es 0, etiqueta la lectura con la hora del navegador.

---

## 9. EEPROM del histórico

Hay **dos** históricos distintos en la misma EEPROM.

### 9.1 Semanas de sequía (menú del LCD)

Cuatro `Semana` (nivel, temperatura, humedad, presión mínima, presión máxima, luz, flag `valida`). Se promedian muestras del `loop` y, cada `7 * 24 * 60 * 60 * 1000` ms, se cierra una semana y se escribe con `EEPROM.put`. El índice de la semana actual va justo después del arreglo.

Eso alimenta el menú «Ver historial» y la alerta de largo plazo. **No** es el JSON de `/history`.

Alerta de sequía semanal: score > 0,6 durante al menos 2 semanas seguidas. Pesos de ese score: nivel bajando 0,4; humedad baja 0,2; temperatura alta 0,2; presión alta y estable 0,1; luz alta 0,1. Umbrales: bajada de nivel > 0,3 cm frente a la semana anterior, humedad < 30 %, temperatura > 30 °C, presión media > 745 hPa con rango < 2 hPa, luz > 700.

### 9.2 Registros que ve el panel (`/history`)

`MetaHistoricoWeb` en la dirección:

```
EEPROM_HIST_WEB_META = 4 * sizeof(Semana) + sizeof(uint8_t) + 16
```

El `+ 16` es un hueco a propósito para no pisar el índice de semanas. Los registros empiezan en `EEPROM_HIST_WEB_META + sizeof(MetaHistoricoWeb)`.

Meta:

| Campo | Valor |
| --- | --- |
| `magic` | `0x48575234` |
| `version` | `1` |
| `siguiente` | Índice donde se escribirá el próximo (anillo). |
| `cantidad` | Cuántos hay, tope 160. |
| `intervaloAutoMin` | 0, 15, 60 o 360. Por defecto 360 al inicializar. |

Cada `RegistroWeb` lleva: `epochUTC`, temperatura, humedad, presión, distancia, luz (`int32`), VPD, evaporación, score (ya en 0–100), `flags` y 3 bytes reservados.

Flags:

| Bit | Significado |
| --- | --- |
| `0x01` | BME280 válido en ese instante |
| `0x02` | AJ-SR04M válido |
| `0x04` | TCS230 válido |
| `0x08` | Medición pedida por la web (`manual`) |

Al guardar se escribe en `siguiente`, se avanza el índice módulo 160 y, si aún no se llenó, se incrementa `cantidad`. Al llenarse, se sobrescribe el más viejo. El JSON reconstruye el orden desde `(siguiente - cantidad) mod 160`.

En `inicializarHistoricoWeb`, si `ultimoByte > EEPROM.length()` el histórico web se desactiva (`historicoWebDisponible = false`). Si la meta está corrupta (magic, versión, índices o intervalo ilegal), se reinicia en cero con intervalo 360. **Eso borra la meta, no recorre a borrar los 160 registros uno por uno**; al poner `cantidad = 0` el panel deja de mostrarlos.

El guardado automático corre con el intervalo de la EEPROM (0, 15, 60 o 360 minutos) esté el Arduino en el portal o en una red. Si hay hora NTP, el registro lleva fecha UTC. Si no hay internet, igual se guarda: `t` es el segundo de encendido y el JSON marca `"rel": true`. El botón de medir lee `ok`; si la EEPROM no acepta la fila, la página lo dice.

---

## 10. Sensores, pines y fórmulas

### 10.1 Pines

| Señal | Pin | Notas |
| --- | --- | --- |
| TRIG del AJ-SR04M | D2 | Salida. Pulso HIGH de 20 µs, antes 5 µs en LOW. |
| ECHO del AJ-SR04M | D3 | Entrada. Interrupción `CHANGE` mientras se espera el eco. |
| TCS230 OUT | D6 | Interrupción `CHANGE` solo durante la medición de un pulso. |
| TCS230 S2 | D4 | HIGH en el arranque (escala de frecuencia). |
| TCS230 S3 | D5 | LOW. |
| LED verde | D7 | |
| LED amarillo | D8 | |
| LED rojo | D9 | |
| Botón 1 | D11 | `INPUT_PULLUP`. Interrupción `CHANGE`. LOW = presionado. |
| Botón 2 | D12 | Igual. |
| TCS230 S1 | D13 | LOW = escala 2 % (según el código). |
| LCD I2C | dirección `0x27`, 16×2 | `LiquidCrystal_I2C`. |
| BME280 I2C | prueba `0x76` y, si falla, `0x77` | Temperatura, humedad, presión. |

Matriz LED: `ArduinoLEDMatrix` del propio UNO (12×8). Al arrancar, apagada. Si WiFi + los tres sensores + servidor están bien, palomita y LCD «Sistema OK» más la IP. Si falta algo, «REVISAR SISTEMA» y el nombre de lo que falló (WiFi / IP, BME280, AJ-SR04M, TCS230 o servidor web). En portal, la matriz queda apagada y el LCD rota «Config WiFi» / nombre AP y «Abra el AP y» / IP.

### 10.2 Distancia (AJ-SR04M)

El rango útil es el de la zona ciega del sensor:

- `DISTANCIA_MIN_UTIL_CM = 20`
- `DISTANCIA_MAX_UTIL_CM = 400`
- El flanco de subida y el de bajada del eco los toma la interrupción de D3. El tiempo de espera máximo sigue siendo ida y vuelta a 4 m más 5 ms.
- 3 muestras, separación 70 ms sin `delay` en el ciclo, mediana y el mismo suavizado de antes.
- Suavizado exponencial: `alfa = 0.30` → `0.30 * mediana + 0.70 * anterior`.
- Un salto de más de 80 cm respecto al suavizado se rechaza. Al segundo salto seguido se acepta y se reinicia el contador (`SALTOS_PARA_REBLOQUEAR = 2`).
- Si las 3 muestras fallan, se invalida el suavizado y `distancia = -1`, `ajOK = false`.
- El modo esperado del sensor es pulso compatible con HC-SR04, resistencia R19 abierta. VCC 5 V, GND, TRIG/RX en D2, ECHO/TX en D3.
- En el `loop`, la distancia se refresca cada **350 ms**.
- Cada **1 s** el Serial (9600) imprime cruda, filtrada y microsegundos de eco, o «SIN LECTURA».

### 10.3 Luz (TCS230)

La interrupción de D6 mide un solo pulso en bajo y luego se desengancha. Si el ancho es 0 o menor de 12 µs, la lectura es inválida (`-1`). Si no, índice = `1000000 / ancho`. En la tarea de sensores, cada **2 s**, junto con el BME280. Lux aproximado = índice × **1.5** (`FACTOR_APROX_LUX`). El React usa el campo `lux` si viene; si no, aplica el mismo 1.5. Una distancia entre 20 y 400 cm no se marca como anómala.

### 10.4 VPD y evaporación

```
es = 0.6108 * exp(17.27 * T / (T + 237.3))
ea = es * (HR / 100)
VPD = max(0, es - ea)          // kPa
```

Probabilidad de evaporación, 0–100:

```
prob = (VPD / 2.5) limitado a 0..1, en porcentaje
     + (luz / 40000) limitado a 0..1, por 10
```

LEDs (si no está la simulación automática en pantalla):

- VPD ≥ 2,5 kPa → rojo parpadeando cada 400 ms.
- VPD ≥ 1,2 kPa → amarillo parpadeando.
- Si no, verde / amarillo / rojo fijos según riesgo 0 / 1 / 2.

### 10.5 Riesgo instantáneo (hace falta una línea base)

Sin «Guardar base» o `GET /savebase`: estado `-1`, texto `SIN BASE`, score 0.

Con base, las variaciones se normalizan y se suman:

| Variable | Cómo sube el riesgo | Peso | Umbral que equivale a 1,0 |
| --- | --- | --- | --- |
| Nivel | La distancia **aumenta** respecto a la base (el agua baja) | 0,4 | 1,0 cm |
| Temperatura | Sube respecto a la base | 0,2 | 6,0 °C |
| Humedad | **Baja** respecto a la base | 0,2 | 20 puntos |
| Luz | El índice sube respecto a la base | 0,2 | 300 |

Score ≥ 0,6 → ALTO (2). Score ≥ 0,3 → MEDIO (1). Si no, BAJO (0). Atajo: si el nivel bajó más de **0,8 cm** respecto a la base, ALTO directo.

Esos pesos son los del riesgo en vivo. Los de la sequía semanal (sección 9.1) y los de la simulación son otros.

### 10.6 Simulaciones del menú (no pasan por `/history`)

- **Sim. manual:** 4 muestras, cada una esperando el botón 1. Score de sequía con la primera y la última distancia y los promedios. Muestra el resultado 4 s en el LCD.
- **Sim. auto 4 s:** ventana de 4 muestras, una cada 4 s, desplazando las viejas. LEDs siguen `estadoAuto` mientras esa pantalla está abierta.

Umbrales de ese score: nivel bajando > 0,3 cm entre la primera y la última de la ventana, humedad promedio < 30, temperatura promedio > 30, presión promedio > 745 con rango < 2, luz promedio > 700. Mismos pesos 0,4 / 0,2 / 0,2 / 0,1 / 0,1. Alto si ≥ 0,6, medio si ≥ 0,3.

### 10.7 Menú físico

Pulsador 1: corto = avanzar, largo (≥ 600 ms) = entrar. Pulsador 2: volver. Antirrebote 30 ms.

Opciones: 1 Ver variables, 2 Guardar base, 3 Ver historial (4 semanas, 3 páginas), 4 Sim. manual, 5 Sim. auto 4 s, 6 Evaporación (probabilidad y VPD), 7 Config WiFi (borra y abre portal), 8 Volver al inicio.

Rotación del LCD cada 3 s: temperatura/humedad, presión, distancia, luz. En portal, esa rotación se sustituye por las dos páginas del AP.

---

## 11. Panel HTML que va dentro del programa

Origen legible: `local/arduino/MonitorHidrico_LOCAL/dashboard.html`.

Lo que el compilador mete en la flash: `dashboard_page.h`, cadena cruda `AERIS_PAGE`. `enviarDashboard` la escribe en trozos de 480 bytes. Cabeceras: `text/html; charset=utf-8`, `Cache-Control: no-store`, `Connection: close`.

Páginas del propio HTML (botones, sin cambiar de URL):

- **Panel:** 8 indicadores (temperatura, humedad, presión, distancia, luz en lx, VPD, evaporación, riesgo), anillo de score, cuatro lienzos (temperatura+humedad, VPD+evaporación, presión, luz) con las últimas 48 filas, y alertas.
- **Tiempo real:** elige una variable y grafica esa serie.
- **Histórico:** tabla, últimas 60 filas, hora `es-CO` si `t > 1700000000`; si no, el texto «ahora».
- **Dispositivo:** SSID, IP, dBm, internet, estado de los tres sensores, riesgo, cuántos registros hay en EEPROM, uptime, matriz, intervalo.
- Enlace **WiFi** → `/wifi`.
- **Tema** claro/oscuro (`data-theme="dark"` en `<html>`).
- **Tomar medicion** → `/measure`.

Umbrales de alerta de esta página (solo visuales, en el navegador del teléfono):

| Variable | Por debajo | Por encima |
| --- | --- | --- |
| Temperatura | 8 °C | 38 °C |
| Humedad | 20 % | 95 % |
| Presión | 650 hPa | 850 hPa |
| VPD | 0,1 kPa | 2,2 kPa |
| Evaporación | — | 75 % |
| Riesgo | — | 75 % |

Esos números coinciden con `defaultThresholds` de `src/config/sensors.ts`, que usa el panel React. En el React se pueden cambiar y quedan solo en ese navegador, no en el Arduino.

Colores del anillo de riesgo: &lt; 30 verde `#2b8b69`, &lt; 60 ámbar `#bb8c31`, &lt; 80 naranja `#c76b2f`, si no rojo `#bf4848`.

Si `fetch` falla: «Sin respuesta del Arduino.»

---

## 12. Panel React: qué pantallas quedan y qué datos usan

`local/src/main.tsx` monta `src/App.tsx`. Las páginas son las de siempre, leyendo el arreglo que llenó `useSensorData`:

| Página | Qué muestra de los datos locales |
| --- | --- |
| Panel (`Dashboard`) | Últimas lecturas filtradas por el rango de la barra, KPIs, riesgo, alertas con los umbrales del navegador. |
| Histórico | Tabla del rango elegido. |
| Tiempo real | Serie y última lectura. |
| Analítica | Estadísticos del rango y del periodo anterior. |
| Correlaciones | Dispersión entre variables del rango. |
| Alertas | Cruces de umbral. |
| Ajustes | Umbrales en memoria del navegador y la URL del Arduino (`aeris.local.arduinoUrl`). |
| Dispositivo | IP, RSSI, SSID, modo AP/STA, internet, NTP, sensores, riesgo, cantidad EEPROM, uptime, matriz. El texto de integración nombra `/data`, `/history`, `/measure`, el AP `AERIS-XXXX` y el borrado con los dos botones. |

Rango de la barra (en minutos, salvo personalizado): 1 min, 1 h, 6 h, 12 h, 24 h (valor inicial `1440`), 7 días, 30 días, 3 meses, 6 meses, 1 año, o desde/hasta manual.

La lista completa que hay en memoria del navegador es, como máximo, lo que cupo en las 160 filas de EEPROM más la lectura en vivo. Filtrar «1 año» no inventa datos: solo muestra las filas cuyo `t` cae en ese intervalo. Si el Arduino lleva poco tiempo guardando, el gráfico sale corto.

Primera petición de histórico: desde hace 30 días hasta ahora. El filtro de la barra se aplica encima, en el PC.

---

## 13. Secuencia completa, de encendido a un punto en la gráfica

1. Se sube **solo** la carpeta `local/arduino/MonitorHidrico_LOCAL/` al UNO R4 WiFi (Arduino IDE: abrir el `.ino`; tiene que ver `dashboard_page.h` al lado).
2. `setup`: Serial 9600, matriz apagada, LCD «Iniciando...», BME280, pines del ultrasonido, 800 ms, primera distancia filtrada, diagnóstico por Serial, TCS230, prueba de LEDs, botones, posible borrado de WiFi, meta del histórico web, `iniciarWiFi`, carga de las 4 semanas, evaluación de sequía, 1,8 s con el estado de sensores, 5 s con «Sistema OK» o el fallo, y a partir de ahí el `loop`.
3. Si no hay clave, el teléfono se une a `AERIS-XXXX`, el DNS cautivo apunta a `192.168.4.1`, abre el navegador y o bien ve el dashboard o entra a `/wifi`, escribe SSID y clave, y el Arduino guarda e intenta unirse.
4. En la red de destino, el LCD muestra la IP. `server` escucha en el puerto 80.
5. Cada ~2 s se leen clima y luz; cada ~350 ms la distancia; se evalúa el riesgo; si toca, se appende una fila en EEPROM (por defecto a las 6 horas, y solo con hora NTP).
6. Alguien abre `http://IP/` y el Arduino escupe el HTML. El JavaScript de esa página pide `/history` y `/data` y repite cada 10 s.
7. Otra persona, en un PC de la misma red, pone esa IP en `local/.env`, ejecuta `npm run dev` dentro de `local/` y abre `http://localhost:5174`. El navegador pide `http://localhost:5174/arduino/data`. Vite reenvía a `http://IP/data`. El Arduino contesta el JSON. React lo dibuja. A los 10 s se repite solo `/data`.

Para comprobar el Arduino sin el panel: en el navegador, `http://IP-DEL-ARDUINO/data`.

---

## 14. Qué queda fuera de esta versión

- No hay `POST` a `iot-monitor-pro.vercel.app` ni cabecera `X-AERIS-KEY`.
- No se usa `api/arduino/[...path].js` ni el proxy de Vercel.
- El histórico no está en una base de datos de internet. Está en la EEPROM del UNO (160 filas como tope) y, para las semanas del menú, en otras 4 ranuras.
- El PC no necesita internet para ver el panel de la LAN. El Arduino sí lo necesita si se quieren fechas reales en el histórico.
- Subir el sketch de la nube y el local a la vez no es posible: el UNO solo ejecuta un programa. El que se flashea último es el que queda.

---

## 15. Librerías que el sketch local incluye

`Wire.h`, `EEPROM.h`, `string.h`, `stdlib.h`, `LiquidCrystal_I2C.h`, `Adafruit_Sensor.h`, `Adafruit_BME280.h`, `WiFiS3.h`, `Arduino_LED_Matrix.h`, `math.h`, y `dashboard_page.h`.

No incluye `WiFiSSLClient.h`.
