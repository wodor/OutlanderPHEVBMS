/**
 * @file arduino_mock.cpp
 * @brief Mock Arduino implementation for native unit testing
 */
#include "Arduino.h"

MockSerial Serial;
unsigned long g_mockMillis = 0;
