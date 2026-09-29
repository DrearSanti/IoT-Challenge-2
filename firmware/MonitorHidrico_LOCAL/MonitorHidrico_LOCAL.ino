/*
 * ============================================================================
 *  MONITOR HIDRICO LOCAL — mini servidor en la IP del WiFi (puerto 80)
 *  SUBA ESTE ARCHIVO al UNO R4 WiFi (Arduino IDE: esta carpeta).
 *  La pagina del dashboard y el historico quedan en el Arduino.
 *  Al abrir http://192.168.4.1/ (red AERIS-XXXX) sale el panel.
 * ============================================================================
 *
 *  Parche distancia (AJ-SR04M):
 *  - Zona ciega: por debajo de 20 cm la lectura se descarta.
 *  - Rango de trabajo: 20 a 400 cm.
 *  - El flanco del eco entra por interrupcion en D3. No usa pulseIn.
 *  - 3 muestras separadas 70 ms, sin frenar el ciclo con delay().
 *  - Pulso TRIG de 20 us y timeout adecuado para 4 m.
 *  - Si no hay eco, reinicia el suavizado y lo informa por Serial.
 *  - Requiere AJ-SR04M en modo pulso compatible HC/HR-SR04 (R19 abierto).
 *
 *  Concurrencia en el RA4M1:
 *  - Interrupcion de eco (D3), de la luz (D6) y de BTN1/BTN2.
 *  - FspTimer a 1 kHz. Cada 2 ms corre la tarea de red y cada 5 ms
 *    la de sensores. El temporizador solo sube un reloj y deja un aviso:
 *    WiFi, I2C, LCD y EEPROM se atienden en esas tareas, porque esas
 *    librerias no se pueden llamar desde la interrupcion ni desde dos
 *    hilos a la vez.
 *  - El puerto 80 admite hasta dos clientes y los atiende por turnos.
 *    Una medicion o el historico no congelan el otro cliente ni los botones.
 *
 *  WiFi profesional (sin editar el codigo):
 *  - SSID y clave se guardan en EEPROM (no van en el sketch).
 *  - Si no hay red, falla o se borra: abre AP AERIS-XXXX + portal cautivo.
 *  - Telefono: unirse al AP, pagina de configuracion, Guardar y conectar.
 *  - Reconexion automatica. Si pierde la red ~2 min, vuelve a abrir el portal.
 *  - Borrar WiFi: mantener BTN1+BTN2 4 s, o menu >7 Config WiFi.
 *  - MODO LOCAL: mini servidor HTTP en el puerto 80. Sin puente a la nube.
 *  - El dashboard de la carpeta local/ consume /data, /history y /measure.
 * ============================================================================
 */

#include <Wire.h>
#include <EEPROM.h>
#include <string.h>
#include <stdlib.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFiS3.h>
#include "Arduino_LED_Matrix.h"
#include <math.h>
#include "FspTimer.h"
#include "dashboard_page.h"

LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_BME280 bme;

char ssid[33] = "";
char pass[65] = "";
char apNombre[16] = "AERIS-SETUP";
bool credencialesWifiOK = false;
bool modoPortal = false;
uint8_t fallosStaSeguidos = 0;
const uint8_t MAX_FALLOS_STA_A_PORTAL = 8;
const uint32_t WIFI_CREDS_MAGIC = 0xAE51C0F1UL;
const uint8_t WIFI_CREDS_VERSION = 1;

struct WifiCreds {
  uint32_t magic;
  uint8_t version;
  char ssid[33];
  char pass[65];
  uint8_t crc;
};

WiFiServer server(80);
WiFiUDP Udp;
WiFiUDP dnsUdp;
bool udpIniciado = false;
bool dnsPortalIniciado = false;
bool servidorWebIniciado = false;
bool internetOK = false;
unsigned long tUltimoIntentoWiFi = 0;
unsigned long tUltimaPruebaInternet = 0;
const unsigned long INTERVALO_REINTENTO_WIFI_MS = 15000;
const unsigned long INTERVALO_INTERNET_MS = 30000;

bool horaSincronizada = false;
uint32_t epochBaseUTC = 0;
unsigned long millisBaseEpoch = 0;
unsigned long tUltimaSyncHora = 0;
const unsigned long INTERVALO_SYNC_HORA_MS = 6UL * 60UL * 60UL * 1000UL;
const unsigned int PUERTO_NTP_LOCAL = 2390;

ArduinoLEDMatrix matrix;
bool corazonMatriz = false;
bool palomitaMatriz = false;

byte ICONO_OK[8] = {
  0b00000, 0b00001, 0b00010, 0b10100, 0b01000, 0b00000, 0b00000, 0b00000
};

uint8_t PALOMITA_OK[8][12] = {
  {0,0,0,0,0,0,0,0,0,1,1,0},
  {0,0,0,0,0,0,0,0,1,1,0,0},
  {0,0,0,0,0,0,0,1,1,0,0,0},
  {1,0,0,0,0,0,1,1,0,0,0,0},
  {1,1,0,0,0,1,1,0,0,0,0,0},
  {0,1,1,0,1,1,0,0,0,0,0,0},
  {0,0,1,1,1,0,0,0,0,0,0,0},
  {0,0,0,1,0,0,0,0,0,0,0,0}
};

uint8_t CORAZON[8][12] = {
  {0,0,1,1,0,0,0,1,1,0,0,0},
  {0,1,1,1,1,0,1,1,1,1,0,0},
  {1,1,1,1,1,1,1,1,1,1,1,0},
  {1,1,1,1,1,1,1,1,1,1,1,0},
  {0,1,1,1,1,1,1,1,1,1,0,0},
  {0,0,1,1,1,1,1,1,1,0,0,0},
  {0,0,0,1,1,1,1,1,0,0,0,0},
  {0,0,0,0,1,1,1,0,0,0,0,0}
};

uint8_t MATRIZ_APAGADA[8][12] = {
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0}
};

#define TRIG_PIN 2
#define ECHO_PIN 3
#define TCS_S2 4
#define TCS_S3 5
#define TCS_OUT 6
#define TCS_S1 13
#define LED_VERDE 7
#define LED_AMARILLO 8
#define LED_ROJO 9
#define BTN1 11
#define BTN2 12

const float UMBRAL_CRITICO_NIVEL_CM = 1.0;
const float UMBRAL_CRITICO_TEMP_C   = 6.0;
const float UMBRAL_CRITICO_HUM_PC   = 20.0;
const long  UMBRAL_CRITICO_LUZ      = 300;
const float UMBRAL_DESCENSO_EMERGENCIA_CM = 0.8;
const float PESO_NIVEL   = 0.4;
const float PESO_TEMP    = 0.2;
const float PESO_HUMEDAD = 0.2;
const float PESO_LUZ     = 0.2;
const float UMBRAL_SCORE_ALERTA  = 0.3;
const float UMBRAL_SCORE_CRITICO = 0.6;
const float UMBRAL_HUMEDAD_BAJA        = 30.0;
const float UMBRAL_TEMP_ALTA           = 30.0;
const float UMBRAL_PRESION_ALTA        = 745.0;
const float PRESION_ESTABLE_RANGO      = 2.0;
const long  UMBRAL_LUZ_ALTA            = 700;
const float UMBRAL_NIVEL_BAJANDO_CM    = 0.3;
const float UMBRAL_SCORE_SEQUIA        = 0.6;
const uint8_t SEMANAS_CONSECUTIVAS_ALERTA = 2;

const uint8_t MUESTRAS_MEDIANA = 3;
const unsigned long SEPARACION_MUESTRAS_MS = 70;
const float ALFA_SUAVIZADO = 0.30;
const float SALTO_MAXIMO_ADMISIBLE_CM = 80.0;
const float DISTANCIA_MIN_UTIL_CM = 20.0;
const float DISTANCIA_MAX_UTIL_CM = 400.0;
const uint8_t SALTOS_PARA_REBLOQUEAR = 2;

float distanciaSuavizada = -1;
bool  suavizadoInicializado = false;
uint8_t saltosRechazadosSeguidos = 0;
float ultimaDistanciaCruda = -1;
unsigned long ultimaDuracionEcoUs = 0;
unsigned long tUltimoDebugDistancia = 0;

bool bmeOK = false;
bool ajOK = false;
bool tcsOK = false;

float temperatura = 0;
float humedad = 0;
float presion = 0;
float distancia = -1;
long luz = -1;

bool hayBaseGuardada = false;
float temperaturaBase = 0;
float humedadBase = 0;
float presionBase = 0;
float distanciaBase = -1;
long luzBase = -1;

int8_t estadoRiesgo = -1;
float scoreRiesgo = 0;
bool alarmaRojaActiva = true;

struct Semana {
  float nivel;
  float temperatura;
  float humedad;
  float presionMin;
  float presionMax;
  long  luz;
  uint8_t valida;
};

const uint8_t TOTAL_SEMANAS_HISTORIAL = 4;
Semana historial[TOTAL_SEMANAS_HISTORIAL];
uint8_t indiceSemanaActual = 0;
const unsigned long INTERVALO_SEMANA_MS = 7UL * 24UL * 60UL * 60UL * 1000UL;
unsigned long tUltimaSemana = 0;
float accNivel = 0, accTemp = 0, accHum = 0;
float presMinSemana = 99999, presMaxSemana = -99999;
long accLuz = 0;
unsigned long contMuestrasSemana = 0;
bool alertaSequiaLargoPlazo = false;
uint8_t semanasConsecutivasAltoRiesgo = 0;

struct RegistroWeb {
  uint32_t epochUTC;
  float temperatura;
  float humedad;
  float presion;
  float distancia;
  int32_t luz;
  float vpd;
  float evaporacion;
  float score;
  uint8_t flags;
  uint8_t reservado[3];
};

struct MetaHistoricoWeb {
  uint32_t magic;
  uint16_t version;
  uint16_t siguiente;
  uint16_t cantidad;
  uint16_t intervaloAutoMin;
};

const uint32_t HIST_WEB_MAGIC = 0x48575234UL;
const uint16_t HIST_WEB_VERSION = 1;
const uint16_t MAX_REGISTROS_WEB = 160;
const int EEPROM_HIST_WEB_META =
  TOTAL_SEMANAS_HISTORIAL * sizeof(Semana) + sizeof(uint8_t) + 16;
const int EEPROM_HIST_WEB_DATOS =
  EEPROM_HIST_WEB_META + sizeof(MetaHistoricoWeb);

MetaHistoricoWeb metaWeb;
bool historicoWebDisponible = true;
unsigned long tUltimoGuardadoAutoWeb = 0;

const uint8_t NUM_MUESTRAS_SIMULACION = 4;
const uint8_t VENTANA_AUTO = 4;
const unsigned long INTERVALO_MUESTRA_AUTO_MS = 4000;
float autoNivel[VENTANA_AUTO];
float autoTemp[VENTANA_AUTO];
float autoHum[VENTANA_AUTO];
float autoPres[VENTANA_AUTO];
long  autoLuz[VENTANA_AUTO];
uint8_t autoCantidad = 0;
unsigned long tUltimaMuestraAuto = 0;
float scoreAuto = 0;
int8_t estadoAuto = -1;
bool autoTomandoMuestra = false;

const float FACTOR_APROX_LUX = 1.5;
struct EvBtn { uint8_t nivel; uint32_t tick; };
float aproximarLux(long indiceLuz) {
  if (indiceLuz < 0) return 0;
  return indiceLuz * FACTOR_APROX_LUX;
}
const float VPD_UMBRAL_ALTO_KPA     = 1.2;
const float VPD_UMBRAL_MUY_ALTO_KPA = 2.5;
const long  EVAP_LUZ_MAX = 40000;

float calcularVPD(float tempC, float humedadRelPc) {
  float es = 0.6108 * exp((17.27 * tempC) / (tempC + 237.3));
  float ea = es * (humedadRelPc / 100.0);
  float vpd = es - ea;
  if (vpd < 0) vpd = 0;
  return vpd;
}

float calcularProbabilidadEvaporacion() {
  float vpd = calcularVPD(temperatura, humedad);
  float probVPD = constrain(vpd / VPD_UMBRAL_MUY_ALTO_KPA, 0.0, 1.0) * 100.0;
  float aporteLuz = constrain((float)luz / (float)EVAP_LUZ_MAX, 0.0, 1.0);
  float ajusteRadiacion = aporteLuz * 10.0;
  return constrain(probVPD + ajusteRadiacion, 0.0, 100.0);
}

unsigned long tUltimaDistancia = 0;
const unsigned long INTERVALO_DISTANCIA_MS = 350;
unsigned long tUltimoAmbiente = 0;
const unsigned long INTERVALO_AMBIENTE_MS = 2000;

