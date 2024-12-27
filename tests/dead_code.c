#include <stdio.h>

int main() {
  int x = 10;
  int y = 20;

  // Dead code: These computations do not affect the output
  int z = x + y;
  int w = z * 2;

  // Only this statement has an effect
  printf("Hello, World!\n");

  return 0;
}
