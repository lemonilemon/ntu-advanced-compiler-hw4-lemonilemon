#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
int main() {
  srand(0);
  int x = rand();
  for (int i = 0; i < 100000000; ++i) {
    x = x * 128;
    x = x / 64;
    x = x + i * 0;
    x = x + i ^ i;
    x = x & x;
    x = x | x;
    x = x + !(!(false));
    x = x | 0;
    x = x - 0;
    x = x + 0;
    x = x * 1;
    x = x / 1;
  }
  printf("x = %d\n", x);
  return 0;
}
