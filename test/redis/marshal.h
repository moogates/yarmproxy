#include <stdio.h>
#include <string.h>

int IntegerLength(size_t value) {
  int len = 1;
  while(value /= 10) {
    ++len;
  }
  return len;
}

void PrintBulkKey(const char* key) {
  printf("$%lu\r\n%s\r\n", strlen(key), key);
}

void PrintBulkBody(const char* key, int body_size) {
  printf("$%d\r\n", body_size);
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

