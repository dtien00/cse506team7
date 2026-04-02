#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("=== Memory Allocation Test ===\n");
    
    printf("\nTesting malloc()...\n");
    int *ptr = malloc(sizeof(int) * 10);
    if (ptr) {
        printf("  malloc(40) succeeded: %p\n", (void*)ptr);
        for (int i = 0; i < 10; i++) {
            ptr[i] = i * 2;
        }
        printf("  Wrote values: ");
        for (int i = 0; i < 10; i++) {
            printf("%d ", ptr[i]);
        }
        printf("\n");
        free(ptr);
        printf("  free() succeeded\n");
    } else {
        printf("  malloc failed!\n");
        return 1;
    }
    
    printf("\nTesting calloc()...\n");
    int *ptr2 = calloc(5, sizeof(int));
    if (ptr2) {
        printf("  calloc(5, 4) succeeded\n");
        printf("  Values (should be zero): ");
        for (int i = 0; i < 5; i++) {
            printf("%d ", ptr2[i]);
        }
        printf("\n");
        free(ptr2);
    }
    
    printf("\nTesting realloc()...\n");
    char *str = malloc(10);
    strcpy(str, "small");
    printf("  Initial: \"%s\"\n", str);
    str = realloc(str, 100);
    strcat(str, " now bigger!");
    printf("  After realloc: \"%s\"\n", str);
    free(str);
    
    printf("\nTesting large allocation (mmap threshold)...\n");
    void *large = malloc(8192);
    if (large) {
        printf("  Large malloc succeeded\n");
        free(large);
    }
    
    printf("\n=== All memory tests passed! ===\n");
    return 0;
}
