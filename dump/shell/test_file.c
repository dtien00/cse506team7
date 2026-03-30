#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main(void) {
    printf("=== File I/O Test ===\n");
    
    const char *test_file = "/tmp/test.txt";
    const char *test_data = "Hello from file test!\n";
    
    printf("\nTesting open() with O_CREAT | O_WRONLY...\n");
    int fd = open(test_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        printf("  open() failed!\n");
        return 1;
    }
    printf("  open() succeeded, fd=%d\n", fd);
    
    printf("\nTesting write()...\n");
    int len = strlen(test_data);
    int written = write(fd, test_data, len);
    printf("  write() wrote %d bytes\n", written);
    
    printf("\nTesting close()...\n");
    close(fd);
    printf("  close() succeeded\n");
    
    printf("\nTesting open() with O_RDONLY...\n");
    fd = open(test_file, O_RDONLY);
    if (fd < 0) {
        printf("  open() failed!\n");
        return 1;
    }
    printf("  open() succeeded, fd=%d\n", fd);
    
    printf("\nTesting read()...\n");
    char buf[128];
    int n = read(fd, buf, sizeof(buf) - 1);
    buf[n] = '\0';
    printf("  read() read %d bytes: \"%s\"\n", n, buf);
    
    printf("\nTesting lseek()...\n");
    lseek(fd, 0, SEEK_SET);
    printf("  lseek() to beginning\n");
    n = read(fd, buf, 5);
    buf[n] = '\0';
    printf("  read first 5 bytes: \"%s\"\n", buf);
    
    close(fd);
    
    printf("\nTesting unlink()...\n");
    if (unlink(test_file) == 0) {
        printf("  unlink() succeeded\n");
    }
    
    printf("\n=== All file I/O tests passed! ===\n");
    return 0;
}
