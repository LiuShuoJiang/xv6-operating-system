#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int g(int x) {
  return x+3;
}

int f(int x) {
  return g(x);
}

void main(void) {
  printf("%d %d\n", f(8)+1, 13);

  // // Lab4: traps ------ RISC-V assembly
  unsigned int i = 0x00646c72;
	printf("H%x Wo%s\n", 57616, &i);  // HE110 World

  int c1 = 0x64, c2 = 0x6c, c3 = 0x72;
  printf("%c %c %c %x\n", c1, c2, c3, 57616);  // d l r

  exit(0);
}
