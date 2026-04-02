#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stddef.h>

static void count_file(int fd, const char *name) {
    char buf[4096];
    int n;
    int lines = 0, words = 0, chars = 0;
    int in_word = 0;
    
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            chars++;
            if (buf[i] == '\n') lines++;
            if (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n') {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }
    
    printf("%d %d %d", lines, words, chars);
    if (name) printf(" %s", name);
    printf("\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        count_file(STDIN_FILENO, NULL);
    } else {
        for (int i = 1; i < argc; i++) {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                printf("wc: cannot open '%s'\n", argv[i]);
                continue;
            }
            count_file(fd, argv[i]);
            close(fd);
        }
    }
    return 0;
}
