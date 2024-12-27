#include <stdio.h>
#include <stdlib.h>
int main() {
  int *fib = malloc(50000000 * sizeof(int));
  fib[0] = 0, fib[1] = 1;
  for (int i = 2; i < 50000000; i++) {
    fib[i] = fib[i - 1] + fib[i - 2];
  }
  printf("%d\n", fib[49999999]);
  free(fib);
  return 0;
}
