#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static void list_directory(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        printf("ls: cannot open directory %s\n", path);
        return;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        printf("%s\n", entry->d_name);
    }
    
    closedir(dir);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        list_directory(".");
    } else {
        for (int i = 1; i < argc; i++) {
            if (argc > 2) {
                printf("%s:\n", argv[i]);
            }
            list_directory(argv[i]);
            if (i < argc - 1) {
                printf("\n");
            }
        }
    }
    return 0;
}
