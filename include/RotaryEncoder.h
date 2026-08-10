#pragma once
#include <Arduino.h>

// Interrupt-driven quadrature rotary encoder + push button, on the GPIOs
// reserved in pins.h. Rotation is read via a hardware interrupt (not
// polled in loop()) because a mechanical encoder generates transitions far
// faster than the main loop's cadence -- polling would miss steps.
class RotaryEncoder {
public:
  static void begin();

  // Call every main loop() iteration. Cheap -- just polls/debounces the
  // button (rotation itself is captured by the ISR independent of this).
  static void loop();

  // Positive = clockwise steps, negative = counter-clockwise, accumulated
  // and consumed (reset to 0) since the last call. One "step" is one
  // physical detent -- most common encoder modules (e.g. KY-040) produce 4
  // raw electrical transitions per detent, which this accounts for
  // internally.
  static int consumeRotationDelta();

  // True exactly once per physical button press (debounced). Consumed on
  // read -- calling this twice in a row without an intervening press
  // returns true once, then false.
  static bool consumeButtonPress();
};
