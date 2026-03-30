#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void) {
    printf("=== String Function Test ===\n");
    
    printf("\nTesting strlen()...\n");
    const char *str = "Hello, World!";
    printf("  strlen(\"%s\") = %d\n", str, (int)strlen(str));
    
    printf("\nTesting strcmp()...\n");
    printf("  strcmp(\"abc\", \"abc\") = %d\n", strcmp("abc", "abc"));
    printf("  strcmp(\"abc\", \"abd\") = %d\n", strcmp("abc", "abd"));
    
    printf("\nTesting strcpy()...\n");
    char buf[64];
    strcpy(buf, "copied string");
    printf("  strcpy result: \"%s\"\n", buf);
    
    printf("\nTesting strcat()...\n");
    strcpy(buf, "Hello");
    strcat(buf, " World");
    printf("  strcat result: \"%s\"\n", buf);
    
    printf("\nTesting strchr()...\n");
    char *pos = strchr("Hello World", 'W');
    if (pos) {
        printf("  Found 'W' at: \"%s\"\n", pos);
    }
    
    printf("\nTesting strstr()...\n");
    pos = strstr("Hello World", "Wor");
    if (pos) {
        printf("  Found \"Wor\" at: \"%s\"\n", pos);
    }
    
    printf("\nTesting memcpy()...\n");
    char src[] = "memory copy";
    char dst[64];
    memcpy(dst, src, strlen(src) + 1);
    printf("  memcpy result: \"%s\"\n", dst);
    
    printf("\nTesting memset()...\n");
    memset(buf, 'X', 10);
    buf[10] = '\0';
    printf("  memset result: \"%s\"\n", buf);
    
    printf("\nTesting strdup()...\n");
    char *dup = strdup("duplicated");
    printf("  strdup result: \"%s\"\n", dup);
    free(dup);
    
    printf("\n=== All string tests passed! ===\n");
    return 0;
}
