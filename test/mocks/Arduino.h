/**
 * @file Arduino.h
 * @brief Mock Arduino header for native unit testing
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <cmath>
#include <cstdlib>

// Arduino types
typedef uint8_t byte;
typedef bool boolean;

// Time functions
extern unsigned long g_mockMillis;

inline unsigned long millis() {
    return g_mockMillis;
}

inline void delay(unsigned long ms) {
    g_mockMillis += ms;
}

// Mock Serial class
class MockSerial {
public:
    void begin(unsigned long baud) { (void)baud; }
    void print(const char* s) { printf("%s", s); }
    void print(int v) { printf("%d", v); }
    void print(float v) { printf("%f", v); }
    void println() { printf("\n"); }
    void println(const char* s) { printf("%s\n", s); }
    void println(int v) { printf("%d\n", v); }
    void println(float v) { printf("%f\n", v); }
    void printf(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
    }
    int available() { return 0; }
    int read() { return -1; }
};

extern MockSerial Serial;

// Min/max macros
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif

// Constrain
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))

// Map function
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Math functions - bring into global namespace
using std::abs;
using std::isnan;
using std::isinf;

// Analog functions
extern int g_analogReadState[256];
extern int g_analogWriteState[256];
inline void analogReadResolution(int bits) { (void)bits; }
inline void analogSetPinAttenuation(int pin, int atten) { (void)pin; (void)atten; }
inline int analogRead(int pin) { return g_analogReadState[pin]; }
inline void analogWrite(int pin, int value) { g_analogWriteState[pin] = value; }

// Digital functions
extern int g_pinModeState[256];
extern int g_digitalWriteState[256];
extern int g_digitalReadState[256];
inline void pinMode(int pin, int mode) { g_pinModeState[pin] = mode; }
inline void digitalWrite(int pin, int value) { g_digitalWriteState[pin] = value; }
inline int digitalRead(int pin) { return g_digitalReadState[pin]; }

// Pin modes
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define HIGH 1
#define LOW 0

// ADC attenuation constant (placeholder for native tests)
#define ADC_11db 0
