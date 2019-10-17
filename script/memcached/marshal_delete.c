#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printf("This program is used to marshal a memcache 'delete' command.\r\n"
           "Usage\t: %s key\r\n"
           "Exmaple\t: %s key1\r\n", argv[0], argv[0]);
    return 1;
  }
  const char* key = argv[1];

  printf("delete %s\r\n", key);
  return 0;
}