enum Nivel {
  NIVEL_ROTACION, NIVEL_MENU_PRINCIPAL, NIVEL_MENU_VARIABLES,
  NIVEL_MOSTRANDO_VARIABLE, NIVEL_CONFIRMACION, NIVEL_HISTORIAL,
  NIVEL_EVAPORACION, NIVEL_SIM_AUTO, NIVEL_SIM_MANUAL
};
Nivel nivel = NIVEL_ROTACION;
unsigned long tUltimaPantalla = 0;
const unsigned long INTERVALO_PANTALLA_MS = 3000;
uint8_t pantallaActual = 0;
const uint8_t TOTAL_PANTALLAS = 4;
unsigned long tUltimoRefrescoVariable = 0;
const unsigned long INTERVALO_REFRESCO_VARIABLE_MS = 500;
unsigned long tInicioConfirmacion = 0;
const unsigned long DURACION_CONFIRMACION_MS = 1200;

const char* const OPCIONES_MENU_PRINCIPAL[] = {
  ">1 Ver variables", ">2 Guardar base", ">3 Ver historial",
  ">4 Sim. manual", ">5 Sim. auto 4s", ">6 Evaporacion",
  ">7 Config WiFi", ">8 Volver inicio"
};
const uint8_t TOTAL_OPCIONES_MENU = 8;
uint8_t opcionMenuPrincipal = 0;
const char* const NOMBRES_VARIABLES[] = {
  "Temperatura", "Humedad", "Presion", "Distancia", "Indice de luz"
};
const uint8_t TOTAL_VARIABLES = 5;
uint8_t variableSeleccionada = 0;
const char* leerOpcionMenu(uint8_t i) {
  if (i >= TOTAL_OPCIONES_MENU) return "Error menu";
  return OPCIONES_MENU_PRINCIPAL[i];
}
const char* leerNombreVariable(uint8_t i) {
  if (i >= TOTAL_VARIABLES) return "Error variable";
  return NOMBRES_VARIABLES[i];
}
uint8_t semanaHistorialMostrada = 0;
uint8_t paginaHistorial = 0;
uint8_t paginaEvaporacion = 0;

const unsigned long UMBRAL_MANTENIDO_MS = 600;
const unsigned long DEBOUNCE_MS = 30;
bool btn1RawAnterior = false;
unsigned long tBtn1CambioRaw = 0;
bool btn1Estable = false;
unsigned long tBtn1Inicio = 0;
bool btn1LargoDisparado = false;
bool btn2RawAnterior = false;
unsigned long tBtn2CambioRaw = 0;
bool btn2Estable = false;
unsigned long tBtn2Inicio = 0;
bool wifiResetDisparado = false;

FspTimer relojTareas;
volatile uint32_t tickMs = 0;
volatile uint8_t pedirRed = 0;
volatile uint8_t pedirSensor = 0;
bool timerOk = false;

void isrReloj(timer_callback_args_t *args) {
  (void)args;
  tickMs++;
  if ((tickMs & 1u) == 0u) pedirRed = 1;
  if ((tickMs % 5u) == 0u) pedirSensor = 1;
}

bool iniciarRelojTareas() {
  uint8_t tipo = GPT_TIMER;
  int8_t canal = FspTimer::get_available_timer(tipo);
  if (canal < 0) {
    tipo = AGT_TIMER;
    canal = FspTimer::get_available_timer(tipo);
  }
  if (canal < 0) return false;
  if (!relojTareas.begin(TIMER_MODE_PERIODIC, tipo, canal, 1000.0f, 50.0f, isrReloj)) return false;
  if (!relojTareas.setup_overflow_irq()) return false;
  if (!relojTareas.open()) return false;
  if (!relojTareas.start()) return false;
  timerOk = true;
  return true;
}

uint32_t relojMs() { return timerOk ? tickMs : millis(); }

enum DistFase : uint8_t { DIST_REPOSO, DIST_ECO, DIST_PAUSA, DIST_LISTA };
DistFase distFase = DIST_REPOSO;
uint8_t sensorDueno = 0;
uint8_t distMuestrasTomadas = 0;
uint8_t distValidas = 0;
float distMuestras[MUESTRAS_MEDIANA];
unsigned long distEcoT0 = 0;
unsigned long distPausaHasta = 0;
float distRafagaCm = -1;

volatile uint8_t ecoFaseIsr = 0;
volatile unsigned long ecoRiseUs = 0;
volatile unsigned long ecoDuracionIsr = 0;
volatile bool ecoCapturado = false;

void isrEcho() {
  if (ecoFaseIsr == 1 && digitalRead(ECHO_PIN) == HIGH) {
    ecoRiseUs = micros();
    ecoFaseIsr = 2;
  } else if (ecoFaseIsr == 2 && digitalRead(ECHO_PIN) == LOW) {
    ecoDuracionIsr = micros() - ecoRiseUs;
    ecoFaseIsr = 0;
    ecoCapturado = true;
  }
}

void dispararTrig() {
  ecoCapturado = false;
  ecoFaseIsr = 1;
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(5);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(20);
  digitalWrite(TRIG_PIN, LOW);
  distEcoT0 = micros();
}

void cerrarMuestraDistancia(float cm) {
  ultimaDistanciaCruda = cm;
  if (cm >= DISTANCIA_MIN_UTIL_CM && cm <= DISTANCIA_MAX_UTIL_CM) {
    distMuestras[distValidas] = cm;
    distValidas++;
  }
  distMuestrasTomadas++;
  if (distMuestrasTomadas < MUESTRAS_MEDIANA) {
    distFase = DIST_PAUSA;
    distPausaHasta = millis() + SEPARACION_MUESTRAS_MS;
    return;
  }
  ecoFaseIsr = 0;
  distFase = DIST_LISTA;
  if (distValidas == 0) {
    suavizadoInicializado = false;
    distanciaSuavizada = -1;
    saltosRechazadosSeguidos = 0;
    distRafagaCm = -1;
    return;
  }
  for (uint8_t i = 1; i < distValidas; i++) {
    float clave = distMuestras[i];
    int8_t j = i - 1;
    while (j >= 0 && distMuestras[j] > clave) {
      distMuestras[j + 1] = distMuestras[j];
      j--;
    }
    distMuestras[j + 1] = clave;
  }
  float mediana;
  if (distValidas % 2 == 1) mediana = distMuestras[distValidas / 2];
  else mediana = (distMuestras[(distValidas / 2) - 1] + distMuestras[distValidas / 2]) / 2.0f;
  if (suavizadoInicializado) {
    if (fabs(mediana - distanciaSuavizada) > SALTO_MAXIMO_ADMISIBLE_CM) {
      saltosRechazadosSeguidos++;
      if (saltosRechazadosSeguidos < SALTOS_PARA_REBLOQUEAR) {
        distRafagaCm = distanciaSuavizada;
        return;
      }
      distanciaSuavizada = mediana;
      saltosRechazadosSeguidos = 0;
    } else {
      saltosRechazadosSeguidos = 0;
    }
  }
  if (!suavizadoInicializado) {
    distanciaSuavizada = mediana;
    suavizadoInicializado = true;
  } else {
    distanciaSuavizada = (ALFA_SUAVIZADO * mediana) +
                         ((1.0f - ALFA_SUAVIZADO) * distanciaSuavizada);
  }
  distRafagaCm = distanciaSuavizada;
}

void iniciarRafagaDistancia() {
  distMuestrasTomadas = 0;
  distValidas = 0;
  distRafagaCm = -1;
  distFase = DIST_ECO;
  dispararTrig();
}

void avanzarDistancia() {
  if (distFase == DIST_PAUSA) {
    if ((long)(millis() - distPausaHasta) >= 0) {
      distFase = DIST_ECO;
      dispararTrig();
    }
    return;
  }
  if (distFase != DIST_ECO) return;
  if (ecoCapturado) {
    noInterrupts();
    unsigned long dur = ecoDuracionIsr;
    bool listo = ecoCapturado;
    ecoCapturado = false;
    interrupts();
    if (!listo) return;
    ultimaDuracionEcoUs = dur;
    cerrarMuestraDistancia(dur * 0.0343f / 2.0f);
    return;
  }
  unsigned long timeoutUs = (unsigned long)((DISTANCIA_MAX_UTIL_CM * 2.0 / 0.0343) + 5000.0);
  if ((unsigned long)(micros() - distEcoT0) > timeoutUs) {
    ecoFaseIsr = 0;
    ultimaDuracionEcoUs = 0;
    cerrarMuestraDistancia(-1);
  }
}

void publicarDistanciaLista() {
  distancia = distRafagaCm;
  ajOK = (distancia > 0);
  distFase = DIST_REPOSO;
}

const unsigned long ANCHO_MINIMO_VALIDO_US = 12;
uint8_t luzDueno = 0;
bool luzMidiendo = false;
bool luzLista = false;
long luzResultado = -1;
unsigned long luzT0 = 0;
volatile uint8_t luzFaseIsr = 0;
volatile unsigned long luzBajoUs = 0;
volatile unsigned long luzAnchoIsr = 0;
volatile bool luzPulsoListo = false;

void isrLuz() {
  if (luzFaseIsr == 1 && digitalRead(TCS_OUT) == LOW) {
    luzBajoUs = micros();
    luzFaseIsr = 2;
  } else if (luzFaseIsr == 2 && digitalRead(TCS_OUT) == HIGH) {
    luzAnchoIsr = micros() - luzBajoUs;
    luzFaseIsr = 0;
    luzPulsoListo = true;
  }
}

void pedirLuz() {
  luzLista = false;
  luzPulsoListo = false;
  luzResultado = -1;
  luzFaseIsr = 1;
  luzMidiendo = true;
  luzT0 = millis();
  attachInterrupt(digitalPinToInterrupt(TCS_OUT), isrLuz, CHANGE);
}

void terminarLuzIrq() {
  detachInterrupt(digitalPinToInterrupt(TCS_OUT));
  luzFaseIsr = 0;
}

void avanzarLuz() {
  if (!luzMidiendo) return;
  if (luzPulsoListo) {
    noInterrupts();
    unsigned long ancho = luzAnchoIsr;
    bool listo = luzPulsoListo;
    luzPulsoListo = false;
    interrupts();
    if (!listo) return;
    luzMidiendo = false;
    terminarLuzIrq();
    luzResultado = (ancho < ANCHO_MINIMO_VALIDO_US) ? -1 : (1000000L / (long)ancho);
    luzLista = true;
    return;
  }
  if (millis() - luzT0 > 50UL) {
    terminarLuzIrq();
    luzMidiendo = false;
    luzResultado = -1;
    luzLista = true;
  }
}

void soltarSensores() {
  ecoFaseIsr = 0;
  ecoCapturado = false;
  luzPulsoListo = false;
  luzMidiendo = false;
  terminarLuzIrq();
  distFase = DIST_REPOSO;
  sensorDueno = 0;
  luzDueno = 0;
}

void evaluarRiesgo() {
  if (!hayBaseGuardada) { estadoRiesgo = -1; scoreRiesgo = 0; return; }
  float deltaNivel = ajOK  ? (distancia - distanciaBase)     : 0;
  float deltaTemp  = bmeOK ? (temperatura - temperaturaBase) : 0;
  float deltaHum   = bmeOK ? (humedadBase - humedad)         : 0;
  float deltaLuz   = tcsOK ? (float)(luz - luzBase)          : 0;
  float normNivel = constrain(deltaNivel / UMBRAL_CRITICO_NIVEL_CM, 0.0, 1.0);
  float normTemp  = constrain(deltaTemp  / UMBRAL_CRITICO_TEMP_C,   0.0, 1.0);
  float normHum   = constrain(deltaHum   / UMBRAL_CRITICO_HUM_PC,   0.0, 1.0);
  float normLuz   = constrain(deltaLuz   / (float)UMBRAL_CRITICO_LUZ, 0.0, 1.0);
  scoreRiesgo = (PESO_NIVEL * normNivel) + (PESO_TEMP * normTemp) +
                (PESO_HUMEDAD * normHum) + (PESO_LUZ * normLuz);
  if (ajOK && deltaNivel > UMBRAL_DESCENSO_EMERGENCIA_CM) estadoRiesgo = 2;
  else if (scoreRiesgo >= UMBRAL_SCORE_CRITICO) estadoRiesgo = 2;
  else if (scoreRiesgo >= UMBRAL_SCORE_ALERTA) estadoRiesgo = 1;
  else estadoRiesgo = 0;
}

const unsigned long INTERVALO_PARPADEO_MS = 400;
unsigned long tUltimoParpadeo = 0;
bool estadoParpadeo = false;

void escribirLedRojo(bool pedido) {
  digitalWrite(LED_ROJO, (pedido && alarmaRojaActiva) ? HIGH : LOW);
}

