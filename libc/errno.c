#include "errno.h"

int __errno_value = 0;

int *__errno_location(void) {
    return &__errno_value;
}