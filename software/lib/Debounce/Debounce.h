#pragma once
#include <Arduino.h>

// Stability debouncer: the raw pin value must be stable for DEBOUNCE_MS
// before the reported state changes.  A brief noise spike (< DEBOUNCE_MS)
// resets the stability timer but never propagates to _state.
class Debounce {
public:
  Debounce(int pin) : _pin(pin), _state(HIGH), _rawLast(HIGH), _lastChangeMs(0) {}
  int read() {
    int raw = digitalRead(_pin);
    if (raw != _rawLast) {         // raw changed → restart stability timer
      _rawLast = raw;
      _lastChangeMs = millis();
    }
    if (_rawLast != _state && (millis() - _lastChangeMs) >= 50) {
      _state = _rawLast;           // stable for 50 ms → accept new state
    }
    return _state;
  }
private:
  int _pin;
  int _state;
  int _rawLast;
  unsigned long _lastChangeMs;
};