void actualizarLeds() {
  if (nivel == NIVEL_SIM_MANUAL) return;
  unsigned long ahoraMs = millis();
  if (ahoraMs - tUltimoParpadeo >= INTERVALO_PARPADEO_MS) {
    tUltimoParpadeo = ahoraMs;
    estadoParpadeo = !estadoParpadeo;
  }
  if (nivel == NIVEL_SIM_AUTO && estadoAuto >= 0) {
    digitalWrite(LED_VERDE,    estadoAuto == 0 ? HIGH : LOW);
    digitalWrite(LED_AMARILLO, estadoAuto == 1 ? HIGH : LOW);
    escribirLedRojo(estadoAuto == 2);
    return;
  }
  float vpd = calcularVPD(temperatura, humedad);
  if (vpd >= VPD_UMBRAL_MUY_ALTO_KPA) {
    escribirLedRojo(estadoParpadeo);
    digitalWrite(LED_AMARILLO, LOW); digitalWrite(LED_VERDE, LOW); return;
  }
  if (vpd >= VPD_UMBRAL_ALTO_KPA) {
    digitalWrite(LED_AMARILLO, estadoParpadeo ? HIGH : LOW);
    escribirLedRojo(false); digitalWrite(LED_VERDE, LOW); return;
  }
  digitalWrite(LED_VERDE,    estadoRiesgo == 0 ? HIGH : LOW);
  digitalWrite(LED_AMARILLO, estadoRiesgo == 1 ? HIGH : LOW);
  escribirLedRojo(estadoRiesgo == 2);
}

void fijarAlarmaRoja(bool activa) {
  alarmaRojaActiva = activa;
  if (!activa) {
    digitalWrite(LED_ROJO, LOW);
    return;
  }
  actualizarLeds();
}

float calcularScoreSemana(Semana actual, Semana* anteriorPtr,
                          bool *nivelBajando, bool *humBaja, bool *tempAlta,
                          bool *presAltaEstable, bool *luzAlta) {
  *nivelBajando = false;
  if (anteriorPtr != NULL && anteriorPtr->valida) {
    *nivelBajando = (actual.nivel - anteriorPtr->nivel) > UMBRAL_NIVEL_BAJANDO_CM;
  }
  *humBaja  = (actual.humedad < UMBRAL_HUMEDAD_BAJA);
  *tempAlta = (actual.temperatura > UMBRAL_TEMP_ALTA);
  float rangoPresion = actual.presionMax - actual.presionMin;
  float presionProm  = (actual.presionMax + actual.presionMin) / 2.0;
  *presAltaEstable = (presionProm > UMBRAL_PRESION_ALTA) && (rangoPresion < PRESION_ESTABLE_RANGO);
  *luzAlta = (actual.luz > UMBRAL_LUZ_ALTA);
  return 0.4 * (*nivelBajando ? 1 : 0) + 0.2 * (*humBaja ? 1 : 0) +
         0.2 * (*tempAlta ? 1 : 0) + 0.1 * (*presAltaEstable ? 1 : 0) +
         0.1 * (*luzAlta ? 1 : 0);
}

void guardarSemanaEnEEPROM(uint8_t i) { EEPROM.put(i * sizeof(Semana), historial[i]); }
void guardarIndiceEnEEPROM() { EEPROM.put(TOTAL_SEMANAS_HISTORIAL * sizeof(Semana), indiceSemanaActual); }
void cargarHistorialDeEEPROM() {
  for (uint8_t i = 0; i < TOTAL_SEMANAS_HISTORIAL; i++) {
    EEPROM.get(i * sizeof(Semana), historial[i]);
    if (historial[i].valida != 1) historial[i].valida = 0;
  }
  EEPROM.get(TOTAL_SEMANAS_HISTORIAL * sizeof(Semana), indiceSemanaActual);
  if (indiceSemanaActual >= TOTAL_SEMANAS_HISTORIAL) indiceSemanaActual = 0;
}
void acumularMuestraSemana() {
  if (bmeOK) {
    accTemp += temperatura; accHum += humedad;
    if (presion < presMinSemana) presMinSemana = presion;
    if (presion > presMaxSemana) presMaxSemana = presion;
  }
  if (ajOK)  accNivel += distancia;
  if (tcsOK) accLuz   += luz;
  contMuestrasSemana++;
}
void evaluarSequiaLargoPlazo();
void cerrarSemana() {
  if (contMuestrasSemana == 0) return;
  Semana nueva;
  nueva.nivel = accNivel / contMuestrasSemana;
  nueva.temperatura = accTemp / contMuestrasSemana;
  nueva.humedad = accHum / contMuestrasSemana;
  nueva.presionMin = presMinSemana;
  nueva.presionMax = presMaxSemana;
  nueva.luz = accLuz / contMuestrasSemana;
  nueva.valida = 1;
  historial[indiceSemanaActual] = nueva;
  guardarSemanaEnEEPROM(indiceSemanaActual);
  indiceSemanaActual = (indiceSemanaActual + 1) % TOTAL_SEMANAS_HISTORIAL;
  guardarIndiceEnEEPROM();
  accNivel = 0; accTemp = 0; accHum = 0; accLuz = 0;
  presMinSemana = 99999; presMaxSemana = -99999;
  contMuestrasSemana = 0;
  evaluarSequiaLargoPlazo();
}

void evaluarSequiaLargoPlazo() {
  uint8_t consecutivas = 0, maxConsecutivas = 0;
  Semana anteriorVal; Semana* anteriorPtr = NULL;
  for (uint8_t k = 0; k < TOTAL_SEMANAS_HISTORIAL; k++) {
    uint8_t idx = (indiceSemanaActual + k) % TOTAL_SEMANAS_HISTORIAL;
    Semana actual = historial[idx];
    if (!actual.valida) { anteriorPtr = NULL; consecutivas = 0; continue; }
    bool nb, hb, ta, pae, la;
    float score = calcularScoreSemana(actual, anteriorPtr, &nb, &hb, &ta, &pae, &la);
    if (score > UMBRAL_SCORE_SEQUIA) {
      consecutivas++;
      if (consecutivas > maxConsecutivas) maxConsecutivas = consecutivas;
    } else consecutivas = 0;
    anteriorVal = actual; anteriorPtr = &anteriorVal;
  }
  semanasConsecutivasAltoRiesgo = maxConsecutivas;
  alertaSequiaLargoPlazo = (maxConsecutivas >= SEMANAS_CONSECUTIVAS_ALERTA);
}

void iniciarSimulacionAuto() {
  autoCantidad = 0; scoreAuto = 0; estadoAuto = -1;
  autoTomandoMuestra = false; tUltimaMuestraAuto = millis();
}
void recalcularScoreAuto() {
  if (autoCantidad < 2) { scoreAuto = 0; estadoAuto = -1; return; }
  uint8_t ultimo = autoCantidad - 1;
  bool nivelBajando = (autoNivel[ultimo] - autoNivel[0]) > UMBRAL_NIVEL_BAJANDO_CM;
  float sumaHum = 0, sumaTemp = 0, sumaPres = 0; long sumaLuz = 0;
  float presMinS = autoPres[0], presMaxS = autoPres[0];
  for (uint8_t i = 0; i < autoCantidad; i++) {
    sumaHum += autoHum[i]; sumaTemp += autoTemp[i]; sumaPres += autoPres[i]; sumaLuz += autoLuz[i];
    if (autoPres[i] < presMinS) presMinS = autoPres[i];
    if (autoPres[i] > presMaxS) presMaxS = autoPres[i];
  }
  float humProm = sumaHum / autoCantidad, tempProm = sumaTemp / autoCantidad;
  float presProm = sumaPres / autoCantidad; long luzProm = sumaLuz / autoCantidad;
  bool humBaja = humProm < UMBRAL_HUMEDAD_BAJA;
  bool tempAlta = tempProm > UMBRAL_TEMP_ALTA;
  bool presAltaEstable = (presProm > UMBRAL_PRESION_ALTA) && ((presMaxS - presMinS) < PRESION_ESTABLE_RANGO);
  bool luzAlta = luzProm > UMBRAL_LUZ_ALTA;
  scoreAuto = 0.4 * (nivelBajando ? 1 : 0) + 0.2 * (humBaja ? 1 : 0) +
              0.2 * (tempAlta ? 1 : 0) + 0.1 * (presAltaEstable ? 1 : 0) + 0.1 * (luzAlta ? 1 : 0);
  if (scoreAuto >= UMBRAL_SCORE_SEQUIA) estadoAuto = 2;
  else if (scoreAuto >= UMBRAL_SCORE_ALERTA) estadoAuto = 1;
  else estadoAuto = 0;
}
void tomarMuestraAuto() {
  if (autoCantidad == VENTANA_AUTO) {
    for (uint8_t i = 0; i < VENTANA_AUTO - 1; i++) {
      autoNivel[i] = autoNivel[i + 1]; autoTemp[i] = autoTemp[i + 1];
      autoHum[i] = autoHum[i + 1]; autoPres[i] = autoPres[i + 1]; autoLuz[i] = autoLuz[i + 1];
    }
    autoCantidad = VENTANA_AUTO - 1;
  }
  autoNivel[autoCantidad] = distancia; autoTemp[autoCantidad] = temperatura;
  autoHum[autoCantidad] = humedad; autoPres[autoCantidad] = presion; autoLuz[autoCantidad] = luz;
  autoCantidad++;
  recalcularScoreAuto();
}
void mostrarSimulacionAuto() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(F("Auto ")); lcd.print(autoCantidad);
  lcd.print(F("/")); lcd.print(VENTANA_AUTO);
  if (autoTomandoMuestra) lcd.print(F(" *"));
  lcd.setCursor(0, 1);
  if (estadoAuto < 0) lcd.print(F("Recolectando..."));
  else {
    lcd.print(F("Sc:")); lcd.print(scoreAuto * 100, 0); lcd.print(F("% "));
    if (estadoAuto == 2) lcd.print(F("ALTO"));
    else if (estadoAuto == 1) lcd.print(F("MEDIO"));
    else lcd.print(F("BAJO"));
  }
}

enum FaseSimMan : uint8_t { SIMM_ESPERA, SIMM_MIDIENDO, SIMM_PAUSA, SIMM_RESULTADO };
FaseSimMan faseSimMan = SIMM_ESPERA;
uint8_t simIndice = 0;
unsigned long simManT0 = 0;
int8_t simLedResult = -1;
float nivelSim[NUM_MUESTRAS_SIMULACION];
float tempSim[NUM_MUESTRAS_SIMULACION];
float humSim[NUM_MUESTRAS_SIMULACION];
float presSim[NUM_MUESTRAS_SIMULACION];
long luzSim[NUM_MUESTRAS_SIMULACION];

void mostrarEsperaSim() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Dato "));
  lcd.print(simIndice + 1);
  lcd.print(F("/4 -Pulsa P1"));
  lcd.setCursor(0, 1);
  lcd.print(F("para tomarlo"));
}

void iniciarSimManual() {
  simIndice = 0;
  simLedResult = -1;
  faseSimMan = SIMM_ESPERA;
  nivel = NIVEL_SIM_MANUAL;
  mostrarEsperaSim();
}

void publicarResultadoSim(float scoreSim) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Score:"));
  lcd.print(scoreSim * 100, 0);
  lcd.print(F("%"));
  lcd.setCursor(0, 1);
  if (scoreSim >= UMBRAL_SCORE_SEQUIA) {
    lcd.print(F("RIESGO ALTO"));
    simLedResult = 2;
    escribirLedRojo(true); digitalWrite(LED_AMARILLO, LOW); digitalWrite(LED_VERDE, LOW);
  } else if (scoreSim >= UMBRAL_SCORE_ALERTA) {
    lcd.print(F("ALERTA MEDIA"));
    simLedResult = 1;
    digitalWrite(LED_AMARILLO, HIGH); escribirLedRojo(false); digitalWrite(LED_VERDE, LOW);
  } else {
    lcd.print(F("RIESGO BAJO"));
    simLedResult = 0;
    digitalWrite(LED_VERDE, HIGH); digitalWrite(LED_AMARILLO, LOW); escribirLedRojo(false);
  }
  faseSimMan = SIMM_RESULTADO;
  simManT0 = millis();
}

void cerrarCalculoSim() {
  uint8_t ultimo = NUM_MUESTRAS_SIMULACION - 1;
  bool nivelBajando = (nivelSim[ultimo] - nivelSim[0]) > UMBRAL_NIVEL_BAJANDO_CM;
  float sumaHum = 0, sumaTemp = 0, sumaPres = 0;
  long sumaLuz = 0;
  float presMinS = presSim[0], presMaxS = presSim[0];
  for (uint8_t i = 0; i < NUM_MUESTRAS_SIMULACION; i++) {
    sumaHum += humSim[i];
    sumaTemp += tempSim[i];
    sumaPres += presSim[i];
    sumaLuz += luzSim[i];
    if (presSim[i] < presMinS) presMinS = presSim[i];
    if (presSim[i] > presMaxS) presMaxS = presSim[i];
  }
  float humProm = sumaHum / NUM_MUESTRAS_SIMULACION;
  float tempProm = sumaTemp / NUM_MUESTRAS_SIMULACION;
  float presProm = sumaPres / NUM_MUESTRAS_SIMULACION;
  long luzProm = sumaLuz / NUM_MUESTRAS_SIMULACION;
  bool humBaja = humProm < UMBRAL_HUMEDAD_BAJA;
  bool tempAlta = tempProm > UMBRAL_TEMP_ALTA;
  bool presAltaEstable = (presProm > UMBRAL_PRESION_ALTA) && ((presMaxS - presMinS) < PRESION_ESTABLE_RANGO);
  bool luzAlta = luzProm > UMBRAL_LUZ_ALTA;
  float scoreSim = 0.4f * (nivelBajando ? 1 : 0) + 0.2f * (humBaja ? 1 : 0) +
                   0.2f * (tempAlta ? 1 : 0) + 0.1f * (presAltaEstable ? 1 : 0) + 0.1f * (luzAlta ? 1 : 0);
  publicarResultadoSim(scoreSim);
}

