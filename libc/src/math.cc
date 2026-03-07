#include <math.h>

double fabs(double x) {
    // TODO NaN?
    return x < 0 ? -x : x;
}
