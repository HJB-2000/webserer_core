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

    char *match = strstr(buffer, "events ");
    if (match) {
        long offset = match - buffer + strlen("events");
        printf("Offset: %ld\n", offset);

        fseek(f, offset, SEEK_SET);
        fwrite("\0", 1, 1, f);
    }

    free(buffer);
    fclose(f);
}