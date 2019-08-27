#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char* argv[]) {
  int size = 0;
  int i = 0;
  char ending_line[] = "00_abcdef_10_abcdef_20_abcdef_30_abcdef_40_abcdef_";

  if (argc < 2) {
    printf("Usage: %s size\r\n", argv[0]);
    return 1;
  }
  srand(time(NULL));

  size = atoi(argv[1]);
  if (size == 0) {
    size = 1 + random() % (128 * 1024);
  }

  printf("set EXAMPLE_KEY 0 86400 %d\r\n", size);
  for(i = 0; (i + 1) * 50 <= size; ++i) {
    printf("%08d__10_abcdef_20_abcdef_30_abcdef_40_abcde\r\n", i * 50);
  }
  if (size % 50 > 0) {
    ending_line[size % 50] = 0;
    printf("%s", ending_line);
  }
  printf("\r\n");
  return 0;
}
