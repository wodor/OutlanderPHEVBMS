/**
 * @file arduino_mock.cpp
 * @brief Mock Arduino implementation for native unit testing
 */
#include "Arduino.h"

MockSerial Serial;
unsigned long g_mockMillis = 0;

int g_pinModeState[256] = {0};
int g_digitalWriteState[256] = {0};
int g_digitalReadState[256] = {0};
int g_analogReadState[256] = {0};
int g_analogWriteState[256] = {0};
