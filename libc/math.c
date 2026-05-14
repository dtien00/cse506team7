#include <math.h>

double fabs(double x) {
    return x < 0.0 ? -x : x;
}

/* Newton-Raphson sqrt, good enough for soft-float rv64imac */
double sqrt(double x) {
    if (x < 0.0)
        return NAN;
    if (x == 0.0)
        return 0.0;

    double g = x > 1.0 ? x : 1.0;
    for (int i = 0; i < 60; i++) {
        double g1 = 0.5 * (g + x / g);
        if (g1 == g)
            break;
        g = g1;
    }
    return g;
}
