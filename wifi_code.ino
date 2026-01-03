```/* Deployment Logger (RTC timestamps only, NO WiFi normally)
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
#include <WiFi.h>
#include <HTTPClient.h>

// ---------------- WIFI + FIREBASE ----------------
#define WIFI_SSID     "YOUR_WIFI"
#define WIFI_PASS     "YOUR_PASS"
#define FIREBASE_HOST "your-project-id-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "YOUR_DATABASE_SECRET"

// ---------------- HARD RTC ----------------
const int RTC_YEAR=2025, RTC_MONTH=12, RTC_DAY=15, RTC_HOUR=14, RTC_MIN=55, RTC_SEC=0;

// ---------------- PINS ----------------
#define ONE_WIRE_BUS 4
const int TDS_PIN=35, PH_PIN=32, ORP_PIN=34, SD_CS_PIN=5;

// ---------------- ADC ----------------
const float VREF=3.3;
const int ADC_RES=4095;

// ---------------- CALIB ----------------
const float pH_m=-24.0, pH_b=67.48;
const float ORP_VZERO=1.8, ORP_CAL_OFFSET=-15.0;

// ---------------- TIMING ----------------
const unsigned long SAMPLE_INTERVAL_MS=10000UL;
const unsigned long ACTIVE_PERIOD_MS=10UL*60UL*1000UL;
const unsigned long DEEP_SLEEP_MS=5UL*60UL*1000UL;
const unsigned long SD_RETRY_SLEEP_MS=2UL*60UL*1000UL;

// ---------------- CSV ----------------
const char* LOG_FILE="/week1_test2.CSV";

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

unsigned long activeStartMillis=0,lastSampleMillis=0;

enum SensorPhase{READ_TEMP,READ_TDS,READ_PH,READ_ORP,WRITE_LOG};
SensorPhase currentPhase=READ_TEMP;

float g_tempC=NAN,g_tds_ppm=NAN,g_phVal=NAN,g_orp_mV=NAN;

// ---------------- RTC ----------------
bool setRTCFromTm(struct tm *tmv){
 time_t t=mktime(tmv); if(t<=0)return false;
 struct timeval tv={t,0}; settimeofday(&tv,NULL); return true;
}

String getDateTimeString(){
 time_t now=time(nullptr); struct tm ti; localtime_r(&now,&ti);
 char buf[32];
 snprintf(buf,sizeof(buf),"%04d-%02d-%02d %02d:%02d:%02d",
 ti.tm_year+1900,ti.tm_mon+1,ti.tm_mday,ti.tm_hour,ti.tm_min,ti.tm_sec);
 return String(buf);
}

// -------- REQUIRED FUNCTION ----------
bool parseDateTimeString(const String &s, struct tm &out_tm){
 if(s.length()<19)return false;
 int YYYY=s.substring(0,4).toInt();
 int MM=s.substring(5,7).toInt();
 int DD=s.substring(8,10).toInt();
 int hh=s.substring(11,13).toInt();
 int mm=s.substring(14,16).toInt();
 int ss=s.substring(17,19).toInt();
 if(YYYY<1970||MM<1||MM>12||DD<1||DD>31)return false;
 memset(&out_tm,0,sizeof(out_tm));
 out_tm.tm_year=YYYY-1900;
 out_tm.tm_mon=MM-1;
 out_tm.tm_mday=DD;
 out_tm.tm_hour=hh;
 out_tm.tm_min=mm;
 out_tm.tm_sec=ss;
 return true;
}

void initRTCIfNeeded(){
 struct tm ti; time_t now=time(nullptr); localtime_r(&now,&ti);
 if(ti.tm_year+1900<RTC_YEAR){
  struct tm t={}; t.tm_year=RTC_YEAR-1900; t.tm_mon=RTC_MONTH-1;
  t.tm_mday=RTC_DAY; t.tm_hour=RTC_HOUR; t.tm_min=RTC_MIN; t.tm_sec=RTC_SEC;
  setRTCFromTm(&t);
 }
}

// ---------------- ADC ----------------
void adcSetup(){
 analogReadResolution(12);
 analogSetPinAttenuation((gpio_num_t)TDS_PIN,ADC_11db);
 analogSetPinAttenuation((gpio_num_t)PH_PIN,ADC_11db);
 analogSetPinAttenuation((gpio_num_t)ORP_PIN,ADC_11db);
}

float readVoltageAvg(int pin){
 long sum=0; for(int i=0;i<10;i++){sum+=analogRead(pin);delay(5);}
 return (sum/10.0)*VREF/ADC_RES;
}

float readTemperatureC(){ sensors.requestTemperatures(); return sensors.getTempCByIndex(0); }

float computeTDSppm(float v,float t){
 double ec=133.42*v*v*v-255.86*v*v+857.39*v;
 ec/=1+0.02*(t-25.0); return ec*0.5*1.77*0.8186;
}

float computePH(float v){ return pH_m*v+pH_b; }

float readORPmV(){
 long s=0; for(int i=0;i<20;i++){s+=analogRead(ORP_PIN);delay(10);}
 float v=(s/20.0)*VREF/ADC_RES;
 return (ORP_VZERO-v)*1000.0+ORP_CAL_OFFSET;
}

// ---------------- SD ----------------
bool ensureLogFileHeader(){
 if(!SD.exists(LOG_FILE)){
  File f=SD.open(LOG_FILE,FILE_WRITE);
  if(!f)return false;
  f.println("DateTime,Temperature_C,TDS_ppm,pH,ORP_mV"); f.close();
 }
 return true;
}

bool appendLogLine(String line){
 File f=SD.open(LOG_FILE,FILE_APPEND);
 if(!f)return false;
 f.println(line); f.close(); return true;
}

// ---------------- FIREBASE ----------------
bool uploadToFirebase(String ts,float t,float tds,float ph,float orp){
 WiFi.begin(WIFI_SSID,WIFI_PASS);
 unsigned long st=millis();
 while(WiFi.status()!=WL_CONNECTED && millis()-st<10000)delay(500);
 if(WiFi.status()!=WL_CONNECTED)return false;

 HTTPClient http;
 String url="https://"+String(FIREBASE_HOST)+"/logs.json?auth="+FIREBASE_AUTH;
 String payload="{\"timestamp\":\""+ts+"\",\"temperature\":"+String(t,2)+
                ",\"tds\":"+String(tds,3)+",\"ph\":"+String(ph,2)+
                ",\"orp\":"+String(orp,2)+"}";
 http.begin(url);
 http.addHeader("Content-Type","application/json");
 int code=http.POST(payload);
 http.end(); WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
 return code==200||code==201;
}

// ---------------- SETUP ----------------
void setup(){
 Serial.begin(115200); delay(300);
 adcSetup(); sensors.begin(); initRTCIfNeeded();
 if(!SD.begin(SD_CS_PIN)){
  esp_sleep_enable_timer_wakeup((uint64_t)SD_RETRY_SLEEP_MS*1000ULL);
  esp_deep_sleep_start();
 }
 ensureLogFileHeader();
 activeStartMillis=millis();
}

// ---------------- LOOP ----------------
void loop(){
 unsigned long now=millis();
 if(now-activeStartMillis>ACTIVE_PERIOD_MS){
  SD.end(); esp_sleep_enable_timer_wakeup((uint64_t)DEEP_SLEEP_MS*1000ULL);
  esp_deep_sleep_start();
 }

 if(lastSampleMillis==0||now-lastSampleMillis>=SAMPLE_INTERVAL_MS){
  lastSampleMillis=now;
  switch(currentPhase){
   case READ_TEMP: g_tempC=readTemperatureC(); currentPhase=READ_TDS; break;
   case READ_TDS: g_tds_ppm=computeTDSppm(readVoltageAvg(TDS_PIN),(isnan(g_tempC)?25:g_tempC)); currentPhase=READ_PH; break;
   case READ_PH: g_phVal=computePH(readVoltageAvg(PH_PIN)); currentPhase=READ_ORP; break;
   case READ_ORP: g_orp_mV=readORPmV(); currentPhase=WRITE_LOG; break;
   case WRITE_LOG:{
    String ts=getDateTimeString();
    char buf[220];
    snprintf(buf,sizeof(buf),"%s,%.2f,%.3f,%.2f,%.2f",ts.c_str(),g_tempC,g_tds_ppm,g_phVal,g_orp_mV);
    Serial.println(buf);
    appendLogLine(String(buf));
    uploadToFirebase(ts,g_tempC,g_tds_ppm,g_phVal,g_orp_mV);
    currentPhase=READ_TEMP;
   } break;
  }
 }
}
```