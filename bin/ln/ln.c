#include <unistd.h>
#include <stdio.h>

int link(const char *oldpath, const char *newpath);
int symlink(const char *target, const char *linkpath);

int main(int argc, char *argv[]) {
    int symbolic = 0;
    int arg_start = 1;
    
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 's') {
        symbolic = 1;
        arg_start = 2;
    }
    
    if (argc < arg_start + 2) {
        write(STDERR_FILENO, "ln: usage: ln [-s] target link\n", 31);
        return 1;
    }
    
    const char *target = argv[arg_start];
    const char *linkpath = argv[arg_start + 1];
    
    int ret;
    if (symbolic) {
        ret = symlink(target, linkpath);
    } else {
        ret = link(target, linkpath);
    }
    
    if (ret < 0) {
        printf("ln: cannot create link '%s'\n", linkpath);
        return 1;
    }
    
    return 0;
}
