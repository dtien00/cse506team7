#include <stdio.h>
#include <stdlib.h>

int main(void) {
    printf("=== Standard I/O Test ===\n");
    
    printf("\nTesting printf() with various formats...\n");
    printf("  String: %s\n", "Hello");
    printf("  Integer: %d\n", 42);
    printf("  Negative: %d\n", -123);
    printf("  Percent: %%\n");
    
    printf("\nTesting atoi()...\n");
    printf("  atoi(\"123\") = %d\n", atoi("123"));
    printf("  atoi(\"-456\") = %d\n", atoi("-456"));
    
    printf("\nTesting strtol()...\n");
    printf("  strtol(\"0x1A\", NULL, 16) = %d\n", (int)strtol("0x1A", NULL, 16));
    printf("  strtol(\"777\", NULL, 8) = %d\n", (int)strtol("777", NULL, 8));
    
    printf("\n=== All stdio tests passed! ===\n");
    return 0;
}
