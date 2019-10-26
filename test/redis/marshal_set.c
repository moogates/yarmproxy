#include "marshal.h"

#include <stdlib.h>
#include <time.h>

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printf("This program is used to marshal a redis 'set' command.\r\n"
           "Usage\t: %s key size [expire_seconds] [nx_flag]\r\n"
           "Exmaple\t: %s mykey 1024 86400 1\r\n", argv[0], argv[0]);
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
  int bulks = 3;

  if (expired > 0) {
    bulks += 2;
  }

  if (nx_flag > 0) {
    bulks += 1;
  }

  printf("*%d\r\n$3\r\nset\r\n", bulks);
  PrintBulkKey(key);
  PrintBulkBody(key, size);

  if (expired > 0) {
    printf("$2\r\nEX\r\n$5\r\n86400\r\n");
  }

  if (nx_flag > 0) {
    printf("$2\r\nNX\r\n");
  }

  return 0;
}
