#include "marshal.h"

#include <stdlib.h>
#include <time.h>

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printf("This program is used to marshal a redis 'touch' command.\r\n"
           "Usage\t: %s key_prefix [count=16]\r\n"
           "Exmaple\t: %s key 32\r\n", argv[0], argv[0]);
    return 1;
  }
  int count = 16;
  if (argc > 2) {
    count = atoi(argv[2]);
  }

  const char* key_prefix = argv[1];

  int bulks = 1 + count;
  printf("*%d\r\n$5\r\ntouch\r\n", bulks);

  char key[64];
  for(int i = 1; i <= count; ++i) {
    sprintf(key, "%s%d", key_prefix, i);
    PrintBulkKey(key);
  }

  return 0;
}
