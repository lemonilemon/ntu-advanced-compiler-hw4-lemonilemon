#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Complex arithmetic combinations
int32_t test_complex_arithmetic(int32_t x, int32_t y) {
  // Multiple constant folding and combining opportunities
  int32_t a = ((x + 5) + 10) + 15; // -> x + 30
  int32_t b = ((x * 2) * 4) * 8;   // -> x << 6
  int32_t c = (x + y) - y;         // -> x
  int32_t d = (x * 16) / 4;        // -> x << 2

  // Nested operations with multiple optimization opportunities
  int32_t e = (a + b) + (a + b);    // -> 2*(x + 30 + (x << 6))
  int32_t f = ((x << 2) << 3) << 1; // -> x << 6
  int32_t g = x * 2;                // -> x << 1

  return e + f;
}

// Bit manipulation patterns
uint32_t test_bit_patterns(uint32_t x) {
  // Complex shifts and masks
  uint32_t a = (x << 4) >> 2;             // -> (x << 2) & mask
  uint32_t b = (x >> 3) << 3;             // -> x & (-8)
  uint32_t c = (x | 0xFF) & 0xF0;         // -> (x & 0xF0) | 0xF0
  uint32_t d = (x & 0xFF) | (x & 0xFF00); // -> x & 0xFFFF

  // Bit field extractions
  uint32_t e = (x >> 16) & 0xFF;         // Extracts byte 2
  uint32_t f = ((x & 0xFF00) >> 8) << 8; // -> x & 0xFF00

  return a + b + c + d + e + f;
}

// Identity patterns with dependencies
int32_t test_identity_chains(int32_t x, int32_t y) {
  int32_t a = (x + 0) + (y + 0);             // -> x + y
  int32_t b = (x * 1) * (y * 1);             // -> x * y
  int32_t c = ((x + y) - y) + ((y + x) - x); // -> x + y
  int32_t d = (x | 0) | (y & -1);            // -> x | y

  // More complex identities
  int32_t e = (x * 2 + x * 6) * 2; // -> x * 16
  int32_t f = (x << 1) + (x << 3); // -> x * 10

  return a + b + c + d + e + f;
}

// Power of 2 manipulation
uint32_t test_power_of_two(uint32_t x) {
  // Various ways to multiply by powers of 2
  uint32_t a = x * 16; // -> x << 4
  uint32_t b = x * 32; // -> x << 5
  uint32_t c = x * 64; // -> x << 6

  // Complex power of 2 operations
  uint32_t d = (x * 16) * 4;   // -> x << 6
  uint32_t e = (x << 2) * 8;   // -> x << 5
  uint32_t f = (x * 128) / 32; // -> x << 2

  return a + b + c + d + e + f;
}

// Boolean logic optimization
int test_boolean_patterns(int x, int y) {
  // Compare chains
  int a = (x == y) == 1; // -> x == y
  int b = (x != y) == 0; // -> x == y
  int c = !(x != y);     // -> x == y

  // Complex conditions
  int d = (x > y) && (x >= y);  // -> x > y
  int e = (x < y) || (x == y);  // -> x <= y
  int f = (x >= y) && (y >= x); // -> x == y

  return a + b + c + d + e + f;
}

// Complex algebraic simplifications
int32_t test_algebraic_complex(int32_t x, int32_t y) {
  // Distributive law
  int32_t a = (x + y) * 4;  // -> (x * 4) + (y * 4)
  int32_t b = (x + y) * 16; // -> (x << 4) + (y << 4)

  // Combining like terms
  int32_t c = (x * 5) + (x * 3);   // -> x * 8
  int32_t d = (x << 2) + (x << 3); // -> x * 12

  // Complex algebraic identities
  int32_t e = (x + y) - (y - x); // -> 2 * x
  int32_t f = (x * y) / y;       // -> x (if y != 0)

  return a + b + c + d + e + f;
}

// Test driver
int run_all_tests() {
  int32_t x = rand() % 100 + 1;
  int32_t y = rand() % 100 + 1;
  int ret = 0;
  ret ^= test_complex_arithmetic(x, y);
  ret ^= test_bit_patterns(x);
  ret ^= test_identity_chains(x, y);
  ret ^= test_power_of_two(x);
  ret ^= test_boolean_patterns(x, y);
  ret ^= test_algebraic_complex(x, y);
  return ret;
}

int main() {
  int ret = 0;
  for (int i = 0; i < 100000000; i++) {
    ret ^= run_all_tests();
  }
  printf("Result: %d\n", ret);
  return 0;
}
