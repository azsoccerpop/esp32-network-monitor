#include "RotaryEncoder.h"
#include "pins.h"

namespace {

// Full-step quadrature state-machine decoder (the same well-proven
// algorithm used in Ben Buxton's "Rotary" Arduino library). Far more
// bounce-resistant than naively counting CHANGE interrupts on pin A alone:
// a step is only registered once a complete, valid A/B transition sequence
// finishes -- noisy/bouncy intermediate transitions just get rejected
// rather than mis-counted.
constexpr uint8_t R_START = 0x0;
constexpr uint8_t R_CW_FINAL = 0x1;
constexpr uint8_t R_CW_BEGIN = 0x2;
constexpr uint8_t R_CW_NEXT = 0x3;
constexpr uint8_t R_CCW_BEGIN = 0x4;
constexpr uint8_t R_CCW_FINAL = 0x5;
constexpr uint8_t R_CCW_NEXT = 0x6;
constexpr uint8_t DIR_CW = 0x10;
constexpr uint8_t DIR_CCW = 0x20;

const uint8_t kTransitionTable[7][4] = {
    /* R_START     */ {R_START, R_CW_BEGIN, R_CCW_BEGIN, R_START},
    /* R_CW_FINAL  */ {R_CW_NEXT, R_START, R_CW_FINAL, static_cast<uint8_t>(R_START | DIR_CW)},
    /* R_CW_BEGIN  */ {R_CW_NEXT, R_CW_BEGIN, R_START, R_START},
    /* R_CW_NEXT   */ {R_CW_NEXT, R_CW_BEGIN, R_CW_FINAL, R_START},
    /* R_CCW_BEGIN */ {R_CCW_NEXT, R_START, R_CCW_BEGIN, R_START},
    /* R_CCW_FINAL */ {R_CCW_NEXT, R_CCW_FINAL, R_START, static_cast<uint8_t>(R_START | DIR_CCW)},
    /* R_CCW_NEXT  */ {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},
};

// If clockwise rotation ever ends up decrementing (or vice versa) on
// different hardware in the future, the physical wiring's A/B sense is just
// the opposite of what this table assumes -- flip this rather than
// rewiring anything. Confirmed correct as-is on the current hardware
// (CLK/GPIO26, DT/GPIO27).
constexpr bool kInvertDirection = false;

volatile uint8_t s_state = R_START;
volatile int32_t s_stepCounter = 0;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

bool s_lastButtonReading = HIGH;  // INPUT_PULLUP -- HIGH = not pressed
uint32_t s_lastButtonChangeMs = 0;
bool s_buttonPressPending = false;
constexpr uint32_t kButtonDebounceMs = 200;

void IRAM_ATTR onEncoderChanged() {
  const uint8_t a = digitalRead(EncoderPins::kPinA);
  const uint8_t b = digitalRead(EncoderPins::kPinB);
  const uint8_t code = (a << 1) | b;

  portENTER_CRITICAL_ISR(&s_mux);
  const uint8_t newState = kTransitionTable[s_state & 0x0F][code];
  s_state = newState;
  const uint8_t direction = newState & 0x30;
  if (direction == DIR_CW) {
    s_stepCounter += kInvertDirection ? -1 : 1;
  } else if (direction == DIR_CCW) {
    s_stepCounter += kInvertDirection ? 1 : -1;
  }
  portEXIT_CRITICAL_ISR(&s_mux);
}

}  // namespace

void RotaryEncoder::begin() {
  pinMode(EncoderPins::kPinA, INPUT_PULLUP);
  pinMode(EncoderPins::kPinB, INPUT_PULLUP);
  pinMode(EncoderPins::kPinButton, INPUT_PULLUP);

  // Both pins need to trigger the decoder -- reading only pin A misses
  // transitions and produces unreliable step/direction detection.
  attachInterrupt(digitalPinToInterrupt(EncoderPins::kPinA), onEncoderChanged, CHANGE);
  attachInterrupt(digitalPinToInterrupt(EncoderPins::kPinB), onEncoderChanged, CHANGE);

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
  int32_t steps;
  portENTER_CRITICAL(&s_mux);
  steps = s_stepCounter;
  s_stepCounter = 0;
  portEXIT_CRITICAL(&s_mux);
  return static_cast<int>(steps);
}

bool RotaryEncoder::consumeButtonPress() {
  if (!s_buttonPressPending) return false;
  s_buttonPressPending = false;
  return true;
}
