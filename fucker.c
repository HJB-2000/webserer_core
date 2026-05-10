#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main() {
    FILE *f = fopen("server.conf", "r+b");
    if (!f) return 1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char *buffer = malloc(size);
    fread(buffer, 1, size, f);

    char *http = strstr(buffer, "http ");
    if (http) {
        long offset = http - buffer + strlen("http");
        printf("Offset: %ld\n", offset);
        char zeros[8] = {0};

        fseek(f, offset, SEEK_SET);
        fwrite(zeros, 8, 8, f);
        // fwrite(NULL, 8, 8, f);
    }

    free(buffer);
    fclose(f);
}