void gestionarSimManual() {
  if (nivel != NIVEL_SIM_MANUAL) return;
  if (faseSimMan == SIMM_MIDIENDO) {
    avanzarDistancia();
    avanzarLuz();
    if (distFase == DIST_LISTA && luzLista) {
      publicarDistanciaLista();
      luz = luzResultado;
      tcsOK = (luz > 0);
      luzLista = false;
      sensorDueno = 0;
      luzDueno = 0;
      nivelSim[simIndice] = distancia;
      tempSim[simIndice] = temperatura;
      humSim[simIndice] = humedad;
      presSim[simIndice] = presion;
      luzSim[simIndice] = luz;
      simIndice++;
      if (simIndice >= NUM_MUESTRAS_SIMULACION) cerrarCalculoSim();
      else {
        faseSimMan = SIMM_PAUSA;
        simManT0 = millis();
      }
    } else if (millis() - simManT0 > 800UL) {
      soltarSensores();
      nivelSim[simIndice] = -1;
      tempSim[simIndice] = temperatura;
      humSim[simIndice] = humedad;
      presSim[simIndice] = presion;
      luzSim[simIndice] = -1;
      simIndice++;
      if (simIndice >= NUM_MUESTRAS_SIMULACION) cerrarCalculoSim();
      else {
        faseSimMan = SIMM_PAUSA;
        simManT0 = millis();
      }
    }
    return;
  }
  if (faseSimMan == SIMM_PAUSA && millis() - simManT0 >= 600UL) {
    faseSimMan = SIMM_ESPERA;
    mostrarEsperaSim();
    return;
  }
  if (faseSimMan == SIMM_RESULTADO && millis() - simManT0 >= 4000UL) {
    simLedResult = -1;
    nivel = NIVEL_MENU_PRINCIPAL;
    mostrarMenuPrincipal();
  }
}

void mostrarRotacion() {
  lcd.clear();
  switch (pantallaActual) {
    case 0:
      lcd.setCursor(0, 0); lcd.print(F("Temp:")); lcd.print(temperatura, 1); lcd.print((char)223); lcd.print(F("C"));
      lcd.setCursor(0, 1); lcd.print(F("Hum:")); lcd.print(humedad, 1); lcd.print(F("%")); break;
    case 1:
      lcd.setCursor(0, 0); lcd.print(F("Presion:")); lcd.setCursor(0, 1); lcd.print(presion, 1); lcd.print(F(" hPa")); break;
    case 2:
      lcd.setCursor(0, 0); lcd.print(F("Distancia:")); lcd.setCursor(0, 1);
      if (ajOK) { lcd.print(distancia, 2); lcd.print(F(" cm")); } else lcd.print(F("AJ SIN LECTURA")); break;
    case 3:
      lcd.setCursor(0, 0); lcd.print(F("Indice de luz:")); lcd.setCursor(0, 1);
      if (tcsOK) lcd.print(luz); else lcd.print(F("TCS SIN LECTURA")); break;
  }
}
void mostrarMenuPrincipal() {
  lcd.clear(); lcd.setCursor(0, 0); lcd.print(F("Menu principal"));
  lcd.setCursor(0, 1); lcd.print(leerOpcionMenu(opcionMenuPrincipal));
}
void mostrarMenuVariables() {
  lcd.clear(); lcd.setCursor(0, 0); lcd.print(F("Elegir variable"));
  lcd.setCursor(0, 1); lcd.print(F(">")); lcd.print(leerNombreVariable(variableSeleccionada));
}
void mostrarVariableUnica() {
  lcd.clear(); lcd.setCursor(0, 0); lcd.print(leerNombreVariable(variableSeleccionada)); lcd.setCursor(0, 1);
  switch (variableSeleccionada) {
    case 0: lcd.print(temperatura, 1); lcd.print((char)223); lcd.print(F("C")); break;
    case 1: lcd.print(humedad, 1); lcd.print(F(" %")); break;
    case 2: lcd.print(presion, 1); lcd.print(F(" hPa")); break;
    case 3: if (ajOK) { lcd.print(distancia, 2); lcd.print(F(" cm")); } else lcd.print(F("SIN LECTURA")); break;
    case 4: if (tcsOK) lcd.print(luz); else lcd.print(F("SIN LECTURA")); break;
  }
}
void mostrarConfirmacion() {
  lcd.clear(); lcd.setCursor(0, 0); lcd.print(F("Guardado como"));
  lcd.setCursor(0, 1); lcd.print(F("punto de inicio"));
}
void mostrarEvaporacion() {
  lcd.clear();
  if (paginaEvaporacion == 0) {
    float probEvap = calcularProbabilidadEvaporacion(); float luxAprox = aproximarLux(luz);
    lcd.setCursor(0, 0); lcd.print(F("Evap: ")); lcd.print(probEvap, 0); lcd.print(F("%"));
    lcd.setCursor(0, 1); lcd.print(F("Luz: "));
    if (tcsOK) { lcd.print(luxAprox, 0); lcd.print(F(" lm")); } else lcd.print(F("SIN LECTURA"));
  } else {
    float vpd = calcularVPD(temperatura, humedad);
    lcd.setCursor(0, 0); lcd.print(F("VPD: ")); lcd.print(vpd, 2); lcd.print(F(" kPa"));
    lcd.setCursor(0, 1);
    if (vpd >= VPD_UMBRAL_MUY_ALTO_KPA) lcd.print(F("MUY ALTA"));
    else if (vpd >= VPD_UMBRAL_ALTO_KPA) lcd.print(F("ALTA"));
    else lcd.print(F("Normal"));
  }
}
void mostrarHistorial() {
  lcd.clear();
  uint8_t idxReal = (indiceSemanaActual + semanaHistorialMostrada) % TOTAL_SEMANAS_HISTORIAL;
  Semana s = historial[idxReal];
  lcd.setCursor(0, 0); lcd.print(F("Sem ")); lcd.print(semanaHistorialMostrada + 1); lcd.print(F("/4 "));
  if (!s.valida) { lcd.print(F("(vacia)")); return; }
  switch (paginaHistorial) {
    case 0: lcd.print(F("p1")); lcd.setCursor(0, 1); lcd.print(F("N:")); lcd.print(s.nivel, 1); lcd.print(F(" T:")); lcd.print(s.temperatura, 1); break;
    case 1: lcd.print(F("p2")); lcd.setCursor(0, 1); lcd.print(F("H:")); lcd.print(s.humedad, 0); lcd.print(F(" L:")); lcd.print(s.luz); break;
    case 2: {
      lcd.print(F("p3")); lcd.setCursor(0, 1);
      Semana anterior; Semana* anteriorPtr = NULL;
      if (semanaHistorialMostrada > 0) {
        uint8_t idxAnt = (indiceSemanaActual + semanaHistorialMostrada - 1) % TOTAL_SEMANAS_HISTORIAL;
        anterior = historial[idxAnt]; if (anterior.valida) anteriorPtr = &anterior;
      }
      bool nb, hb, ta, pae, la;
      float score = calcularScoreSemana(s, anteriorPtr, &nb, &hb, &ta, &pae, &la);
      lcd.print(F("Sc:")); lcd.print(score * 100, 0); lcd.print(F("%")); break;
    }
  }
}

void probarLeds() {
  digitalWrite(LED_VERDE, HIGH); delay(400); digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_AMARILLO, HIGH); delay(400); digitalWrite(LED_AMARILLO, LOW);
  digitalWrite(LED_ROJO, HIGH); delay(400); digitalWrite(LED_ROJO, LOW);
}
void guardarPuntoDePartida() {
  temperaturaBase = temperatura; humedadBase = humedad; presionBase = presion;
  distanciaBase = distancia; luzBase = luz; hayBaseGuardada = true;
}

void accionPulsador1Corto() {
  switch (nivel) {
    case NIVEL_ROTACION: nivel = NIVEL_MENU_PRINCIPAL; opcionMenuPrincipal = 0; mostrarMenuPrincipal(); break;
    case NIVEL_MENU_PRINCIPAL: opcionMenuPrincipal = (opcionMenuPrincipal + 1) % TOTAL_OPCIONES_MENU; mostrarMenuPrincipal(); break;
    case NIVEL_MENU_VARIABLES: variableSeleccionada = (variableSeleccionada + 1) % TOTAL_VARIABLES; mostrarMenuVariables(); break;
    case NIVEL_HISTORIAL:
      paginaHistorial++;
      if (paginaHistorial > 2) { paginaHistorial = 0; semanaHistorialMostrada = (semanaHistorialMostrada + 1) % TOTAL_SEMANAS_HISTORIAL; }
      mostrarHistorial(); break;
    case NIVEL_EVAPORACION: paginaEvaporacion = (paginaEvaporacion + 1) % 2; mostrarEvaporacion(); break;
    case NIVEL_SIM_AUTO: iniciarSimulacionAuto(); mostrarSimulacionAuto(); break;
    case NIVEL_SIM_MANUAL:
      if (faseSimMan == SIMM_ESPERA && sensorDueno == 0 && luzDueno == 0) {
        faseSimMan = SIMM_MIDIENDO;
        simManT0 = millis();
        sensorDueno = 3;
        luzDueno = 3;
        if (bmeOK) {
          temperatura = bme.readTemperature();
          humedad = bme.readHumidity();
          presion = bme.readPressure() / 100.0F;
        }
        iniciarRafagaDistancia();
        pedirLuz();
        lcd.setCursor(0, 1);
        lcd.print(F("Tomando dato... "));
      }
      break;
    default: break;
  }
}
void accionPulsador1Largo() {
  switch (nivel) {
    case NIVEL_ROTACION: nivel = NIVEL_MENU_PRINCIPAL; opcionMenuPrincipal = 0; mostrarMenuPrincipal(); break;
    case NIVEL_MENU_PRINCIPAL:
      if (opcionMenuPrincipal == 0) { nivel = NIVEL_MENU_VARIABLES; variableSeleccionada = 0; mostrarMenuVariables(); }
      else if (opcionMenuPrincipal == 1) { guardarPuntoDePartida(); nivel = NIVEL_CONFIRMACION; tInicioConfirmacion = millis(); mostrarConfirmacion(); }
      else if (opcionMenuPrincipal == 2) { nivel = NIVEL_HISTORIAL; semanaHistorialMostrada = 0; paginaHistorial = 0; mostrarHistorial(); }
      else if (opcionMenuPrincipal == 3) { iniciarSimManual(); }
      else if (opcionMenuPrincipal == 4) { nivel = NIVEL_SIM_AUTO; iniciarSimulacionAuto(); mostrarSimulacionAuto(); }
      else if (opcionMenuPrincipal == 5) { nivel = NIVEL_EVAPORACION; paginaEvaporacion = 0; mostrarEvaporacion(); }
      else if (opcionMenuPrincipal == 6) { borrarCredencialesWifi(); iniciarPortalAP(); nivel = NIVEL_ROTACION; tUltimaPantalla = millis(); mostrarRotacion(); }
      else { nivel = NIVEL_ROTACION; tUltimaPantalla = millis(); mostrarRotacion(); }
      break;
    case NIVEL_MENU_VARIABLES: nivel = NIVEL_MOSTRANDO_VARIABLE; mostrarVariableUnica(); break;
    default: break;
  }
}
void accionPulsador2() {
  switch (nivel) {
    case NIVEL_MENU_PRINCIPAL: nivel = NIVEL_ROTACION; tUltimaPantalla = millis(); mostrarRotacion(); break;
    case NIVEL_MENU_VARIABLES: nivel = NIVEL_MENU_PRINCIPAL; mostrarMenuPrincipal(); break;
    case NIVEL_MOSTRANDO_VARIABLE: nivel = NIVEL_MENU_VARIABLES; mostrarMenuVariables(); break;
    case NIVEL_CONFIRMACION: case NIVEL_HISTORIAL: case NIVEL_EVAPORACION:
      nivel = NIVEL_MENU_PRINCIPAL; mostrarMenuPrincipal(); break;
    case NIVEL_SIM_AUTO: estadoAuto = -1; nivel = NIVEL_MENU_PRINCIPAL; mostrarMenuPrincipal(); break;
    case NIVEL_SIM_MANUAL:
      soltarSensores();
      simLedResult = -1;
      nivel = NIVEL_MENU_PRINCIPAL;
      mostrarMenuPrincipal();
      break;
    default: break;
  }
}
const uint8_t BTN_Q = 8;
volatile EvBtn qBtn1[BTN_Q];
volatile EvBtn qBtn2[BTN_Q];
volatile uint8_t q1h = 0, q1t = 0, q2h = 0, q2t = 0;

