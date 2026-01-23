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
inline unsigned long millis() {
    static unsigned long ms = 0;
    return ms++;
}

inline void delay(unsigned long ms) {
    (void)ms;
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
inline void analogReadResolution(int bits) { (void)bits; }
inline int analogRead(int pin) { (void)pin; return 2048; }  // Mid-scale for 12-bit
inline void analogWrite(int pin, int value) { (void)pin; (void)value; }

// Digital functions
inline void pinMode(int pin, int mode) { (void)pin; (void)mode; }
inline void digitalWrite(int pin, int value) { (void)pin; (void)value; }
inline int digitalRead(int pin) { (void)pin; return 0; }

// Pin modes
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define HIGH 1
#define LOW 0
