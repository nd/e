#include <stdio.h>

enum {
  BUFSIZE = 512
};

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: makeFont /path/to/font/file\n");
    return 1;
  }
  FILE *font = fopen(argv[1], "r+b");
  if (!font) {
    printf("Cannot open file\n");
    return 1;
  }
  printf("unsigned char font[] = {");
  unsigned char buf[BUFSIZE];
  while (1) {
    size_t totalCount = 0;
    size_t count = fread(buf, 1, BUFSIZE, font);
    for (size_t i = 0; i < count; i++, totalCount++) {
      if (totalCount % 8 == 0) {
        printf("\n    ");
      }
      printf("0x%02x,", buf[i]);
      if (totalCount % 8 != 7) {
        printf(" ");
      }
    }
    if (feof(font)) {
      break;
    }
  }
  printf("\n};\n");
  return 0;
}