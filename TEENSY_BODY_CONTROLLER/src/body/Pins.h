#pragma once

// Teensy 4.1 body-controller pin assignments (design section 3).
namespace body_pins {
const int kLeftVescRx = 0;
const int kLeftVescTx = 1;
const int kRightVescRx = 7;
const int kRightVescTx = 8;
const int kAudioRx = 15;
const int kAudioTx = 14;
const int kDomeLinkRx = 16;
const int kDomeLinkTx = 17;
const int kReceiverRx = 21;
const int kTelemetrySingleWire = 24;
const int kDomeServo = 2;
}  // namespace body_pins
