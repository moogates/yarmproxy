#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void PrintBody(const char* key, int body_size) {
  char fmt_key[16];
  snprintf(fmt_key, 16, "%15s", key);
  // printf("%s\r\n", fmt_key);
  for(int i = 0; (i + 1) * 50 <= body_size; ++i) {
    printf("%08d__%08d__20__%s_40___45_\r\n",
           i * 50, body_size, fmt_key);
  }

  char ending_line[] = "00_abcdef_10_abcdef_20_abcdef_30_abcdef_40_abcdef_";
  if (body_size % 50 > 0) {
    ending_line[body_size % 50] = 0;
    printf("%s", ending_line);
  }
  printf("\r\n");
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    fprintf(stderr, "This program is used to marshal a memcache 'set' command.\r\n"
           "Usage\t: %s key size [expire_seconds]\r\n"
           "Exmaple\t: %s mykey 1024 86400\r\n", argv[0], argv[0]);
    return 1;
  }
  int expired = 0;
  if (argc > 3) {
    expired = atoi(argv[3]);
  }
  const char* key = argv[1];
  int size = atoi(argv[2]);
  if (size == 0) {
    srand(time(NULL));
    size = 1 + random() % (128 * 1024);
  }

  printf("set %s 0 %d %d\r\n", key, expired, size);
  PrintBody(key, size);
  return 0;
}
