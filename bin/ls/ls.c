#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <stddef.h>

int main(int argc, char *argv[]) {
    const char *path = ".";
    
    if (argc > 1) {
        path = argv[1];
    }
    
    DIR *dir = opendir(path);
    if (!dir) {
        write(STDERR_FILENO, "ls: cannot open directory\n", 26);
        return 1;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        printf("%s\n", entry->d_name);
    }
    
    closedir(dir);
    return 0;
}