void encolarBtn(volatile EvBtn *q, volatile uint8_t *cabeza, volatile uint8_t *cola, uint8_t nivel) {
  uint8_t h = *cabeza;
  uint8_t siguiente = (h + 1) & (BTN_Q - 1);
  if (siguiente == *cola) return;
  q[h].nivel = nivel;
  q[h].tick = timerOk ? tickMs : millis();
  *cabeza = siguiente;
}

void isrBtn1() { encolarBtn(qBtn1, &q1h, &q1t, digitalRead(BTN1) == LOW ? 0 : 1); }
void isrBtn2() { encolarBtn(qBtn2, &q2h, &q2t, digitalRead(BTN2) == LOW ? 0 : 1); }

void aplicarBordeBtn1(uint8_t nivel, uint32_t tick) {
  bool down = nivel == 0;
  if (tick - tBtn1CambioRaw < DEBOUNCE_MS) return;
  tBtn1CambioRaw = tick;
  if (down == btn1Estable) return;
  btn1RawAnterior = down;
  if (down) {
    btn1Estable = true;
    tBtn1Inicio = tick;
    btn1LargoDisparado = false;
    return;
  }
  btn1Estable = false;
  if (!btn1LargoDisparado && (tick - tBtn1Inicio >= UMBRAL_MANTENIDO_MS)) {
    btn1LargoDisparado = true;
    accionPulsador1Largo();
  } else if (!btn1LargoDisparado) {
    accionPulsador1Corto();
  }
}

void aplicarBordeBtn2(uint8_t nivel, uint32_t tick) {
  bool down = nivel == 0;
  if (tick - tBtn2CambioRaw < DEBOUNCE_MS) return;
  tBtn2CambioRaw = tick;
  if (down == btn2Estable) return;
  btn2Estable = down;
  btn2RawAnterior = down;
  if (down) {
    tBtn2Inicio = tick;
        accionPulsador2();
      }
    }

void leerPulsadores() {
  while (q1t != q1h) {
    uint8_t nivelEv;
    uint32_t tickEv;
    noInterrupts();
    nivelEv = qBtn1[q1t].nivel;
    tickEv = qBtn1[q1t].tick;
    q1t = (q1t + 1) & (BTN_Q - 1);
    interrupts();
    aplicarBordeBtn1(nivelEv, tickEv);
  }
  while (q2t != q2h) {
    uint8_t nivelEv;
    uint32_t tickEv;
    noInterrupts();
    nivelEv = qBtn2[q2t].nivel;
    tickEv = qBtn2[q2t].tick;
    q2t = (q2t + 1) & (BTN_Q - 1);
    interrupts();
    aplicarBordeBtn2(nivelEv, tickEv);
  }
  uint32_t ahora = relojMs();
  if (btn1Estable && !btn1LargoDisparado && (ahora - tBtn1Inicio >= UMBRAL_MANTENIDO_MS)) {
    btn1LargoDisparado = true;
    accionPulsador1Largo();
  }
  if (btn1Estable && btn2Estable) {
    unsigned long t1 = ahora - tBtn1Inicio;
    unsigned long t2 = ahora - tBtn2Inicio;
    unsigned long tHold = t1 < t2 ? t1 : t2;
    if (!wifiResetDisparado && tHold >= 4000UL) {
      wifiResetDisparado = true;
      borrarCredencialesWifi();
      iniciarPortalAP();
      nivel = NIVEL_ROTACION;
      tUltimaPantalla = millis();
      mostrarRotacion();
    }
  } else {
    wifiResetDisparado = false;
  }
}

const char* textoRiesgo(int8_t estado) {
  if (estado == 2) return "ALTO"; if (estado == 1) return "MEDIO"; if (estado == 0) return "BAJO"; return "SIN BASE";
}
const char* textoAuto(int8_t estado) {
  if (estado == 2) return "ALTO"; if (estado == 1) return "MEDIO"; if (estado == 0) return "BAJO"; return "RECOLECTANDO";
}
uint32_t epochActualUTC() {
  if (!horaSincronizada) return 0;
  return epochBaseUTC + ((millis() - millisBaseEpoch) / 1000UL);
}

uint8_t ntpFase = 0;
unsigned long ntpEsperaDesde = 0;
uint8_t ntpReintentos = 0;

void aplicarEpoch(uint32_t epoch) {
  epochBaseUTC = epoch;
  millisBaseEpoch = millis();
  horaSincronizada = true;
  tUltimaSyncHora = millis();
  ntpReintentos = 0;
  ntpFase = 0;
}

void enviarPaqueteNtp() {
  if (!udpIniciado) { Udp.begin(PUERTO_NTP_LOCAL); udpIniciado = true; }
  IPAddress timeServer(162, 159, 200, 123);
  byte packetBuffer[48];
  memset(packetBuffer, 0, sizeof(packetBuffer));
  packetBuffer[0] = 0b11100011; packetBuffer[1] = 0; packetBuffer[2] = 6; packetBuffer[3] = 0xEC;
  packetBuffer[12] = 49; packetBuffer[13] = 0x4E; packetBuffer[14] = 49; packetBuffer[15] = 52;
  while (Udp.parsePacket()) { while (Udp.available()) Udp.read(); }
  Udp.beginPacket(timeServer, 123);
  Udp.write(packetBuffer, sizeof(packetBuffer));
  Udp.endPacket();
  ntpFase = 1;
  ntpEsperaDesde = millis();
}

void arrancarNtp() {
  if (modoPortal || WiFi.status() != WL_CONNECTED || ntpFase != 0) return;
  uint32_t epoch = WiFi.getTime();
  if (epoch >= 1700000000UL) { aplicarEpoch(epoch); return; }
  enviarPaqueteNtp();
}

void avanzarNtp() {
  if (ntpFase != 1) return;
    int tam = Udp.parsePacket();
  if (tam >= 48) {
    byte packetBuffer[48];
    Udp.read(packetBuffer, 48);
      unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
      unsigned long secsSince1900 = (highWord << 16) | lowWord;
      const unsigned long SETENTA_ANOS = 2208988800UL;
    ntpFase = 0;
    if (secsSince1900 > SETENTA_ANOS) {
      uint32_t epoch = secsSince1900 - SETENTA_ANOS;
      if (epoch >= 1700000000UL) { aplicarEpoch(epoch); return; }
    }
  } else if (millis() - ntpEsperaDesde < 500UL) {
    return;
  } else {
    ntpFase = 0;
  }
  ntpReintentos++;
  if (ntpReintentos < 3) enviarPaqueteNtp();
  else ntpReintentos = 0;
}

bool intervaloAutoValido(uint16_t minutos) {
  return minutos == 0 || minutos == 15 || minutos == 60 || minutos == 360;
}
int direccionRegistroWeb(uint16_t indice) {
  return EEPROM_HIST_WEB_DATOS + ((int)indice * (int)sizeof(RegistroWeb));
}
void guardarMetaWeb() { EEPROM.put(EEPROM_HIST_WEB_META, metaWeb); }
void reiniciarMetaWeb() {
  metaWeb.magic = HIST_WEB_MAGIC; metaWeb.version = HIST_WEB_VERSION;
  metaWeb.siguiente = 0; metaWeb.cantidad = 0; metaWeb.intervaloAutoMin = 360;
  guardarMetaWeb();
}
void inicializarHistoricoWeb() {
  int ultimoByte = EEPROM_HIST_WEB_DATOS + ((int)MAX_REGISTROS_WEB * (int)sizeof(RegistroWeb));
  if (ultimoByte > EEPROM.length()) { historicoWebDisponible = false; return; }
  EEPROM.get(EEPROM_HIST_WEB_META, metaWeb);
  bool invalida = metaWeb.magic != HIST_WEB_MAGIC || metaWeb.version != HIST_WEB_VERSION ||
    metaWeb.siguiente >= MAX_REGISTROS_WEB || metaWeb.cantidad > MAX_REGISTROS_WEB ||
    !intervaloAutoValido(metaWeb.intervaloAutoMin);
  if (invalida) reiniciarMetaWeb();
  tUltimoGuardadoAutoWeb = millis();
}
bool guardarRegistroWeb(bool manualWeb) {
  if (!historicoWebDisponible) return false;
  bool absoluta = horaSincronizada && epochActualUTC() >= 1700000000UL;
  uint32_t marca = absoluta ? epochActualUTC() : (millis() / 1000UL);
  RegistroWeb r;
  r.epochUTC = marca; r.temperatura = temperatura; r.humedad = humedad; r.presion = presion;
  r.distancia = distancia; r.luz = (int32_t)luz; r.vpd = calcularVPD(temperatura, humedad);
  r.evaporacion = calcularProbabilidadEvaporacion(); r.score = scoreRiesgo * 100.0; r.flags = 0;
  if (bmeOK) r.flags |= 0x01; if (ajOK) r.flags |= 0x02; if (tcsOK) r.flags |= 0x04; if (manualWeb) r.flags |= 0x08;
  if (!absoluta) r.flags |= 0x10;
  r.reservado[0] = r.reservado[1] = r.reservado[2] = 0;
  EEPROM.put(direccionRegistroWeb(metaWeb.siguiente), r);
  metaWeb.siguiente = (metaWeb.siguiente + 1) % MAX_REGISTROS_WEB;
  if (metaWeb.cantidad < MAX_REGISTROS_WEB) metaWeb.cantidad++;
  guardarMetaWeb();
  return true;
}
void gestionarHistoricoAutomatico() {
  if (!historicoWebDisponible || metaWeb.intervaloAutoMin == 0) return;
  unsigned long intervalo = (unsigned long)metaWeb.intervaloAutoMin * 60UL * 1000UL;
  unsigned long ahora = millis();
  if (ahora - tUltimoGuardadoAutoWeb >= intervalo) { tUltimoGuardadoAutoWeb = ahora; guardarRegistroWeb(false); }
}

void enviarCabecerasCors(WiFiClient &client, const char* contentType) {
  client.println("HTTP/1.1 200 OK");
  client.print("Content-Type: "); client.println(contentType);
  client.println("Cache-Control: no-cache, no-store, must-revalidate");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Access-Control-Allow-Methods: GET, OPTIONS");
  client.println("Connection: close");
  client.println();
}
void responderTexto(WiFiClient &client, const char* texto) {
  enviarCabecerasCors(client, "text/plain; charset=utf-8");
  client.println(texto);
}
void responderResultadoMedicion(WiFiClient &client, bool guardada) {
  enviarCabecerasCors(client, "application/json; charset=utf-8");
  client.print("{\"ok\":"); client.print(guardada ? "true" : "false");
  client.print(",\"timeOK\":"); client.print(horaSincronizada ? "true" : "false");
  client.print(",\"count\":"); client.print(historicoWebDisponible ? metaWeb.cantidad : 0);
  client.println("}");
}

void enviarHistoricoJSON(WiFiClient &client) {
  enviarCabecerasCors(client, "application/json; charset=utf-8");
  client.print("{\"count\":"); client.print(historicoWebDisponible ? metaWeb.cantidad : 0);
  client.print(",\"max\":"); client.print(MAX_REGISTROS_WEB);
  client.print(",\"autoMin\":"); client.print(historicoWebDisponible ? metaWeb.intervaloAutoMin : 0);
  client.print(",\"timeOK\":"); client.print(horaSincronizada ? "true" : "false");
  client.print(",\"uptime\":"); client.print(millis() / 1000UL);
  client.print(",\"records\":[");
  if (historicoWebDisponible && metaWeb.cantidad > 0) {
    uint16_t primero = (metaWeb.siguiente + MAX_REGISTROS_WEB - metaWeb.cantidad) % MAX_REGISTROS_WEB;
    for (uint16_t i = 0; i < metaWeb.cantidad; i++) {
      uint16_t idx = (primero + i) % MAX_REGISTROS_WEB;
      RegistroWeb r; EEPROM.get(direccionRegistroWeb(idx), r);
      if (i > 0) client.print(',');
      client.print("{\"t\":"); client.print(r.epochUTC);
      client.print(",\"temp\":"); client.print(r.temperatura, 2);
      client.print(",\"hum\":"); client.print(r.humedad, 2);
      client.print(",\"pres\":"); client.print(r.presion, 2);
      client.print(",\"dist\":"); client.print(r.distancia, 2);
      client.print(",\"luz\":"); client.print(r.luz);
      client.print(",\"vpd\":"); client.print(r.vpd, 3);
      client.print(",\"evap\":"); client.print(r.evaporacion, 1);
      client.print(",\"score\":"); client.print(r.score, 1);
      client.print(",\"manual\":"); client.print((r.flags & 0x08) ? "true" : "false");
      client.print(",\"rel\":"); client.print((r.flags & 0x10) ? "true" : "false");
      client.print('}');
    }
  }
  client.println("]}");
}

