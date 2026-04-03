#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    printf("=== Persistent Storage Test ===\n\n");
    
    // Test 1: Create and write to a file
    printf("Test 1: Creating /mnt/test.txt...\n");
    int fd = open("/mnt/test.txt", O_CREAT | O_WRONLY, 0644);
    if (fd < 0) {
        printf("  FAILED: Could not create file\n");
        return 1;
    }
    
    const char *msg = "Hello from persistent storage!\n";
    ssize_t written = write(fd, msg, strlen(msg));
    if (written < 0) {
        printf("  FAILED: Could not write to file\n");
        close(fd);
        return 1;
    }
    printf("  SUCCESS: Wrote %d bytes\n", (int)written);
    close(fd);
    
    // Test 2: Read the file back
    printf("\nTest 2: Reading /mnt/test.txt...\n");
    fd = open("/mnt/test.txt", O_RDONLY);
    if (fd < 0) {
        printf("  FAILED: Could not open file for reading\n");
        return 1;
    }
    
    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        printf("  FAILED: Could not read from file\n");
        close(fd);
        return 1;
    }
    buf[n] = '\0';
    printf("  SUCCESS: Read %d bytes: %s", (int)n, buf);
    close(fd);
    
    // Test 3: Create a directory
    printf("\nTest 3: Creating /mnt/data directory...\n");
    if (mkdir("/mnt/data", 0755) < 0) {
        printf("  FAILED: Could not create directory\n");
        return 1;
    }
    printf("  SUCCESS: Directory created\n");
    
    // Test 4: Create file in subdirectory
    printf("\nTest 4: Creating /mnt/data/nested.txt...\n");
    fd = open("/mnt/data/nested.txt", O_CREAT | O_WRONLY, 0644);
    if (fd < 0) {
        printf("  FAILED: Could not create nested file\n");
        return 1;
    }
    
    const char *nested_msg = "Nested file content\n";
    write(fd, nested_msg, strlen(nested_msg));
    close(fd);
    printf("  SUCCESS: Nested file created\n");
    
    // Test 5: Seek test
    printf("\nTest 5: Testing lseek...\n");
    fd = open("/mnt/test.txt", O_RDWR);
    if (fd < 0) {
        printf("  FAILED: Could not open for read-write\n");
        return 1;
    }
    
    off_t pos = lseek(fd, 6, SEEK_SET);
    if (pos < 0) {
        printf("  FAILED: lseek failed\n");
        close(fd);
        return 1;
    }
    printf("  SUCCESS: Seeked to position %d\n", (int)pos);
    
    // Read from new position
    n = read(fd, buf, 10);
    buf[n] = '\0';
    printf("  Read from offset 6: '%s'\n", buf);
    close(fd);
    
    // Test 6: Sync to disk
    printf("\nTest 6: Syncing to disk...\n");
    sync();
    printf("  SUCCESS: Cache flushed to disk\n");
    
    printf("\n=== All Tests Passed! ===\n");
    printf("\nFiles created:\n");
    printf("  /mnt/test.txt\n");
    printf("  /mnt/data/\n");
    printf("  /mnt/data/nested.txt\n");
    printf("\nYou need to check if these files persist across reboots!\n");
    
    return 0;
}
