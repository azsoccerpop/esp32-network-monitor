#pragma once

// Reserved GPIOs for hardware not yet wired up. Documented here ahead of
// time so nothing else in the project claims these pins before the rotary
// encoder is physically added.
//
// Already in use elsewhere (do not reuse):
//   GPIO 21 -- I2C SDA (DisplayManager)
//   GPIO 22 -- I2C SCL (DisplayManager)
//
// Avoided deliberately: ESP32 strapping pins (0, 2, 12, 15) -- using these
// for external hardware can interfere with boot mode selection.
namespace EncoderPins {
constexpr uint8_t kPinA = 32;       // rotary quadrature A
constexpr uint8_t kPinB = 33;       // rotary quadrature B
constexpr uint8_t kPinButton = 25;  // push-button (page select / action)
}  // namespace EncoderPins
