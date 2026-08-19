/**
 * @file unity_config.h
 * @brief Unity test framework configuration for native tests
 */

#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

// Use standard output for test results
#include <stdio.h>

#define UNITY_OUTPUT_CHAR(c)    putchar(c)
#define UNITY_OUTPUT_FLUSH()    fflush(stdout)

// Enable float support
#define UNITY_INCLUDE_FLOAT
#define UNITY_INCLUDE_DOUBLE

// Use 32-bit integers for test results
#define UNITY_INT_WIDTH 32

#endif // UNITY_CONFIG_H
