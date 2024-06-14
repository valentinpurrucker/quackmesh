#include <Arduino.h>

#include <QuackMeshRouter.h>

QuackMeshRouter router;

void setup() {
  Serial.begin(115200);
  router.begin();
}

void loop() {
}