bool comprobarInternet() {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClient prueba;
  prueba.setConnectionTimeout(800);
  if (!prueba.connect("example.com", 80)) { prueba.stop(); return false; }
  prueba.println("GET / HTTP/1.1"); prueba.println("Host: example.com");
  prueba.println("Connection: close"); prueba.println(); delay(20); prueba.stop();
  return true;
}

uint8_t crcWifi(const WifiCreds &c) {
  uint8_t crc = 0;
  const uint8_t *p = (const uint8_t *)&c;
  size_t n = sizeof(WifiCreds) - sizeof(c.crc);
  for (size_t i = 0; i < n; i++) crc ^= p[i];
  return crc;
}

int direccionWifiEEPROM() {
  int addr = (int)EEPROM.length() - (int)sizeof(WifiCreds);
  return addr < 0 ? 0 : addr;
}

void construirNombreAP() {
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  snprintf(apNombre, sizeof(apNombre), "AERIS-%02X%02X", mac[4], mac[5]);
}

void urlDecode(const String &src, char *dest, size_t destMax) {
  size_t o = 0;
  for (unsigned i = 0; i < src.length() && o + 1 < destMax; i++) {
    char c = src.charAt(i);
    if (c == '+') dest[o++] = ' ';
    else if (c == '%' && i + 2 < src.length()) {
      char h[3] = { (char)src.charAt(i + 1), (char)src.charAt(i + 2), 0 };
      dest[o++] = (char)strtol(h, NULL, 16);
      i += 2;
    } else dest[o++] = c;
  }
  dest[o] = 0;
}

bool extraerParametro(const String &req, const char *clave, char *dest, size_t destMax) {
  dest[0] = 0;
  int finLinea = req.indexOf('\r');
  String linea = finLinea > 0 ? req.substring(0, finLinea) : req;
  String token = String(clave) + "=";
  int q = linea.indexOf('?');
  if (q < 0) return false;
  int start = linea.indexOf(token, q);
  if (start < 0) return false;
  start += token.length();
  int amp = linea.indexOf('&', start);
  int sp = linea.indexOf(' ', start);
  int end = linea.length();
  if (amp >= 0) end = amp;
  if (sp >= 0 && sp < end) end = sp;
  urlDecode(linea.substring(start, end), dest, destMax);
  return dest[0] != 0;
}

void guardarCredencialesWifi(const char *nuevoSsid, const char *nuevoPass) {
  WifiCreds c;
  memset(&c, 0, sizeof(c));
  c.magic = WIFI_CREDS_MAGIC;
  c.version = WIFI_CREDS_VERSION;
  strncpy(c.ssid, nuevoSsid, sizeof(c.ssid) - 1);
  strncpy(c.pass, nuevoPass, sizeof(c.pass) - 1);
  c.crc = crcWifi(c);
  EEPROM.put(direccionWifiEEPROM(), c);
  strncpy(ssid, c.ssid, sizeof(ssid) - 1);
  strncpy(pass, c.pass, sizeof(pass) - 1);
  ssid[sizeof(ssid) - 1] = 0;
  pass[sizeof(pass) - 1] = 0;
  credencialesWifiOK = (ssid[0] != 0);
}

void borrarCredencialesWifi() {
  WifiCreds c;
  memset(&c, 0, sizeof(c));
  EEPROM.put(direccionWifiEEPROM(), c);
  ssid[0] = 0;
  pass[0] = 0;
  credencialesWifiOK = false;
  Serial.println(F("WiFi: credenciales borradas"));
}

bool cargarCredencialesWifi() {
  WifiCreds c;
  EEPROM.get(direccionWifiEEPROM(), c);
  if (c.magic != WIFI_CREDS_MAGIC || c.version != WIFI_CREDS_VERSION) return false;
  if (c.crc != crcWifi(c) || c.ssid[0] == 0) return false;
  strncpy(ssid, c.ssid, sizeof(ssid) - 1);
  strncpy(pass, c.pass, sizeof(pass) - 1);
  ssid[sizeof(ssid) - 1] = 0;
  pass[sizeof(pass) - 1] = 0;
  credencialesWifiOK = true;
  return true;
}

void detenerDnsPortal() {
  if (dnsPortalIniciado) {
    dnsUdp.stop();
    dnsPortalIniciado = false;
  }
}

void atenderCaptiveDNS() {
  if (!dnsPortalIniciado) return;
  int n = dnsUdp.parsePacket();
  if (n < 12) return;
  uint8_t buf[512];
  if (n > 512) n = 512;
  dnsUdp.read(buf, n);
  buf[2] = 0x84;
  buf[3] = 0x00;
  buf[6] = 0x00;
  buf[7] = 0x01;
  buf[8] = 0x00; buf[9] = 0x00;
  buf[10] = 0x00; buf[11] = 0x00;
  int idx = n;
  if (idx + 16 > 512) return;
  buf[idx++] = 0xC0; buf[idx++] = 0x0C;
  buf[idx++] = 0x00; buf[idx++] = 0x01;
  buf[idx++] = 0x00; buf[idx++] = 0x01;
  buf[idx++] = 0x00; buf[idx++] = 0x00; buf[idx++] = 0x00; buf[idx++] = 0x1E;
  buf[idx++] = 0x00; buf[idx++] = 0x04;
  IPAddress ip = WiFi.localIP();
  buf[idx++] = ip[0]; buf[idx++] = ip[1]; buf[idx++] = ip[2]; buf[idx++] = ip[3];
  dnsUdp.beginPacket(dnsUdp.remoteIP(), dnsUdp.remotePort());
  dnsUdp.write(buf, idx);
  dnsUdp.endPacket();
}

bool esperarSTA(unsigned long timeoutMs) {
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < timeoutMs) delay(400);
  if (WiFi.status() != WL_CONNECTED) return false;
  unsigned long esperaIP = millis();
  while (WiFi.localIP() == IPAddress(0, 0, 0, 0) && millis() - esperaIP < 8000UL) delay(200);
  return WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

void activarServidorSTA() {
  modoPortal = false;
  detenerDnsPortal();
  if (!udpIniciado) { Udp.begin(PUERTO_NTP_LOCAL); udpIniciado = true; }
  server.begin();
  servidorWebIniciado = true;
  internetOK = comprobarInternet();
  arrancarNtp();
  fallosStaSeguidos = 0;
  Serial.print(F("WiFi STA IP "));
  Serial.println(WiFi.localIP());
}

void iniciarPortalAP() {
  internetOK = false;
  horaSincronizada = false;
  construirNombreAP();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(F("Portal WiFi"));
  lcd.setCursor(0, 1); lcd.print(apNombre);
  Serial.print(F("WiFi AP "));
  Serial.println(apNombre);
  WiFi.disconnect();
  delay(200);
  WiFi.beginAP(apNombre);
  delay(300);
  dnsUdp.begin(53);
  dnsPortalIniciado = true;
  server.begin();
  servidorWebIniciado = true;
  modoPortal = true;
  fallosStaSeguidos = 0;
  tUltimoIntentoWiFi = millis();
}

bool conectarSTA(unsigned long timeoutMs) {
  if (!credencialesWifiOK || ssid[0] == 0) return false;
  modoPortal = false;
  detenerDnsPortal();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(F("Conectando WiFi"));
  lcd.setCursor(0, 1); lcd.print(ssid);
  Serial.print(F("WiFi STA "));
  Serial.println(ssid);
  WiFi.disconnect();
  delay(200);
  if (pass[0] == 0) WiFi.begin(ssid);
  else WiFi.begin(ssid, pass);
  if (!esperarSTA(timeoutMs)) {
    servidorWebIniciado = false;
    internetOK = false;
    return false;
  }
  activarServidorSTA();
  return true;
}

void iniciarWiFi() {
  construirNombreAP();
  cargarCredencialesWifi();
  tUltimoIntentoWiFi = millis();
  tUltimaPruebaInternet = millis();
  if (!credencialesWifiOK) {
    iniciarPortalAP();
    return;
  }
  if (conectarSTA(20000UL)) return;
  if (conectarSTA(12000UL)) return;
  iniciarPortalAP();
}

void mostrarPortalLCD() {
  static uint8_t paginaPortal = 0;
  lcd.clear();
  if (paginaPortal == 0) {
    lcd.setCursor(0, 0); lcd.print(F("Config WiFi"));
    lcd.setCursor(0, 1); lcd.print(apNombre);
  } else {
    lcd.setCursor(0, 0); lcd.print(F("Abra el AP y"));
    lcd.setCursor(0, 1); lcd.print(WiFi.localIP());
  }
  paginaPortal = (paginaPortal + 1) % 2;
}

void mostrarEstadoArranqueLCD() {
  if (modoPortal) {
    matrix.renderBitmap(MATRIZ_APAGADA, 8, 12); palomitaMatriz = false; corazonMatriz = false;
    mostrarPortalLCD();
    return;
  }
  bool wifiOK = (WiFi.status() == WL_CONNECTED) && (WiFi.localIP() != IPAddress(0,0,0,0));
  bool sensoresOK = bmeOK && ajOK && tcsOK;
  bool sistemaOK = wifiOK && sensoresOK && servidorWebIniciado;
  lcd.clear();
  if (sistemaOK) {
    matrix.renderBitmap(PALOMITA_OK, 8, 12); palomitaMatriz = true; corazonMatriz = false;
    lcd.setCursor(0, 0); lcd.print(F("Sistema OK"));
    lcd.setCursor(0, 1); lcd.print(WiFi.localIP());
  } else {
    matrix.renderBitmap(MATRIZ_APAGADA, 8, 12); palomitaMatriz = false; corazonMatriz = false;
    lcd.setCursor(0, 0); lcd.print(F("REVISAR SISTEMA")); lcd.setCursor(0, 1);
    if (!wifiOK) lcd.print(F("WiFi / IP"));
    else if (!bmeOK) lcd.print(F("BME280"));
    else if (!ajOK) lcd.print(F("AJ-SR04M"));
    else if (!tcsOK) lcd.print(F("TCS230"));
    else lcd.print(F("Servidor web"));
  }
}

void gestionarWiFi() {
  unsigned long ahora = millis();
  if (modoPortal) {
    atenderCaptiveDNS();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    internetOK = false;
    if (ahora - tUltimoIntentoWiFi >= INTERVALO_REINTENTO_WIFI_MS) {
      tUltimoIntentoWiFi = ahora;
      fallosStaSeguidos++;
      if (credencialesWifiOK) {
        if (pass[0] == 0) WiFi.begin(ssid);
        else WiFi.begin(ssid, pass);
      }
      if (fallosStaSeguidos >= MAX_FALLOS_STA_A_PORTAL) iniciarPortalAP();
    }
    return;
  }
  fallosStaSeguidos = 0;
  if (!servidorWebIniciado) { server.begin(); servidorWebIniciado = true; }
  if (!httpHayCliente() && ahora - tUltimaPruebaInternet >= INTERVALO_INTERNET_MS) {
    tUltimaPruebaInternet = ahora; internetOK = comprobarInternet();
  }
  avanzarNtp();
  if (ntpFase == 0 && (!horaSincronizada || ahora - tUltimaSyncHora >= INTERVALO_SYNC_HORA_MS)) {
    if (millis() - ntpEsperaDesde >= 2000UL) arrancarNtp();
  }
}

void enviarPaginaPortal(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=utf-8");
  client.println("Cache-Control: no-store");
  client.println("Connection: close");
  client.println();
  client.print(F("<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"));
  client.print(F("<title>AERIS WiFi</title><style>body{font-family:sans-serif;background:#f7f8fa;margin:0;padding:24px;color:#17202a}"));
  client.print(F(".c{background:#fff;border-radius:16px;padding:20px;max-width:420px;margin:auto}input,button{width:100%;padding:12px;margin:8px 0;font-size:16px;border-radius:10px;border:1px solid #e5e9ed;box-sizing:border-box}"));
  client.print(F("button{background:#1d5c4f;color:#fff;border:0;font-weight:700}</style></head><body><div class='c'><h1>AERIS · WiFi</h1>"));
  client.print(F("<p>El dashboard ya esta en este Arduino. <a href='/'>Volver al panel</a>. Este formulario solo sirve para unirlo despues a otra WiFi.</p>"));
  client.print(F("<form action='/save' method='get'><label>SSID</label><input name='s' maxlength='32' required>"));
  client.print(F("<label>Contrasena</label><input name='p' type='password' maxlength='64'>"));
  client.print(F("<button>Guardar y conectar</button></form><p><a href='/scan'>Ver redes cercanas</a> · <a href='/'>Panel</a></p></div></body></html>"));
}

