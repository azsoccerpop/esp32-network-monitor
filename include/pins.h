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
//
// CLK/DT were originally on GPIO 32/33, but GPIO 33 proved unreliable on
// this specific board (DT pin never toggled even across two different
// physical encoder modules, isolating the fault to that pin rather than
// the hardware) -- moved to 26/27 instead.
namespace EncoderPins {
constexpr uint8_t kPinA = 26;       // rotary quadrature A (CLK)
constexpr uint8_t kPinB = 27;       // rotary quadrature B (DT)
constexpr uint8_t kPinButton = 25;  // push-button (page select / action)
}  // namespace EncoderPins
