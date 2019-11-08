#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printf("This program is used to marshal a memcache 'get' command.\r\n"
           "Usage\t: %s key_prefix [count=16]\r\n"
           "Exmaple\t: %s mykey 20\r\n", argv[0], argv[0]);
    return 1;
  }
  const char* key = argv[1];
  int count = atoi(argv[2]);
  if (count == 0) {
    srand(time(NULL));
    count = 1 + random() % 256;
  }

  printf("get");
  for (int i = 1; i <= count; ++i) {
    printf(" %s%d", key, i);
  }
  printf("\r\n");
  return 0;
}
