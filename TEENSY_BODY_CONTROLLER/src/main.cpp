#include <Arduino.h>
#include "Codec.h"
#include "body/Pins.h"

// Commissioning-first target: no actuator pin is configured and no motor,
// audio, link or servo output is generated.
void setup() {
    Serial.begin(115200);
    const uint32_t start = millis();
    while (!Serial && millis() - start < 2000) {}
    Serial.println("[BODY] UNCOMMISSIONED");
}

void loop() {
    static uint32_t last = 0;
    if (millis() - last >= 5000) {
        last = millis();
        Serial.println("[BODY] UNCOMMISSIONED");
    }
}
