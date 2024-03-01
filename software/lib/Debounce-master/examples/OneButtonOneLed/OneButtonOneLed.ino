#include <Debounce.h>

// A debounced button that turns a Led on.
// Led is Off, turns On when the button is pressed.
// Also works with INPUT_PULLUP.

byte button = 10;
byte led = LED_BUILTIN;
Debounce Button(button); // A debounced button that will keep working.
// Use like this for INPUT_PULLUP:
  // Debounce Button(button, 50, true);

void setup() {
  pinMode(button, INPUT); // Also works with INPUT_PULLUP
  pinMode(led, OUTPUT);
}

void loop() {
  digitalWrite(led, Button.read()); // Read the button state and write it to the Led.
}
