#include "RotaryEncoder.h"
#include "pins.h"

namespace {

// Most common cheap modules (KY-040 and similar) produce 4 raw A/B edge
// transitions per physical detent/click. If your encoder feels like it
// takes 2 clicks per page, or skips every other click, adjust this.
constexpr int32_t kCountsPerDetent = 4;

constexpr uint32_t kButtonDebounceMs = 200;

volatile int32_t s_rawCounter = 0;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

int32_t s_carry = 0;  // leftover sub-detent raw counts between reads

bool s_lastButtonReading = HIGH;   // INPUT_PULLUP -- HIGH = not pressed
uint32_t s_lastButtonChangeMs = 0;
bool s_buttonPressPending = false;

void IRAM_ATTR onEncoderAChanged() {
  const bool a = digitalRead(EncoderPins::kPinA);
  const bool b = digitalRead(EncoderPins::kPinB);
  portENTER_CRITICAL_ISR(&s_mux);
  if (a == b) {
    ++s_rawCounter;
  } else {
    --s_rawCounter;
  }
  portEXIT_CRITICAL_ISR(&s_mux);
}

}  // namespace

void RotaryEncoder::begin() {
  pinMode(EncoderPins::kPinA, INPUT_PULLUP);
  pinMode(EncoderPins::kPinB, INPUT_PULLUP);
  pinMode(EncoderPins::kPinButton, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(EncoderPins::kPinA), onEncoderAChanged, CHANGE);

  s_lastButtonReading = digitalRead(EncoderPins::kPinButton);
}

void RotaryEncoder::loop() {
  // Button: polled here (not interrupt-driven) -- fine, since a physical
  // press lasts far longer than the main loop's ~100ms cadence, unlike
  // rotation which needs the ISR.
  const bool reading = digitalRead(EncoderPins::kPinButton);
  const uint32_t now = millis();

  if (reading != s_lastButtonReading && (now - s_lastButtonChangeMs) > kButtonDebounceMs) {
    s_lastButtonChangeMs = now;
    s_lastButtonReading = reading;
    if (reading == LOW) {  // pressed (active-low with INPUT_PULLUP)
      s_buttonPressPending = true;
    }
  }
}

int RotaryEncoder::consumeRotationDelta() {
  int32_t raw;
  portENTER_CRITICAL(&s_mux);
  raw = s_rawCounter;
  s_rawCounter = 0;
  portEXIT_CRITICAL(&s_mux);

  const int32_t total = s_carry + raw;
  const int32_t steps = total / kCountsPerDetent;
  s_carry = total % kCountsPerDetent;
  return static_cast<int>(steps);
}

bool RotaryEncoder::consumeButtonPress() {
  if (!s_buttonPressPending) return false;
  s_buttonPressPending = false;
  return true;
}
