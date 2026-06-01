#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Scanning for WiFi networks...");
  int n = WiFi.scanNetworks();

  if (n == 0) {
    Serial.println("No networks found — WiFi hardware may be faulty.");
  } else {
    Serial.printf("Found %d networks:\n", n);
    for (int i = 0; i < n; i++) {
      Serial.printf("  %d: %s (RSSI: %d dBm)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    }
    Serial.println("WiFi is working!");
  }
}

void loop() {}
