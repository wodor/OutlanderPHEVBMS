/**
 * @file bms_data.cpp
 * @brief Implementation file for BMS data structures
 *
 * EMBEDDED CONCEPT: Header/Source Split
 * --------------------------------------
 * C++ splits code into .h (declarations) and .cpp (definitions).
 * - .h files are like PHP interfaces - they declare what exists
 * - .cpp files provide the actual implementation
 *
 * This split enables:
 * 1. Faster compilation (change .cpp, only recompile that file)
 * 2. Hiding implementation details
 * 3. Avoiding "multiple definition" linker errors
 */

#include "bms_data.h"

// =============================================================================
// GLOBAL STATE DEFINITION
// =============================================================================
/**
 * This is where the actual memory for g_bmsState is allocated.
 * The header only declared it (extern), here we define it.
 *
 * MEMORY NOTE:
 * This struct uses approximately:
 * - 8 modules × (8 longs × 4 bytes + 3 longs × 4 bytes + int + bool) ≈ 360 bytes
 *
 * That's tiny compared to ESP32's 320KB RAM.
 */
BmsState g_bmsState;