void enviarPaginaScan(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=utf-8");
  client.println("Connection: close");
  client.println();
  client.print(F("<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Redes</title></head><body>"));
  client.print(F("<h1>Redes WiFi</h1><p>Toque una red y luego escriba la clave en el formulario.</p><ul>"));
  int n = WiFi.scanNetworks();
  if (n < 1) client.print(F("<li>No se detectaron redes. Escriba el SSID a mano.</li>"));
  for (int i = 0; i < n; i++) {
    client.print(F("<li>"));
    client.print(WiFi.SSID(i));
    client.print(F(" ("));
    client.print(WiFi.RSSI(i));
    client.print(F(" dBm)</li>"));
  }
  client.print(F("</ul><p><a href='/'>Volver</a></p></body></html>"));
}

void aplicarCredencialesDesdePortal(const char *nuevoSsid, const char *nuevoPass) {
  guardarCredencialesWifi(nuevoSsid, nuevoPass);
  if (conectarSTA(20000UL)) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(F("WiFi OK"));
    lcd.setCursor(0, 1); lcd.print(WiFi.localIP());
    delay(1500);
  } else {
    iniciarPortalAP();
  }
}

void imprimirJsonTelemetria(Print &out) {
  float vpd = calcularVPD(temperatura, humedad);
  float probEvap = calcularProbabilidadEvaporacion();
  float luxAprox = aproximarLux(luz);
  out.print('{');
  out.print("\"wifi\":\""); out.print(modoPortal ? "Portal AP" : (WiFi.status() == WL_CONNECTED ? "Conectado" : "Desconectado")); out.print("\",");
  out.print("\"modoWifi\":\""); out.print(modoPortal ? "AP" : "STA"); out.print("\",");
  out.print("\"ssid\":\""); out.print(modoPortal ? apNombre : ssid); out.print("\",");
  out.print("\"internet\":\""); out.print(internetOK ? "Conectado" : "Sin acceso"); out.print("\",");
  out.print("\"ip\":\""); out.print(WiFi.localIP()); out.print("\",");
  out.print("\"rssi\":"); out.print(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0); out.print(',');
  out.print("\"bmeOK\":"); out.print(bmeOK ? "true" : "false"); out.print(',');
  out.print("\"ajOK\":"); out.print(ajOK ? "true" : "false"); out.print(',');
  out.print("\"tcsOK\":"); out.print(tcsOK ? "true" : "false"); out.print(',');
  out.print("\"temperatura\":"); out.print(temperatura, 2); out.print(',');
  out.print("\"humedad\":"); out.print(humedad, 2); out.print(',');
  out.print("\"presion\":"); out.print(presion, 2); out.print(',');
  out.print("\"distancia\":"); out.print(distancia, 2); out.print(',');
  out.print("\"luz\":"); out.print(luz); out.print(',');
  out.print("\"lux\":"); out.print(luxAprox, 1); out.print(',');
  out.print("\"vpd\":"); out.print(vpd, 3); out.print(',');
  out.print("\"evaporacion\":"); out.print(probEvap, 1); out.print(',');
  out.print("\"scoreRiesgo\":"); out.print(scoreRiesgo * 100.0, 1); out.print(',');
  out.print("\"riesgo\":\""); out.print(textoRiesgo(estadoRiesgo)); out.print("\",");
  out.print("\"alarmaRoja\":"); out.print(alarmaRojaActiva ? "true" : "false"); out.print(',');
  out.print("\"baseGuardada\":"); out.print(hayBaseGuardada ? "true" : "false"); out.print(',');
  out.print("\"alertaLargoPlazo\":"); out.print(alertaSequiaLargoPlazo ? "true" : "false"); out.print(',');
  out.print("\"semanasAltoRiesgo\":"); out.print(semanasConsecutivasAltoRiesgo); out.print(',');
  out.print("\"autoCantidad\":"); out.print(autoCantidad); out.print(',');
  out.print("\"scoreAuto\":"); out.print(scoreAuto * 100.0, 1); out.print(',');
  out.print("\"estadoAuto\":\""); out.print(textoAuto(estadoAuto)); out.print("\",");
  out.print("\"matriz\":\"");
  if (corazonMatriz) out.print("Corazon"); else if (palomitaMatriz) out.print("Sistema OK"); else out.print("Apagada");
  out.print("\",");
  out.print("\"historyCount\":"); out.print(historicoWebDisponible ? metaWeb.cantidad : 0); out.print(',');
  out.print("\"historyAutoMin\":"); out.print(historicoWebDisponible ? metaWeb.intervaloAutoMin : 0); out.print(',');
  out.print("\"timeOK\":"); out.print(horaSincronizada ? "true" : "false"); out.print(',');
  out.print("\"epoch\":"); out.print(epochActualUTC()); out.print(',');
  out.print("\"uptime\":"); out.print(millis() / 1000UL);
  out.print('}');
}

void enviarDatosJSON(WiFiClient &client) {
  enviarCabecerasCors(client, "application/json; charset=utf-8");
  imprimirJsonTelemetria(client);
  client.println();
}

const uint8_t HTTP_N = 2;
const uint8_t HF_LIBRE = 0;
const uint8_t HF_LEE = 1;
const uint8_t HF_HIST = 3;
const uint8_t HF_PAGINA = 4;
const uint8_t HF_MIDE = 5;

WiFiClient httpCli[HTTP_N];
String httpReq[HTTP_N];
uint8_t httpFase[HTTP_N] = {HF_LIBRE, HF_LIBRE};
uint8_t httpMideTipo[HTTP_N] = {0, 0};
bool httpMideArr[HTTP_N] = {false, false};
bool httpHdr[HTTP_N] = {false, false};
uint16_t httpHistI[HTTP_N] = {0, 0};
uint16_t httpHist0[HTTP_N] = {0, 0};
size_t httpOff[HTTP_N] = {0, 0};
unsigned long httpT0[HTTP_N] = {0, 0};

bool httpHayCliente() {
  for (uint8_t i = 0; i < HTTP_N; i++) if (httpFase[i] != HF_LIBRE) return true;
  return false;
}

void cerrarHttp(uint8_t i) {
  if (httpCli[i]) httpCli[i].stop();
  httpFase[i] = HF_LIBRE;
  httpReq[i] = "";
  httpMideArr[i] = false;
  httpHdr[i] = false;
  httpMideTipo[i] = 0;
  httpOff[i] = 0;
}

void imprimirRegistroHistorico(WiFiClient &client, const RegistroWeb &r, bool coma) {
  if (coma) client.print(',');
  client.print("{\"t\":"); client.print(r.epochUTC);
  client.print(",\"temp\":"); client.print(r.temperatura, 2);
  client.print(",\"hum\":"); client.print(r.humedad, 2);
  client.print(",\"pres\":"); client.print(r.presion, 2);
  client.print(",\"dist\":"); client.print(r.distancia, 2);
  client.print(",\"luz\":"); client.print(r.luz);
  client.print(",\"vpd\":"); client.print(r.vpd, 3);
  client.print(",\"evap\":"); client.print(r.evaporacion, 1);
  client.print(",\"score\":"); client.print(r.score, 1);
  client.print(",\"manual\":"); client.print((r.flags & 0x08) ? "true" : "false");
  client.print(",\"rel\":"); client.print((r.flags & 0x10) ? "true" : "false");
  client.print('}');
}

void pasoHistorico(uint8_t i) {
  WiFiClient &client = httpCli[i];
  if (!httpHdr[i]) {
    enviarCabecerasCors(client, "application/json; charset=utf-8");
    client.print("{\"count\":"); client.print(historicoWebDisponible ? metaWeb.cantidad : 0);
    client.print(",\"max\":"); client.print(MAX_REGISTROS_WEB);
    client.print(",\"autoMin\":"); client.print(historicoWebDisponible ? metaWeb.intervaloAutoMin : 0);
    client.print(",\"timeOK\":"); client.print(horaSincronizada ? "true" : "false");
    client.print(",\"uptime\":"); client.print(millis() / 1000UL);
    client.print(",\"records\":[");
    httpHdr[i] = true;
    httpHistI[i] = 0;
    if (historicoWebDisponible && metaWeb.cantidad > 0) {
      httpHist0[i] = (metaWeb.siguiente + MAX_REGISTROS_WEB - metaWeb.cantidad) % MAX_REGISTROS_WEB;
    }
    if (!historicoWebDisponible || metaWeb.cantidad == 0) {
      client.println("]}");
      cerrarHttp(i);
    }
    return;
  }
  uint8_t n = 0;
  while (n < 4 && httpHistI[i] < metaWeb.cantidad) {
    uint16_t idx = (httpHist0[i] + httpHistI[i]) % MAX_REGISTROS_WEB;
    RegistroWeb r;
    EEPROM.get(direccionRegistroWeb(idx), r);
    imprimirRegistroHistorico(client, r, httpHistI[i] > 0);
    httpHistI[i]++;
    n++;
  }
  if (httpHistI[i] >= metaWeb.cantidad) {
    client.println("]}");
    cerrarHttp(i);
  }
}

void pasoPagina(uint8_t i) {
  WiFiClient &client = httpCli[i];
  if (!httpHdr[i]) {
    client.println(F("HTTP/1.1 200 OK"));
    client.println(F("Content-Type: text/html; charset=utf-8"));
    client.println(F("Cache-Control: no-store"));
    client.println(F("Connection: close"));
    client.println();
    httpHdr[i] = true;
    httpOff[i] = 0;
    return;
  }
  const size_t total = sizeof(AERIS_PAGE) - 1;
  if (httpOff[i] >= total) {
    client.flush();
    cerrarHttp(i);
    return;
  }
  size_t n = total - httpOff[i];
  if (n > 480) n = 480;
  client.write((const uint8_t *)AERIS_PAGE + httpOff[i], n);
  httpOff[i] += n;
}

void pasoMide(uint8_t i) {
  if (!httpMideArr[i]) {
    if (sensorDueno != 0 || luzDueno != 0 || distFase == DIST_ECO || distFase == DIST_PAUSA || luzMidiendo) {
      if (millis() - httpT0[i] > 3000UL) {
        soltarSensores();
        responderResultadoMedicion(httpCli[i], false);
        cerrarHttp(i);
      }
      return;
    }
    sensorDueno = 2;
    luzDueno = 2;
    if (bmeOK) {
      temperatura = bme.readTemperature();
      humedad = bme.readHumidity();
      presion = bme.readPressure() / 100.0F;
    }
    iniciarRafagaDistancia();
    pedirLuz();
    httpMideArr[i] = true;
    httpT0[i] = millis();
    return;
  }
  avanzarDistancia();
  avanzarLuz();
  if (distFase == DIST_LISTA && luzLista) {
    publicarDistanciaLista();
    luz = luzResultado;
    tcsOK = (luz > 0);
    luzLista = false;
    sensorDueno = 0;
    luzDueno = 0;
    evaluarRiesgo();
    actualizarLeds();
    if (httpMideTipo[i] == 2) {
      guardarPuntoDePartida();
      responderTexto(httpCli[i], "LINEA BASE GUARDADA");
    } else {
      responderResultadoMedicion(httpCli[i], guardarRegistroWeb(true));
    }
    cerrarHttp(i);
    return;
  }
  if (millis() - httpT0[i] > 800UL) {
    soltarSensores();
    responderResultadoMedicion(httpCli[i], false);
    cerrarHttp(i);
  }
}

