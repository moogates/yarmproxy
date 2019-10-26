#include "marshal.h"

#include <stdlib.h>
#include <time.h>

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printf("This program is used to marshal a redis 'get' command.\r\n"
           "Usage\t: %s key\r\n"
           "Exmaple\t: %s mykey\r\n", argv[0], argv[0]);
    return 1;
  }

  const char* key = argv[1];
  int bulks = 2;

  printf("*%d\r\n$3\r\nget\r\n", bulks);
  PrintBulkKey(key);

  return 0;
}
