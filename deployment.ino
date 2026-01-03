/* Deployment Logger (RTC timestamps only, NO WiFi)
   - DS18B20 (OneWire) on GPIO4
   - TDS (ET10901) AOUT -> ADC1 GPIO35
   - pH analog -> ADC1 GPIO32
   - ORP analog -> ADC1 GPIO34
   - SD CS -> GPIO5
*/

#include <OneWire.h>
#include <DallasTemperature.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "time.h"
#include "esp_sleep.h"

// -------------- HARD-CODED START TIME --------------
const int RTC_YEAR  = 2025;
const int RTC_MONTH = 12;
const int RTC_DAY   = 15;
const int RTC_HOUR  = 14;
const int RTC_MIN   = 55;
const int RTC_SEC   = 0;
// ---------------------------------------------------

#define ONE_WIRE_BUS 4
const int TDS_PIN   = 35;
const int PH_PIN    = 32;
const int SD_CS_PIN = 5;
const int ORP_PIN   = 34;

// ADC & sensor constants
const float VREF    = 3.3;
const int   ADC_RES = 4095;

// pH calibration
const float pH_m = -24.0;
const float pH_b = 67.48;

// ORP calibration
const float ORP_VZERO      = 1.8;
const float ORP_CAL_OFFSET = -15.0;

// Timing
const unsigned long SAMPLE_INTERVAL_MS = 10000UL;
const unsigned long ACTIVE_PERIOD_MS   = 10UL * 60UL * 1000UL; 
const unsigned long DEEP_SLEEP_MS      = 5UL * 60UL * 1000UL;
const unsigned long SD_RETRY_SLEEP_MS  = 2UL * 60UL * 1000UL;

// CSV
const char* LOG_FILE = "/week1_test2.CSV";

// OneWire
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Runtime state
unsigned long activeStartMillis = 0;
unsigned long lastSampleMillis  = 0;
int sampleCount = 0;

// -------- Sensor sequencing --------
enum SensorPhase {
  READ_TEMP,
  READ_TDS,
  READ_PH,
  READ_ORP,
  WRITE_LOG
};

SensorPhase currentPhase = READ_TEMP;

// Stored readings
float g_tempC   = NAN;
float g_tds_ppm = NAN;
float g_phVal   = NAN;
float g_orp_mV  = NAN;

// ----------------- RTC helpers -----------------
bool setRTCFromTm(struct tm *tmval) {
  time_t t = mktime(tmval);
  if (t <= 0) return false;
  struct timeval tv;
  tv.tv_sec = t;
  tv.tv_usec = 0;
  settimeofday(&tv, NULL);
  return true;
}

String getDateTimeString() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           timeinfo.tm_year + 1900,
           timeinfo.tm_mon + 1,
           timeinfo.tm_mday,
           timeinfo.tm_hour,
           timeinfo.tm_min,
           timeinfo.tm_sec);
  return String(buf);
}

bool parseDateTimeString(const String &s, struct tm &out_tm) {
  if (s.length() < 19) return false;
  int YYYY = s.substring(0,4).toInt();
  int MM   = s.substring(5,7).toInt();
  int DD   = s.substring(8,10).toInt();
  int hh   = s.substring(11,13).toInt();
  int mm   = s.substring(14,16).toInt();
  int ss   = s.substring(17,19).toInt();
  if (YYYY < 1970 || MM < 1 || MM > 12 || DD < 1 || DD > 31) return false;
  memset(&out_tm, 0, sizeof(out_tm));
  out_tm.tm_year = YYYY - 1900;
  out_tm.tm_mon  = MM - 1;
  out_tm.tm_mday = DD;
  out_tm.tm_hour = hh;
  out_tm.tm_min  = mm;
  out_tm.tm_sec  = ss;
  return true;
}

void initRTCIfNeeded() {
  time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);
  int year = ti.tm_year + 1900;

  if (year < RTC_YEAR) {
    struct tm t = {};
    t.tm_year = RTC_YEAR - 1900;
    t.tm_mon  = RTC_MONTH - 1;
    t.tm_mday = RTC_DAY;
    t.tm_hour = RTC_HOUR;
    t.tm_min  = RTC_MIN;
    t.tm_sec  = RTC_SEC;
    setRTCFromTm(&t);
  }
}

// ----------------- ADC helpers -----------------
void adcSetup() {
  analogReadResolution(12);
  analogSetPinAttenuation((gpio_num_t)TDS_PIN, ADC_11db);
  analogSetPinAttenuation((gpio_num_t)PH_PIN,  ADC_11db);
  analogSetPinAttenuation((gpio_num_t)ORP_PIN, ADC_11db);
}

