#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        write(STDERR_FILENO, "sleep: missing operand\n", 23);
        return 1;
    }
    
    unsigned int seconds = (unsigned int)atoi(argv[1]);
    sleep(seconds);
    return 0;
}
