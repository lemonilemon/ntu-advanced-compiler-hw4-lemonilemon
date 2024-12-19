#include <stdio.h>

// Test constant folding
int test_constant_folding(void) {
  int x = 2 + 3; // Should fold to 5
  int y = 4 * 3; // Should fold to 12
  return x + y;  // Should fold to 17
}

// Test multiply by 1
int test_multiply_identity(int x) {
  return x * 1; // Should optimize to just x
}

// Test add zero
int test_add_identity(int x) {
  return x + 0; // Should optimize to just x
}

// Test multiply by 2
int test_multiply_by_two(int x) {
  return x * 2; // Should optimize to x << 1
}

// Test multiple optimizations
int test_multiple_opts(int x) {
  int a = 2 + 3; // Should fold to 5
  int b = x * a; // x * 5
  int c = b * 1; // Should remove the *1
  int d = c + 0; // Should remove the +0
  return d;
}

// Test case that shouldn't be optimized
int test_no_opt(int x, int y) {
  return x + y; // Can't optimize - both variables
}

// Test more complex arithmetic
int test_complex_constant_folding(void) {
  int a = (2 + 3) * 4;  // Should fold to 20
  int b = 10 - (3 + 2); // Should fold to 5
  return a + b;         // Should fold to 25
}

// Test combination of constant and variable
int test_mixed_expressions(int x) {
  int a = (2 + 3) * x; // Should fold 2+3 first
  int b = x + (4 * 2); // Should fold 4*2 first
  return a + b;
}

// Test function for printing results
void run_tests(void) {
  printf("Constant folding test: %d\n", test_constant_folding());

  int x = 5;
  printf("Multiply identity test with %d: %d\n", x, test_multiply_identity(x));
  printf("Add identity test with %d: %d\n", x, test_add_identity(x));
  printf("Multiply by two test with %d: %d\n", x, test_multiply_by_two(x));
  printf("Multiple opts test with %d: %d\n", x, test_multiple_opts(x));

  int y = 3;
  printf("No opt test with %d, %d: %d\n", x, y, test_no_opt(x, y));

  printf("Complex constant folding test: %d\n",
         test_complex_constant_folding());
  printf("Mixed expressions test with %d: %d\n", x, test_mixed_expressions(x));
}

int main() {
  run_tests();
  return 0;
}
