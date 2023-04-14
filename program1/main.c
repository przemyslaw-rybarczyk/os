#include <float.h>
#include <stdio.h>

char buf[20];

void main(void) {
    printf("%f\n", 12.3456789012);
    printf("%f\n", -0.00001234567890123456789);
    printf("%f\n", 0.0);
    printf("%f\n", -0.0);
    printf("%f\n", DBL_MAX);
    printf("%f\n", 1.0 / 0.0);
    printf("%F\n", -1.0 / 0.0);
    printf("%F\n", 0.0 / 0.0);
    printf("%f\n", -(0.0 / 0.0));
}