float readVoltageAvg(int pin, int samples = 10, int delayMs = 5) {
  long sum = 0;
  for (int i = 0; i < samples; ++i) {
    sum += analogRead(pin);
    delay(delayMs);
  }
  return (sum / (float)samples) * VREF / ADC_RES;
}

float readTemperatureC() {
  sensors.requestTemperatures();
  return sensors.getTempCByIndex(0);
}

float computeTDSppm(float voltage, float tempC) {
  double ec = 133.42 * voltage * voltage * voltage
            - 255.86 * voltage * voltage
            + 857.39 * voltage;
  ec = ec / (1 + 0.02 * (tempC - 25.0));
  return ec * 0.5 * 1.7729754359 * 0.8186;
}

float computePH(float voltage) {
  return pH_m * voltage + pH_b;
}

float readORPmV() {
  long sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += analogRead(ORP_PIN);
    delay(10);
  }
  float v = (sum / 20.0) * VREF / ADC_RES;
  return (ORP_VZERO - v) * 1000.0 + ORP_CAL_OFFSET;
}

// ----------------- SD helpers -----------------
bool ensureLogFileHeader() {
  if (!SD.exists(LOG_FILE)) {
    File f = SD.open(LOG_FILE, FILE_WRITE);
    if (!f) return false;
    f.println("DateTime,Temperature_C,TDS_ppm,pH,ORP_mV");
    f.close();
  }
  return true;
}

bool appendLogLine(String line) {
  File f = SD.open(LOG_FILE, FILE_APPEND);
  if (!f) {
    Serial.println("Failed to open log file for append!");
    return false;
  }
  f.println(line);
  f.close();
  return true;
}

// ----------------- Serial commands -----------------
void processSerialCommands() {
  if (!Serial.available()) return;
  String s = Serial.readStringUntil('\n');
  s.trim();
  if (s.length() == 0) return;

  if (s.startsWith("settime ")) {
    String arg = s.substring(8);
    struct tm tmval;
    if (parseDateTimeString(arg, tmval)) {
      if (setRTCFromTm(&tmval)) {
        Serial.println(getDateTimeString());
      }
    }
  } else if (s == "gettime") {
    Serial.println(getDateTimeString());
  } else if (s == "clearfile") {
    if (SD.exists(LOG_FILE)) SD.remove(LOG_FILE);
  }
}

// ----------------- SETUP -----------------
void setup() {
  Serial.begin(115200);
  delay(300);

  adcSetup();
  sensors.begin();
  initRTCIfNeeded();

  if (!SD.begin(SD_CS_PIN)) {
    esp_sleep_enable_timer_wakeup((uint64_t)SD_RETRY_SLEEP_MS * 1000ULL);
    esp_deep_sleep_start();
  }

  ensureLogFileHeader();
  activeStartMillis = millis();
}

// ----------------- LOOP -----------------
void loop() {
  processSerialCommands();
  unsigned long now = millis();

  if (now - activeStartMillis > ACTIVE_PERIOD_MS) {
    SD.end();
    esp_sleep_enable_timer_wakeup((uint64_t)DEEP_SLEEP_MS * 1000ULL);
    esp_deep_sleep_start();
  }

  if (lastSampleMillis == 0 || now - lastSampleMillis >= SAMPLE_INTERVAL_MS) {
    lastSampleMillis = now;

    switch (currentPhase) {

      case READ_TEMP:
        g_tempC = readTemperatureC();
        currentPhase = READ_TDS;
        break;

      case READ_TDS: {
        float t = (g_tempC == -127.0f || isnan(g_tempC)) ? 25.0f : g_tempC;
        float v = readVoltageAvg(TDS_PIN);
        g_tds_ppm = computeTDSppm(v, t);
        currentPhase = READ_PH;
        break;
      }

      case READ_PH: {
        float v = readVoltageAvg(PH_PIN);
        g_phVal = computePH(v);
        currentPhase = READ_ORP;
        break;
      }

      case READ_ORP:
        g_orp_mV = readORPmV();
        currentPhase = WRITE_LOG;
        break;

      case WRITE_LOG: {
  String ts = getDateTimeString();
  char buf[220];
  snprintf(buf, sizeof(buf), "%s,%.2f,%.3f,%.2f,%.2f",
           ts.c_str(), g_tempC, g_tds_ppm, g_phVal, g_orp_mV);

  Serial.print("LOG -> ");
  Serial.println(buf);      // 👈 PRINT TO SERIAL

  appendLogLine(String(buf));  // 👈 WRITE TO SD
  currentPhase = READ_TEMP;
  break;
}

      }
    }
  }
