#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int IntegerLength(size_t value) {
  int len = 1;
  while(value /= 10) {
    ++len;
  }
  return len;
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printf("Usage: %s key size\r\n", argv[0]);
    return 1;
  }

  const char* key = argv[1];
  int size = atoi(argv[2]);

  if (size == 0) {
    srand(time(NULL));
    size = 1 + random() % (128 * 1024);
  }

  printf("*3\r\n$3\r\nset\r\n$%d\r\n%s\r\n$%d\r\n",
          strlen(key), key, size);

  for(int i = 0; (i + 1) * 50 <= size; ++i) {
    printf("%08d__10_%06d_20_abcdef_30_abcdef_40_abcde\r\n", i * 50, size);
  }

  char ending_line[] = "00_abcdef_10_abcdef_20_abcdef_30_abcdef_40_abcdef_";
  if (size % 50 > 0) {
    ending_line[size % 50] = 0;
    printf("%s", ending_line);
  }
  printf("\r\n");
  return 0;
}
