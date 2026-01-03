/* Retrieval Firmware (WiFi AP + SD download only)
   - NO sensor readings
   - NO deep sleep
   - Just exposes the CSV file over WiFi

   Endpoints:
     /          -> small HTML page with link
     /download  -> streams /total_test_LOG.CSV
*/

#include <WiFi.h>
#include <WebServer.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

// ----------------- USER CONFIG -----------------
const char* WIFI_SSID = "esp32";
const char* WIFI_PASS = "12345678";
// -----------------------------------------------

const int   SD_CS_PIN = 5;
const char* LOG_FILE  = "/noel.CSV";

WebServer server(80);
IPAddress localIP;

// ----------------- SD helpers -----------------
bool initSD() {
  Serial.print("Initializing SD on CS ");
  Serial.println(SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD init failed!");
    return false;
  }
  Serial.println("SD initialized.");

  if (!SD.exists(LOG_FILE)) {
    Serial.println("Log file does not exist yet.");
  } else {
    Serial.println("Log file found.");
  }
  return true;
}

// ----------------- Web handlers -----------------
void handleRoot() {
  String page = "<html><body><h3>ESP32 Pond Logger - Data Download</h3>";
  page += "<p>CSV file: " + String(LOG_FILE) + "</p>";
  page += "<p><a href=\"/download\">Download log</a></p>";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void handleDownload() {
  if (!SD.exists(LOG_FILE)) {
    server.send(404, "text/plain", "Log file not found");
    return;
  }
  File f = SD.open(LOG_FILE, FILE_READ);
  if (!f) {
    server.send(500, "text/plain", "Failed to open log file");
    return;
  }
  server.streamFile(f, "text/csv");
  f.close();
}

// ----------------- Setup & Loop -----------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== ESP32 Pond Logger - Retrieval Firmware ===");

  initSD();

  Serial.print("Starting WiFi AP: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_AP);
  if (WiFi.softAP(WIFI_SSID, WIFI_PASS)) {
    localIP = WiFi.softAPIP();
    Serial.print("AP started, IP: ");
    Serial.println(localIP);

    server.on("/", handleRoot);
    server.on("/download", handleDownload);
    server.begin();
    Serial.println("HTTP server started. Open http://"
                   + localIP.toString() + "/ in browser.");
  } else {
    Serial.println("Failed to start AP!");
  }
}

void loop() {
  server.handleClient();
  delay(10);
}
