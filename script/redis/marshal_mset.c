#include "marshal.h"

#include <stdlib.h>
#include <time.h>

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printf("This program is used to marshal a redis 'mset' command.\r\n"
           "Usage\t: %s key_prefix size [count=16]\r\n"
           "Exmaple\t: %s key 1024 32\r\n", argv[0], argv[0]);
    return 1;
  }
  int count = 16;
  if (argc > 3) {
    count = atoi(argv[3]);
  }

  const char* key_prefix = argv[1];
  int size = atoi(argv[2]);

  if (size == 0) {
    srand(time(NULL));
    size = 1 + random() % (128 * 1024);
  }

  int bulks = 1 + count * 2;

  printf("*%d\r\n$4\r\nmset\r\n", bulks);

  char key[64];
  for(int i = 1; i <= count; ++i) {
    sprintf(key, "%s%d", key_prefix, i);
    PrintBulkKey(key);
    PrintBulkBody(size);
  }

  return 0;
}
