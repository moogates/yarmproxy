#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    printf("Usage: %s key size [expire_seconds] [nx_flag]\r\n", argv[0]);
    return 1;
  }
  int expired = 0;
  if (argc > 3) {
    expired = atoi(argv[3]);
  }
  int nx_flag = 0;
  if (argc > 4) {
    nx_flag = atoi(argv[4]);
  }

  const char* key = argv[1];
  int size = atoi(argv[2]);

  if (size == 0) {
    srand(time(NULL));
    size = 1 + random() % (128 * 1024);
  }
  size_t key_len = strlen(key);
  int bulks = 3;

  if (expired > 0) {
    bulks += 2;
  }

  if (nx_flag > 0) {
    bulks += 1;
  }

  printf("*%d\r\n$3\r\nset\r\n$%d\r\n%s\r\n$%d\r\n", bulks, key_len, key, size);

  for(int i = 0; (i + 1) * 50 <= size; ++i) {
    printf("%08d__10_%06d_20_abcdef_30_(%c.*%c)40__abcde\r\n", i * 50, size, key[0], key[key_len - 1]);
    // printf("%08d__10_%06d_20_abcdef_30_abcdef_40_abcde\r\n", i * 50, size);
  }

  char ending_line[] = "00_abcdef_10_abcdef_20_abcdef_30_abcdef_40_abcdef_";
  if (size % 50 > 0) {
    ending_line[size % 50] = 0;
    printf("%s", ending_line);
  }
  printf("\r\n");
  if (expired > 0) {
    printf("$4\r\nkey2\r\n$5\r\n86400\r\n");
  }

  if (nx_flag > 0) {
    printf("$2\r\nNX\r\n");
  }

  return 0;
}