void despacharHttp(uint8_t i) {
  String &request = httpReq[i];
  WiFiClient &client = httpCli[i];
  if (request.indexOf("OPTIONS") >= 0) {
    client.println("HTTP/1.1 204 No Content");
    client.println("Access-Control-Allow-Origin: *");
    client.println("Access-Control-Allow-Methods: GET, OPTIONS");
    client.println("Connection: close");
    client.println();
    cerrarHttp(i);
    return;
  }
  if (!modoPortal && WiFi.status() != WL_CONNECTED) {
    cerrarHttp(i);
    return;
  }
  if (request.indexOf("GET /data") >= 0) {
    enviarDatosJSON(client);
    cerrarHttp(i);
    return;
  }
  if (request.indexOf("GET /history") >= 0) {
    httpFase[i] = HF_HIST;
    httpHdr[i] = false;
    return;
  }
  if (request.indexOf("GET /measure") >= 0) {
    httpFase[i] = HF_MIDE;
    httpMideTipo[i] = 1;
    httpMideArr[i] = false;
    httpT0[i] = millis();
    return;
  }
  if (request.indexOf("GET /savebase") >= 0) {
    httpFase[i] = HF_MIDE;
    httpMideTipo[i] = 2;
    httpMideArr[i] = false;
    httpT0[i] = millis();
    return;
  }
  if (request.indexOf("GET /setinterval?m=") >= 0) {
    int p = request.indexOf("GET /setinterval?m=") + 19;
    int fin = request.indexOf('&', p);
    if (fin < 0) fin = request.indexOf(' ', p);
    uint16_t minutos = 0;
    if (fin > p) minutos = (uint16_t)request.substring(p, fin).toInt();
    if (intervaloAutoValido(minutos)) {
      metaWeb.intervaloAutoMin = minutos;
      guardarMetaWeb();
      tUltimoGuardadoAutoWeb = millis();
      responderTexto(client, "INTERVALO ACTUALIZADO");
    } else responderTexto(client, "INTERVALO NO VALIDO");
    cerrarHttp(i);
    return;
  }
  if (request.indexOf("GET /heart") >= 0) {
    matrix.renderBitmap(CORAZON, 8, 12);
    corazonMatriz = true;
    palomitaMatriz = false;
    responderTexto(client, "CORAZON ENCENDIDO");
    cerrarHttp(i);
    return;
  }
  if (request.indexOf("GET /matrixoff") >= 0) {
    matrix.renderBitmap(MATRIZ_APAGADA, 8, 12);
    corazonMatriz = false;
    palomitaMatriz = false;
    responderTexto(client, "MATRIZ APAGADA");
    cerrarHttp(i);
    return;
  }
  if (request.indexOf("GET /alarmoff") >= 0) {
    fijarAlarmaRoja(false);
    responderTexto(client, "ALARMA APAGADA");
    cerrarHttp(i);
    return;
  }
  if (request.indexOf("GET /alarmon") >= 0) {
    fijarAlarmaRoja(true);
    responderTexto(client, "ALARMA ACTIVADA");
    cerrarHttp(i);
    return;
  }
  if (modoPortal && request.indexOf("GET /scan") >= 0) {
    enviarPaginaScan(client);
    cerrarHttp(i);
    return;
  }
  if (modoPortal && request.indexOf("GET /save?") >= 0) {
      char nuevoSsid[33] = "";
      char nuevoPass[65] = "";
      extraerParametro(request, "s", nuevoSsid, sizeof(nuevoSsid));
      extraerParametro(request, "p", nuevoPass, sizeof(nuevoPass));
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/html; charset=utf-8");
      client.println("Connection: close");
      client.println();
      if (nuevoSsid[0] == 0) {
        client.print(F("<p>SSID vacio. <a href='/'>Volver</a></p>"));
      cerrarHttp(i);
      return;
      }
      client.print(F("<p>Guardado. Conectando a "));
      client.print(nuevoSsid);
      client.print(F("...</p>"));
    client.flush();
    cerrarHttp(i);
      aplicarCredencialesDesdePortal(nuevoSsid, nuevoPass);
      return;
  }
  if (!modoPortal && request.indexOf("GET /wifi/portal") >= 0) {
    responderTexto(client, "ABRIENDO PORTAL WIFI");
    client.flush();
    cerrarHttp(i);
    iniciarPortalAP();
    return;
  }
  if (request.indexOf("GET /wifi") >= 0) {
      enviarPaginaPortal(client);
    cerrarHttp(i);
    return;
  }
  httpFase[i] = HF_PAGINA;
  httpHdr[i] = false;
  httpOff[i] = 0;
}

void aceptarHttp() {
  for (uint8_t n = 0; n < HTTP_N; n++) {
    bool hueco = false;
    for (uint8_t i = 0; i < HTTP_N; i++) if (httpFase[i] == HF_LIBRE) hueco = true;
    if (!hueco) return;
    WiFiClient nuevo = server.available();
    if (!nuevo) return;
    bool repetido = false;
    for (uint8_t i = 0; i < HTTP_N; i++) {
      if (httpFase[i] != HF_LIBRE && httpCli[i] == nuevo) repetido = true;
    }
    if (repetido) return;
    for (uint8_t i = 0; i < HTTP_N; i++) {
      if (httpFase[i] == HF_LIBRE) {
        httpCli[i] = nuevo;
        httpReq[i] = "";
        httpReq[i].reserve(400);
        httpFase[i] = HF_LEE;
        httpT0[i] = millis();
        httpHdr[i] = false;
        httpMideArr[i] = false;
        break;
      }
    }
  }
}

void leerHttp(uint8_t i) {
  while (httpCli[i].available() && httpReq[i].length() < 1200) {
    char c = httpCli[i].read();
    httpReq[i] += c;
    if (httpReq[i].endsWith("\r\n\r\n")) break;
  }
  if (httpReq[i].endsWith("\r\n\r\n") || httpReq[i].length() > 1200) {
    despacharHttp(i);
    return;
  }
  if (millis() - httpT0[i] > 1500UL) cerrarHttp(i);
}

void atenderServidorWeb() {
  if (!servidorWebIniciado) return;
  if (modoPortal) atenderCaptiveDNS();
  aceptarHttp();
  for (uint8_t i = 0; i < HTTP_N; i++) {
    if (httpFase[i] == HF_LEE) leerHttp(i);
    else if (httpFase[i] == HF_HIST) pasoHistorico(i);
    else if (httpFase[i] == HF_PAGINA) pasoPagina(i);
    else if (httpFase[i] == HF_MIDE) pasoMide(i);
  }
}

void setup() {
  Serial.begin(9600); Wire.begin();
  matrix.begin(); matrix.renderBitmap(MATRIZ_APAGADA, 8, 12);
  iniciarRelojTareas();
  lcd.init(); lcd.backlight(); lcd.createChar(0, ICONO_OK);
  lcd.clear(); lcd.setCursor(0, 0); lcd.print(F("Iniciando...")); delay(1000);

  bmeOK = bme.begin(0x76); if (!bmeOK) bmeOK = bme.begin(0x77);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  attachInterrupt(digitalPinToInterrupt(ECHO_PIN), isrEcho, CHANGE);
  delay(800);
  sensorDueno = 1;
  iniciarRafagaDistancia();
  unsigned long tPrueba = millis();
  while (distFase != DIST_LISTA && millis() - tPrueba < 800UL) avanzarDistancia();
  if (distFase == DIST_LISTA) publicarDistanciaLista();
  else { soltarSensores(); ajOK = false; distancia = -1; }
  sensorDueno = 0;

  Serial.println();
  Serial.println(F("=== DIAGNOSTICO AJ-SR04M ==="));
  Serial.println(F("Conexion esperada: VCC->5V, GND->GND, TRIG/RX->D2, ECHO/TX->D3"));
  Serial.println(F("Modo esperado: pulso compatible HC/HR-SR04, R19 abierto"));
  Serial.println(F("Rango util: 20 a 400 cm"));
  if (ajOK) {
    Serial.print(F("AJ-SR04M OK. Distancia inicial: "));
    Serial.print(distancia, 2);
    Serial.println(F(" cm"));
  } else {
    Serial.println(F("AJ-SR04M SIN LECTURA."));
    Serial.println(F("Ponga un objeto plano entre 30 y 200 cm y revise TRIG/ECHO/R19."));
  }
  Serial.println(F("============================"));

  pinMode(TCS_S1, OUTPUT); pinMode(TCS_S2, OUTPUT); pinMode(TCS_S3, OUTPUT); pinMode(TCS_OUT, INPUT);
  digitalWrite(TCS_S1, LOW); digitalWrite(TCS_S2, HIGH); digitalWrite(TCS_S3, LOW);
  delay(200);
  luzDueno = 1;
  pedirLuz();
  tPrueba = millis();
  while (!luzLista && millis() - tPrueba < 80UL) avanzarLuz();
  if (luzLista) { luz = luzResultado; tcsOK = (luz > 0); luzLista = false; }
  else { tcsOK = false; luz = -1; }
  luzDueno = 0;
  luzMidiendo = false;

  pinMode(LED_VERDE, OUTPUT); pinMode(LED_AMARILLO, OUTPUT); pinMode(LED_ROJO, OUTPUT);
  digitalWrite(LED_VERDE, LOW); digitalWrite(LED_AMARILLO, LOW); digitalWrite(LED_ROJO, LOW);
  probarLeds();
  pinMode(BTN1, INPUT_PULLUP); pinMode(BTN2, INPUT_PULLUP);
  delay(40);
  if (digitalRead(BTN1) == LOW && digitalRead(BTN2) == LOW) {
    lcd.clear(); lcd.print(F("Borrar WiFi"));
    borrarCredencialesWifi();
    delay(800);
  }
  attachInterrupt(digitalPinToInterrupt(BTN1), isrBtn1, CHANGE);
  attachInterrupt(digitalPinToInterrupt(BTN2), isrBtn2, CHANGE);

  inicializarHistoricoWeb(); iniciarWiFi();
  cargarHistorialDeEEPROM(); evaluarSequiaLargoPlazo();

  lcd.clear(); lcd.setCursor(0, 0);
  lcd.print(bmeOK ? "BME:OK " : "BME:ERR "); lcd.print(ajOK ? "AJ:OK" : "AJ:ERR");
  lcd.setCursor(0, 1); lcd.print(tcsOK ? "TCS:OK" : "TCS:ERR"); delay(1800);
  mostrarEstadoArranqueLCD(); delay(5000); lcd.clear();

  tUltimaDistancia = millis(); tUltimoAmbiente = millis();
  tUltimaPantalla = millis(); tUltimaSemana = millis();
}

void tareaSensores() {
  unsigned long ahora = millis();
  leerPulsadores();
  gestionarSimManual();

  if (sensorDueno == 0 && distFase == DIST_REPOSO && ahora - tUltimaDistancia >= INTERVALO_DISTANCIA_MS) {
    tUltimaDistancia = ahora;
    sensorDueno = 1;
    iniciarRafagaDistancia();
  }
  if (sensorDueno == 1) {
    avanzarDistancia();
    if (distFase == DIST_LISTA) {
      publicarDistanciaLista();
      sensorDueno = 0;
    }
  }

  if (ahora - tUltimoDebugDistancia >= 1000UL) {
    tUltimoDebugDistancia = ahora;
    if (ajOK) {
      Serial.print(F("AJ-SR04M | cruda="));
      Serial.print(ultimaDistanciaCruda, 2);
      Serial.print(F(" cm | filtrada="));
      Serial.print(distancia, 2);
      Serial.print(F(" cm | eco="));
      Serial.print(ultimaDuracionEcoUs);
      Serial.println(F(" us"));
    } else {
      Serial.print(F("AJ-SR04M | SIN LECTURA | eco="));
      Serial.print(ultimaDuracionEcoUs);
      Serial.println(F(" us | pruebe con objeto a 30-200 cm; revise D2/D3 y R19"));
    }
  }

  if (luzDueno == 0 && ahora - tUltimoAmbiente >= INTERVALO_AMBIENTE_MS) {
    tUltimoAmbiente = ahora;
    if (bmeOK) {
      temperatura = bme.readTemperature();
      humedad = bme.readHumidity();
      presion = bme.readPressure() / 100.0F;
    }
    luzDueno = 1;
    pedirLuz();
  }
  if (luzDueno == 1) {
    avanzarLuz();
    if (luzLista) {
      luz = luzResultado;
      tcsOK = (luz > 0);
      luzLista = false;
      luzDueno = 0;
    acumularMuestraSemana();
  }
  }

  if (ahora - tUltimaSemana >= INTERVALO_SEMANA_MS) { tUltimaSemana = ahora; cerrarSemana(); }
  evaluarRiesgo(); actualizarLeds(); gestionarHistoricoAutomatico();

  switch (nivel) {
    case NIVEL_ROTACION:
      if (ahora - tUltimaPantalla >= INTERVALO_PANTALLA_MS) {
        tUltimaPantalla = ahora;
        if (modoPortal) mostrarPortalLCD();
        else {
          pantallaActual = (pantallaActual + 1) % TOTAL_PANTALLAS; mostrarRotacion();
        }
      }
      break;
    case NIVEL_MOSTRANDO_VARIABLE:
      if (ahora - tUltimoRefrescoVariable >= INTERVALO_REFRESCO_VARIABLE_MS) {
        tUltimoRefrescoVariable = ahora; mostrarVariableUnica();
      }
      break;
    case NIVEL_EVAPORACION:
      if (ahora - tUltimoRefrescoVariable >= INTERVALO_REFRESCO_VARIABLE_MS) {
        tUltimoRefrescoVariable = ahora; mostrarEvaporacion();
      }
      break;
    case NIVEL_SIM_AUTO:
      if (ahora - tUltimaMuestraAuto >= INTERVALO_MUESTRA_AUTO_MS) {
        tUltimaMuestraAuto = ahora; autoTomandoMuestra = true; mostrarSimulacionAuto();
        tomarMuestraAuto(); autoTomandoMuestra = false; mostrarSimulacionAuto();
      }
      break;
    case NIVEL_CONFIRMACION:
      if (ahora - tInicioConfirmacion >= DURACION_CONFIRMACION_MS) {
        nivel = NIVEL_MENU_PRINCIPAL; mostrarMenuPrincipal();
      }
      break;
    default: break;
  }
}

void loop() {
  if (!timerOk) {
    gestionarWiFi();
    atenderServidorWeb();
    tareaSensores();
    return;
  }
  if (pedirRed) { pedirRed = 0; gestionarWiFi(); atenderServidorWeb(); }
  if (pedirSensor) { pedirSensor = 0; tareaSensores(); }
}
